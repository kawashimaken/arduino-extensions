#include <Arduino.h>
#include <Ethernet.h>
#include <SPI.h>
#include <utility/w5100.h>
#include "radio_config.h"
#include "radio_stream.h"
#include "vs1053_decoder.h"
namespace {
using namespace RadioConfig;
Vs1053Decoder decoder;
EthernetClient stream;
RadioStream parser;
enum class State:uint8_t { Codec,Network,Connect,Playing,Stopped };
State state=State::Codec;
uint8_t attenuation=kInitialAttenuation;
uint8_t rx[128],audio[32],rxSize=0,rxIndex=0,audioSize=0;
byte mac[6]; bool haveNetwork=false;
bool priming=false; unsigned long primingSince=0;
unsigned long lastAttempt=0,connectedAt=0,lastInput=0,lastOutput=0,lastLease=0,lastReport=0;
uint32_t audioBytes=0,networkEmptyPolls=0,decoderBusyPolls=0;
uint32_t lastDiagnosticBytes=0; unsigned long lastDiagnosticAt=0;
uint16_t minQueued=0xFFFF; // Lowest W5100 backlog while playing; near 0 = network underrun.
// Ethernet.available() caches a nonzero count until data is consumed.
// During priming no reads occur, so query the chip register directly instead.
uint16_t hardwareQueuedBytes() {
  const uint8_t socket=stream.getSocketNumber();
  if(socket>=MAX_SOCK_NUM) return 0;
  SPI.beginTransaction(SPISettings(8000000,MSBFIRST,SPI_MODE0));
  uint16_t previous=W5100.readSnRX_RSR(socket), current=previous;
  for(uint8_t tries=0;tries<4;++tries) {
    current=W5100.readSnRX_RSR(socket);
    if(current==previous) break;
    previous=current;
  }
  SPI.endTransaction();
  return current;
}
void resetBuffers() { rxSize=rxIndex=audioSize=0; priming=false; parser.reset(); }
void retry(const __FlashStringHelper* why,State next) {
  Serial.println(why); stream.stop(); resetBuffers(); decoder.setVolume(255);
  state=next; lastAttempt=millis();
}
void help() { Serial.println(F("p play/reconnect, s stop, + louder, - quieter, t tone, ? status, d diagnostics")); }
void status() {
  Serial.print(F("State=")); Serial.print(static_cast<uint8_t>(state));
  Serial.print(F(" attenuation=")); Serial.print(attenuation);
  Serial.print(F(" audio bytes=")); Serial.print(audioBytes);
  Serial.print(F(" TCP queued=")); Serial.print(hardwareQueuedBytes());
  Serial.print(F(" min=")); Serial.println(minQueued==0xFFFF?0:minQueued);
  minQueued=0xFFFF;
}
void diagnostics() {
  status();
  const unsigned long now=millis(), elapsed=now-lastDiagnosticAt;
  Serial.print(F("Audio B/s="));
  Serial.print(elapsed?uint32_t(uint64_t(audioBytes-lastDiagnosticBytes)*1000/elapsed):0);
  Serial.print(F(" net-empty polls=")); Serial.print(networkEmptyPolls);
  Serial.print(F(" DREQ-busy polls=")); Serial.println(decoderBusyPolls);
  lastDiagnosticAt=now; lastDiagnosticBytes=audioBytes;
  uint16_t seconds,hdat0,hdat1,clockf;
  if(state==State::Playing && parser.headersComplete() && !priming &&
     decoder.playbackDiagnostics(seconds,hdat0,hdat1,clockf)) {
    Serial.print(F("Decoded seconds=")); Serial.print(seconds);
    Serial.print(F(" HDAT1=0x")); Serial.print(hdat1,HEX);
    Serial.print(F(" HDAT0=0x")); Serial.print(hdat0,HEX);
    Serial.print(F(" CLOCKF=0x")); Serial.println(clockf,HEX);
  }
}
void commands() {
  while(Serial.available()) {
    const char c=Serial.read();
    if(c=='s') {
      stream.stop(); decoder.setVolume(255); resetBuffers(); state=State::Stopped;
      Serial.println(F("Stopped."));
    } else if(c=='p') {
      stream.stop(); resetBuffers(); state=State::Codec; lastAttempt=millis()-kRetryMs;
    } else if(c=='+'||c=='-') {
      if(c=='+'&&attenuation>=4) attenuation-=4;
      if(c=='-'&&attenuation<=116) attenuation+=4;
      if(state==State::Playing) decoder.setVolume(attenuation);
      status();
    } else if(c=='t') {
      stream.stop(); resetBuffers(); state=State::Stopped;
      Serial.println(F("Short headphone tone test..."));
      if(!decoder.sineTest(attenuation)) Serial.println(F("Tone failed: check pin profile, DREQ, reset and CS."));
      decoder.setVolume(255); Serial.println(F("Tone finished. Press p to play radio."));
    } else if(c=='?') { status(); help(); }
    else if(c=='d') diagnostics();
  }
}
void connectRadio() {
  Serial.print(F("Connecting http://")); Serial.print(kStationHost);
  Serial.print(':'); Serial.print(kStationPort); Serial.println(kStationPath);
  stream.setConnectionTimeout(4000);
  if(!stream.connect(kStationHost,kStationPort)) {
    retry(F("TCP/DNS failed. Retrying in 5 s."),State::Codec); return;
  }
  stream.print(F("GET ")); stream.print(kStationPath); stream.println(F(" HTTP/1.0"));
  stream.print(F("Host: ")); stream.print(kStationHost); stream.print(':'); stream.println(kStationPort);
  stream.println(F("User-Agent: Uno-W5100-Radio/1.0"));
  stream.println(F("Accept: audio/mpeg, audio/aac, audio/aacp")); stream.println(F("Icy-MetaData: 0"));
  stream.println(F("Connection: close")); stream.println();
  resetBuffers(); audioBytes=lastDiagnosticBytes=networkEmptyPolls=decoderBusyPolls=0;
  lastDiagnosticAt=millis(); state=State::Playing;
  connectedAt=lastInput=lastOutput=millis();
}
void playRadio() {
  // Leave incoming bytes in W5100's FIFO while priming, not in Uno RAM.
  if(priming) {
    const uint16_t queued=hardwareQueuedBytes()+(rxSize-rxIndex)+audioSize;
    if(queued<kPrebufferBytes && millis()-primingSince<kPrebufferMaxWaitMs &&
       stream.connected() && !parser.done()) return;
    priming=false; lastOutput=millis(); minQueued=0xFFFF;
    Serial.print(F("Buffered bytes=")); Serial.print(queued); Serial.println(F(". Playing."));
  }
  for(uint16_t budget=0;budget<512;++budget) {
    if(audioSize==sizeof(audio)) {
      if(!decoder.ready()||!decoder.writeAudio(audio,audioSize)) { ++decoderBusyPolls; break; }
      audioBytes+=audioSize; audioSize=0; lastOutput=millis();
    }
    if(rxIndex==rxSize) {
      if(parser.done()) break;
      if(parser.headersComplete()) {
        const uint16_t queued=hardwareQueuedBytes(); if(queued<minQueued) minQueued=queued;
      }
      const int available=stream.available(); if(available<=0) { ++networkEmptyPolls; break; }
      const int count=stream.read(rx,min(available,int(sizeof(rx)))); if(count<=0) break;
      rxSize=uint8_t(count); rxIndex=0; lastInput=millis();
    }
    if(parser.plainAudio()) {
      // Zero-copy fast path. Each DREQ check still authorizes at most 32 bytes.
      // Keep the raw TCP buffer untouched until the codec accepts this block.
      const uint8_t count=min(uint8_t(32),uint8_t(rxSize-rxIndex));
      if(!decoder.ready() || !decoder.writeAudio(rx+rxIndex,count)) {
        ++decoderBusyPolls; break;
      }
      rxIndex+=count; audioBytes+=count; lastOutput=millis();
      budget+=count-1;
      continue;
    }
    const bool hadHeaders=parser.headersComplete();
    const auto result=parser.consume(rx[rxIndex++]);
    if(!hadHeaders&&parser.headersComplete()) {
      priming=true; primingSince=lastOutput=millis();
      Serial.println(F("Audio response accepted. Buffering..."));
      break;
    }
    if(result==RadioStream::Result::Audio) audio[audioSize++]=rx[rxIndex-1];
    if(result==RadioStream::Result::Error) {
      Serial.print(F("HTTP/ICY error=")); Serial.print(static_cast<uint8_t>(parser.error()));
      Serial.print(F(" status=")); Serial.println(parser.statusCode());
      retry(F("Use a direct HTTP MP3/AAC URL: no redirect, HTTPS or playlist."),State::Codec); return;
    }
  }
  if(audioSize&&audioSize<sizeof(audio)&&decoder.ready()&&decoder.writeAudio(audio,audioSize)) {
    audioBytes+=audioSize; audioSize=0; lastOutput=millis();
  }
  if(parser.done()&&!audioSize) { retry(F("Stream finished. Reconnecting."),State::Codec); return; }
  if(!stream.connected()&&!stream.available()&&rxIndex==rxSize&&!audioSize) {
    retry(F("Stream disconnected. Reconnecting."),State::Codec); return;
  }
  if((!parser.headersComplete()&&millis()-connectedAt>=kHeaderTimeoutMs)||
     (parser.headersComplete()&&(millis()-lastInput>=kStallTimeoutMs||
                                millis()-lastOutput>=kStallTimeoutMs))) {
    retry(F("Stream/DREQ stalled. Reconnecting."),State::Codec); return;
  }
  if(kReportIntervalMs && millis()-lastReport>=kReportIntervalMs) { lastReport=millis(); status(); }
}
}
void setup() {
  Serial.begin(115200);
  const uint8_t selects[]={kEthernetCs,kEthernetSdCs,kDecoderCs,kDecoderDcs};
  for(uint8_t pin:selects) { digitalWrite(pin,HIGH); pinMode(pin,OUTPUT); }
  if(kDecoderSdCs>=0) { digitalWrite(kDecoderSdCs,HIGH); pinMode(kDecoderSdCs,OUTPUT); }
  memcpy(mac,kMac,sizeof(mac)); SPI.begin(); Ethernet.init(kEthernetCs); delay(1000);
  Serial.println(F("Uno + W5100 + VS1053 Internet Radio"));
  Serial.print(F("Pin profile=")); Serial.println(RADIO_PIN_PROFILE);
  Serial.println(F("Both SD slots must be empty. Check PCB pin mapping."));
  help(); lastAttempt=millis()-kRetryMs;
}
void loop() {
  commands(); if(state==State::Stopped) { delay(1); return; }
  if(state==State::Playing) playRadio();
  else if(millis()-lastAttempt>=kRetryMs) {
    lastAttempt=millis();
    if(state==State::Codec) {
      if(!decoder.begin(attenuation)) {
        Serial.println(F("VS1053 not found. Check CS/DCS/DREQ/RESET and pin profile.")); lastAttempt=millis();
      } else {
        Serial.println(F("VS1053 detected.")); state=haveNetwork?State::Connect:State::Network;
        lastAttempt=millis()-kRetryMs;
      }
    } else if(state==State::Network) {
      Serial.println(F("Starting Ethernet (DHCP may take 15 s)..."));
      if(kUseDhcp) haveNetwork=Ethernet.begin(mac,15000,2000)!=0;
      else {
        Ethernet.begin(mac,IPAddress(kIp),IPAddress(kDns),IPAddress(kGateway),IPAddress(kSubnet));
        haveNetwork=Ethernet.hardwareStatus()!=EthernetNoHardware;
      }
      lastAttempt=millis();
      if(haveNetwork) {
        Serial.print(F("IP: ")); Serial.println(Ethernet.localIP());
        Serial.print(F("TCP RX buffer bytes=")); Serial.println(W5100.SSIZE);
        state=State::Connect; lastAttempt-=kRetryMs;
      } else Serial.println(F("Ethernet/DHCP failed. Check cable, router and CS."));
    } else if(state==State::Connect) connectRadio();
  }
  if(kUseDhcp&&haveNetwork&&millis()-lastLease>=30000) {
    lastLease=millis(); const int lease=Ethernet.maintain();
    // With one socket, renewal fails while the stream holds it: free it and redo DHCP now.
    if(lease==1||lease==3) {
      haveNetwork=false; retry(F("DHCP renewal: restarting DHCP."),State::Codec); lastAttempt-=kRetryMs;
    } else if(lease==2||lease==4) {
      retry(F("DHCP renewed. Reconnecting."),State::Codec); lastAttempt-=kRetryMs;
    }
  }
}
