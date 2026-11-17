#include "../wifi_setup/secrets.h"
#include <WiFi.h>
#include <atomic>

#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/prot/icmp.h"
#include "lwip/raw.h"
#include "lwip/tcpip.h"

constexpr uint8_t SERVO_PIN = 27;
constexpr uint32_t PWM_HZ = 50;
constexpr uint8_t PWM_BITS = 16;
// Pulse widths need calibration for the particular servo and linkage.
constexpr uint32_t CENTER_US = 1500;
constexpr uint32_t AWAY_US = 1250;
constexpr uint32_t HOLD_MS = 600;

enum Phase { IDLE, AWAY, RETURNING };
Phase phase = IDLE;
uint32_t phaseStarted = 0;
uint32_t lastAttempt = 0;
uint32_t movementCount = 0;
bool wasConnected = false;
bool halted = false;
raw_pcb *pingReceiver = nullptr;
// Stays set through the entire movement: additional pings are not queued.
std::atomic<bool> movementRequested{false};

void fail(const char *message) {
  Serial.printf("ERROR %s\n", message);
  ledcDetach(SERVO_PIN);
  halted = true;
}

bool writeServo(uint32_t pulseUs) {
  const uint32_t duty =
      (uint64_t(pulseUs) * PWM_HZ * (1UL << PWM_BITS) + 500000) / 1000000;
  if (!ledcWrite(SERVO_PIN, duty)) {
    fail("servo PWM write failed; stopped");
    return false;
  }
  return true;
}

u8_t onPing(void *, raw_pcb *, pbuf *packet, const ip_addr_t *) {
  const uint16_t headerLength = ip_current_header_tot_len();
  icmp_echo_hdr echo;
  if (!ip4_addr_cmp(ip4_current_dest_addr(),
                    netif_ip4_addr(ip_current_netif())) ||
      packet->tot_len < headerLength + sizeof(echo) ||
      pbuf_copy_partial(packet, &echo, sizeof(echo), headerLength) !=
          sizeof(echo) ||
      echo.type != ICMP_ECHO || echo.code != 0) {
    return 0;
  }

  // Wi-Fi RX buffers may be PBUF_REF: removing their header and adding it
  // back can fail. Inspect through a borrowed descriptor, leaving the original
  // payload pointer and lengths untouched for lwIP's normal echo handling.
  pbuf checksumView = *packet;
  if (pbuf_remove_header(&checksumView, headerLength) != 0)
    return 0;
  const bool valid = inet_chksum_pbuf(&checksumView) == 0;
  if (valid)
    movementRequested.store(true);
  // Do not consume or free the packet: lwIP still sends the normal ping reply.
  return 0;
}

void registerPingReceiver(void *) {
  // raw API calls must execute in the TCP/IP thread, not Arduino's loop task.
  pingReceiver = raw_new_ip_type(IPADDR_TYPE_V4, IP_PROTO_ICMP);
  if (pingReceiver)
    raw_recv(pingReceiver, onPing, nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  if (!ledcAttach(SERVO_PIN, PWM_HZ, PWM_BITS)) {
    fail("servo PWM initialization failed");
    return;
  }
  if (!writeServo(CENTER_US))
    return;
  Serial.printf("SERVO CENTER pin=%u pulse=%lu us\n", SERVO_PIN,
                (unsigned long)CENTER_US);
  delay(HOLD_MS);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32-servo-ping");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastAttempt = millis();
  if (tcpip_callback_wait(registerPingReceiver, nullptr) != ERR_OK ||
      !pingReceiver) {
    fail("ping receiver initialization failed");
    return;
  }
  Serial.printf("Connecting to %s\n", WIFI_SSID);
}

void loop() {
  if (halted) {
    delay(1000);
    return;
  }
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("READY IP=%s RSSI=%d dBm; ping -4 -n 1 %s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
  }
  if (!connected && wasConnected)
    Serial.println("Wi-Fi disconnected");
  if (!connected && now - lastAttempt >= 5000) {
    Serial.printf("Wi-Fi status=%d; retrying %s\n", WiFi.status(), WIFI_SSID);
    WiFi.reconnect();
    lastAttempt = now;
  }
  wasConnected = connected;

  if (phase == IDLE && movementRequested.load()) {
    if (!writeServo(AWAY_US))
      return;
    phase = AWAY;
    phaseStarted = now;
    Serial.printf("SERVO START #%lu pulse=%lu us\n",
                  (unsigned long)++movementCount, (unsigned long)AWAY_US);
  } else if (phase == AWAY && now - phaseStarted >= HOLD_MS) {
    if (!writeServo(CENTER_US))
      return;
    phase = RETURNING;
    phaseStarted = now;
    Serial.println("SERVO RETURN");
  } else if (phase == RETURNING && now - phaseStarted >= HOLD_MS) {
    Serial.printf("SERVO DONE #%lu\n", (unsigned long)movementCount);
    phase = IDLE;
    movementRequested.store(false);
  }
  delay(5);
}
