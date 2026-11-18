#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "radio_config.h"
// Bounded polled SCI/SDI driver: no SD, interrupts, dynamic allocation or plugin.
class Vs1053Decoder {
 public:
  bool begin(uint8_t volume) {
    using namespace RadioConfig;
    pinMode(kDecoderCs,OUTPUT); digitalWrite(kDecoderCs,HIGH);
    pinMode(kDecoderDcs,OUTPUT); digitalWrite(kDecoderDcs,HIGH);
    pinMode(kDecoderDreq,INPUT);
    pinMode(kDecoderReset,OUTPUT); digitalWrite(kDecoderReset,LOW);
    // On some shields D4 also drives GPIO1 (MIDI boot select).
    // Hold it low ONLY during reset; no SPI clocks until both SD CS are high.
    if(kD4IsDecoderGpio1) digitalWrite(kEthernetSdCs,LOW);
    delay(10); digitalWrite(kDecoderReset,HIGH); delay(100);
    digitalWrite(kEthernetSdCs,HIGH);
    if(!waitReady(1000)) return false;
    uint16_t status;
    if(!readRegister(1,status) || ((status>>4)&15)!=4) return false;
    // CLOCKF 0x8800: SC_MULT 3.5x + SC_ADD 1.0x (up to 4.5x while decoding),
    // VLSI's recommended setting. 3.0x could not decode 128 kbps MP3 in real time here.
    if(!writeRegister(0,0x0800) || !writeRegister(3,0x8800)) return false;
    delay(2); return setVolume(volume);
  }
  bool ready() const { return digitalRead(RadioConfig::kDecoderDreq)==HIGH; }
  bool setVolume(uint8_t value) { return writeRegister(0x0b,(uint16_t(value)<<8)|value); }
  bool writeAudio(const uint8_t* data,uint8_t size) {
    if(size>32 || !ready()) return false;
    SPI.beginTransaction(SPISettings(4000000,MSBFIRST,SPI_MODE0));
    digitalWrite(RadioConfig::kDecoderDcs,LOW);
    for(uint8_t i=0;i<size;++i) SPI.transfer(data[i]);
    digitalWrite(RadioConfig::kDecoderDcs,HIGH); SPI.endTransaction(); return true;
  }
  bool playbackDiagnostics(uint16_t& seconds,uint16_t& hdat0,uint16_t& hdat1,uint16_t& clockf) {
    return readRegister(4,seconds) && readRegister(8,hdat0) && readRegister(9,hdat1) &&
           readRegister(3,clockf);
  }
  bool sineTest(uint8_t volume) {
    if(!begin(volume) || !writeRegister(0,0x0820) || !waitReady(1000)) return false;
    const uint8_t start[]={0x53,0xef,0x6e,0x44,0,0,0,0};
    const uint8_t stop[]={0x45,0x78,0x69,0x74,0,0,0,0};
    if(!writeAudio(start,sizeof(start))) return false;
    delay(400);
    if(!waitReady(1000) || !writeAudio(stop,sizeof(stop))) return false;
    return begin(volume);
  }
 private:
  bool waitReady(unsigned long timeout) const {
    const unsigned long start=millis();
    while(!ready()) { if(millis()-start>=timeout) return false; }
    return true;
  }
  bool readRegister(uint8_t address,uint16_t& value) {
    if(!waitReady(500)) return false;
    SPI.beginTransaction(SPISettings(250000,MSBFIRST,SPI_MODE0));
    digitalWrite(RadioConfig::kDecoderCs,LOW); SPI.transfer(3); SPI.transfer(address);
    value=uint16_t(SPI.transfer(0xff))<<8; value|=SPI.transfer(0xff);
    digitalWrite(RadioConfig::kDecoderCs,HIGH); SPI.endTransaction(); return waitReady(500);
  }
  bool writeRegister(uint8_t address,uint16_t value) {
    if(!waitReady(500)) return false;
    SPI.beginTransaction(SPISettings(250000,MSBFIRST,SPI_MODE0));
    digitalWrite(RadioConfig::kDecoderCs,LOW); SPI.transfer(2); SPI.transfer(address);
    SPI.transfer(uint8_t(value>>8)); SPI.transfer(uint8_t(value));
    digitalWrite(RadioConfig::kDecoderCs,HIGH); SPI.endTransaction(); return waitReady(500);
  }
};
