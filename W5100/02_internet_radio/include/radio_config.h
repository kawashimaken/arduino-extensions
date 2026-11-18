#pragma once
#include <Arduino.h>
// 1: Geeetech MP3-TF.zip default / SparkFun-compatible (no CS rewiring).
// 2: Cirkit example, ONLY with decoder CS physically rerouted D10 -> D6.
#ifndef RADIO_PIN_PROFILE
#define RADIO_PIN_PROFILE 1
#endif
namespace RadioConfig {
constexpr uint8_t kEthernetCs=10, kEthernetSdCs=4;
#if RADIO_PIN_PROFILE == 1
constexpr uint8_t kDecoderCs=6, kDecoderDcs=7, kDecoderDreq=2, kDecoderReset=8;
constexpr int8_t kDecoderSdCs=9;
constexpr bool kD4IsDecoderGpio1=true;
#elif RADIO_PIN_PROFILE == 2
constexpr uint8_t kDecoderCs=6, kDecoderDcs=8, kDecoderDreq=3, kDecoderReset=9;
// Article does not specify the Arduino pin for SD CS. Keep both slots empty.
constexpr int8_t kDecoderSdCs=-1;
constexpr bool kD4IsDecoderGpio1=false;
#else
#error "Select a documented RADIO_PIN_PROFILE"
#endif
static_assert(kDecoderCs!=kEthernetCs && kDecoderDcs!=kEthernetCs &&
              kDecoderReset!=kEthernetCs && kDecoderDreq!=kEthernetCs,
              "VS1053 pins conflict with Ethernet CS");
static_assert(kDecoderCs!=kDecoderDcs && kDecoderCs!=kDecoderReset &&
              kDecoderCs!=kDecoderDreq && kDecoderDcs!=kDecoderReset &&
              kDecoderDcs!=kDecoderDreq && kDecoderReset!=kDecoderDreq,
              "VS1053 control pins must be distinct");
// Public example for individual listening. Direct stream links may change.
constexpr char kStationHost[]="ice1.somafm.com";
// 64 kbps HE-AAC (8 KB/s): fits the ~10 KB/s the W5100 sustained here.
// The 128 kbps MP3 stream ("/groovesalad-128-mp3") needs 16 KB/s.
constexpr char kStationPath[]="/groovesalad-64-aac";
constexpr uint16_t kStationPort=80;
constexpr uint8_t kInitialAttenuation=50; // 0.5 dB/step. Lower = louder.
constexpr unsigned long kRetryMs=5000, kHeaderTimeoutMs=15000, kStallTimeoutMs=10000;
// Prime the W5100 hardware FIFO (8 KB with one socket) before starting audio.
// Keep ~2 KB free: W5100 advertises free RX space as the TCP window.
constexpr uint16_t kPrebufferBytes=6144;
constexpr unsigned long kPrebufferMaxWaitMs=3000;
constexpr unsigned long kReportIntervalMs=0; // Manual '?' only by default.
constexpr bool kUseDhcp=true;
// Example-only network values, not an actual device's identifiers.
constexpr uint8_t kMac[]={0x02,0x51,0x00,0x00,0x00,0x02};
constexpr uint8_t kIp[]={192,168,1,178}, kDns[]={192,168,1,1};
constexpr uint8_t kGateway[]={192,168,1,1}, kSubnet[]={255,255,255,0};
}
