#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>

namespace {
constexpr uint8_t kEthernetCsPin = 10;
constexpr uint8_t kSdCsPin = 4;
constexpr bool kUseDhcp = true;
constexpr unsigned long kRequestTimeoutMs = 2000;
constexpr unsigned long kDhcpTimeoutMs = 30000;
constexpr unsigned long kDhcpResponseTimeoutMs = 4000;
constexpr unsigned long kNetworkRetryIntervalMs = 5000;

// Use a different locally administered MAC for each device on the same LAN.
byte mac[] = {0x02, 0x51, 0x00, 0x00, 0x00, 0x01};

// Used only when kUseDhcp is false. Adjust these for your LAN.
IPAddress staticIp(192, 168, 1, 177);
IPAddress dnsServer(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

EthernetServer server(80);
bool networkReady = false;
unsigned long lastNetworkAttemptFinishedAt = 0;

void startNetwork() {
  if (kUseDhcp) {
    Serial.println(F("Requesting an IP address via DHCP (up to 30 seconds)..."));
    if (Ethernet.begin(mac, kDhcpTimeoutMs, kDhcpResponseTimeoutMs) == 0) {
      if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println(F("Ethernet hardware not found. Check the shield and CS pin."));
      } else {
        Serial.println(F("DHCP failed: no lease obtained. Check LAN cable and DHCP router."));
        Serial.println(F("A direct PC connection needs matching static IP settings."));
      }
      Serial.println(F("Retrying automatically in 5 seconds; no RESET needed."));
      lastNetworkAttemptFinishedAt = millis();
      return;
    }
  } else {
    Ethernet.begin(mac, staticIp, dnsServer, gateway, subnet);
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println(F("Ethernet hardware not found. Retrying in 5 seconds."));
      lastNetworkAttemptFinishedAt = millis();
      return;
    }
  }

  Serial.print(F("Ethernet chip: "));
  switch (Ethernet.hardwareStatus()) {
    case EthernetW5100: Serial.println(F("W5100")); break;
    case EthernetW5200: Serial.println(F("W5200")); break;
    case EthernetW5500: Serial.println(F("W5500")); break;
    default: Serial.println(F("unknown")); break;
  }
  Serial.print(F("Subnet: "));
  Serial.println(Ethernet.subnetMask());
  Serial.print(F("Gateway: "));
  Serial.println(Ethernet.gatewayIP());
  server.begin();
  networkReady = true;
  Serial.print(F("Open http://"));
  Serial.print(Ethernet.localIP());
  Serial.println(F("/ in a browser on the same LAN."));
}

void sendPage(EthernetClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println();
  // F() keeps constant text in flash to conserve the Uno's RAM.
  client.println(F("<!doctype html><html lang=\"en\"><head>"));
  client.println(F("<meta charset=\"utf-8\"><title>W5100 sample</title>"));
  client.println(F("</head><body><h1>W5100 Ethernet is working!</h1>"));
  client.print(F("<p>IP address: "));
  client.print(Ethernet.localIP());
  client.print(F("</p><p>Uptime: "));
  client.print(millis() / 1000UL);
  client.println(F(" seconds</p></body></html>"));
}

void handleClient() {
  EthernetClient client = server.available();
  if (!client) {
    return;
  }

  const unsigned long startedAt = millis();
  bool blankLine = true;
  // Read through the HTTP headers without buffering the request in RAM.
  // An absolute timeout also closes connections sending incomplete headers.
  while (client.connected() && millis() - startedAt < kRequestTimeoutMs) {
    if (!client.available()) {
      delay(1);
      continue;
    }

    const char c = client.read();
    if (c == '\n' && blankLine) {
      sendPage(client);
      break;
    }
    if (c == '\n') {
      blankLine = true;
    } else if (c != '\r') {
      blankLine = false;
    }
  }

  delay(1);
  client.stop();
}
}  // namespace

void setup() {
  Serial.begin(115200);

  // Deselect the shield's SD card, which shares the SPI bus with W5100.
  pinMode(kSdCsPin, OUTPUT);
  digitalWrite(kSdCsPin, HIGH);
  Ethernet.init(kEthernetCsPin);

  // Allow shield power-up and PHY initialization before the first attempt.
  delay(1000);
  Serial.println(F("Starting W5100 Ethernet..."));
  startNetwork();
}

void loop() {
  if (!networkReady) {
    if (millis() - lastNetworkAttemptFinishedAt >= kNetworkRetryIntervalMs) {
      startNetwork();
    }
    delay(1);
    return;
  }

  if (kUseDhcp) {
    const int result = Ethernet.maintain();
    if (result == 1 || result == 3) {
      Serial.println(F("DHCP lease renewal failed. Check the network."));
    } else if (result == 2 || result == 4) {
      Serial.print(F("DHCP lease updated. IP: "));
      Serial.println(Ethernet.localIP());
    }
  }
  handleClient();
}
