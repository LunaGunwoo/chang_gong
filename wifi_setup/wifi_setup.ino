#include <WiFi.h>
#include "secrets.h"

unsigned long lastAttempt = 0;
bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32-wifi-setup");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastAttempt = millis();
  Serial.printf("Connecting to %s\n", WIFI_SSID);
}

void loop() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("CONNECTED SSID=%s IP=%s RSSI=%d dBm\n", WIFI_SSID,
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (!connected && millis() - lastAttempt >= 5000) {
    Serial.printf("Wi-Fi status=%d; retrying %s\n", WiFi.status(), WIFI_SSID);
    WiFi.reconnect();
    lastAttempt = millis();
  }
  wasConnected = connected;
  delay(100);
}
