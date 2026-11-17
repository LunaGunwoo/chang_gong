#include "../wifi_setup/secrets.h"
#include <WiFi.h>
#include <atomic>

#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/prot/icmp.h"
#include "lwip/raw.h"
#include "lwip/tcpip.h"

// W10, W11, W12 in the boat wiring diagram (DRV8833 A channel).
constexpr uint8_t AIN1_PIN = 25;
constexpr uint8_t AIN2_PIN = 26;
constexpr uint8_t STBY_PIN = 33;
constexpr uint32_t PWM_HZ = 20000;
constexpr uint8_t PWM_BITS = 8;
constexpr uint32_t MAX_DUTY = (1UL << PWM_BITS) - 1;
constexpr uint32_t LOW_PERCENT = 30;
constexpr uint32_t HIGH_PERCENT = 60;
constexpr uint32_t LOW_DUTY = (MAX_DUTY * LOW_PERCENT + 50) / 100;
constexpr uint32_t HIGH_DUTY = (MAX_DUTY * HIGH_PERCENT + 50) / 100;
constexpr uint32_t WAKE_MS = 2;
constexpr uint32_t RUN_MS = 1000;
static_assert(LOW_PERCENT > 0 && LOW_PERCENT < HIGH_PERCENT && HIGH_PERCENT <= 100,
              "PWM percentages must satisfy 0 < low < high <= 100");

enum Phase { IDLE, WAKING, LOW_SPEED, HIGH_SPEED };
enum Trigger { TRIGGER_DISABLED, TRIGGER_ARMED, TRIGGER_REQUESTED };
// The request stays latched throughout the sequence; extra pings are discarded.
std::atomic<Trigger> trigger{TRIGGER_DISABLED};
Phase phase = IDLE;
uint32_t phaseStarted = 0;
uint32_t lastAttempt = 0;
uint32_t runCount = 0;
bool wasConnected = false;
bool halted = false;
bool ain1Attached = false;
bool ain2Attached = false;
raw_pcb *pingReceiver = nullptr;

bool stopMotor() {
  trigger.store(TRIGGER_DISABLED);
  // Disable the H-bridge first, even if a later PWM write fails.
  digitalWrite(STBY_PIN, LOW);
  bool ok = true;
  if (ain1Attached) ok = ledcWrite(AIN1_PIN, 0) && ok;
  if (ain2Attached) ok = ledcWrite(AIN2_PIN, 0) && ok;
  phase = IDLE;
  return ok;
}

void fail(const char *message) {
  stopMotor();
  halted = true;
  Serial.printf("ERROR %s; STBY=0\n", message);
}

bool setForward(uint32_t duty) {
  if (!ledcWrite(AIN2_PIN, 0) || !ledcWrite(AIN1_PIN, duty)) {
    fail("motor PWM write failed");
    return false;
  }
  return true;
}

u8_t onPing(void *, raw_pcb *, pbuf *packet, const ip_addr_t *) {
  const uint16_t headerLength = ip_current_header_tot_len();
  icmp_echo_hdr echo;
  if (!ip4_addr_cmp(ip4_current_dest_addr(), netif_ip4_addr(ip_current_netif())) ||
      packet->tot_len < headerLength + sizeof(echo) ||
      pbuf_copy_partial(packet, &echo, sizeof(echo), headerLength) != sizeof(echo) ||
      echo.type != ICMP_ECHO || echo.code != 0) {
    return 0;
  }

  // Borrow a descriptor only. Modifying/restoring the original Wi-Fi PBUF_REF
  // header can fail and crash lwIP's raw_input callback checks.
  pbuf checksumView = *packet;
  if (pbuf_remove_header(&checksumView, headerLength) != 0) return 0;
  if (inet_chksum_pbuf(&checksumView) == 0) {
    Trigger expected = TRIGGER_ARMED;
    trigger.compare_exchange_strong(expected, TRIGGER_REQUESTED);
  }
  // Leave the packet alive for lwIP to send its normal echo reply.
  return 0;
}

void registerPingReceiver(void *) {
  // The raw API must run in the TCP/IP thread.
  pingReceiver = raw_new_ip_type(IPADDR_TYPE_V4, IP_PROTO_ICMP);
  if (pingReceiver) raw_recv(pingReceiver, onPing, nullptr);
}

void setup() {
  digitalWrite(STBY_PIN, LOW);
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);
  Serial.begin(115200);
  delay(500);

  ain1Attached = ledcAttach(AIN1_PIN, PWM_HZ, PWM_BITS);
  ain2Attached = ledcAttach(AIN2_PIN, PWM_HZ, PWM_BITS);
  if (!ain1Attached || !ain2Attached || !stopMotor()) {
    fail("motor PWM initialization failed");
    return;
  }
  Serial.printf("MOTOR STOP AIN1=%u AIN2=%u STBY=%u; waiting for ping\n",
                AIN1_PIN, AIN2_PIN, STBY_PIN);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32-motor-ping");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastAttempt = millis();
  if (tcpip_callback_wait(registerPingReceiver, nullptr) != ERR_OK || !pingReceiver) {
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
  if (!connected) {
    if (wasConnected || phase != IDLE) {
      const bool running = phase != IDLE;
      if (!stopMotor()) {
        fail("motor stop failed");
        return;
      }
      Serial.println(running ? "MOTOR ABORT Wi-Fi disconnected; STBY=0"
                             : "Wi-Fi disconnected; STBY=0");
    }
    if (now - lastAttempt >= 5000) {
      Serial.printf("Wi-Fi status=%d; retrying %s\n", WiFi.status(), WIFI_SSID);
      WiFi.reconnect();
      lastAttempt = now;
    }
    wasConnected = false;
    delay(2);
    return;
  }
  if (!wasConnected) {
    trigger.store(TRIGGER_ARMED);
    Serial.printf("READY IP=%s RSSI=%d dBm; ping -n 1 %s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
  }
  wasConnected = true;

  if (phase == IDLE && trigger.load() == TRIGGER_REQUESTED) {
    digitalWrite(STBY_PIN, HIGH);
    phase = WAKING;
    phaseStarted = now;
    Serial.printf("MOTOR START #%lu\n", (unsigned long)++runCount);
  } else if (phase == WAKING && now - phaseStarted >= WAKE_MS) {
    if (!setForward(LOW_DUTY)) return;
    phase = LOW_SPEED;
    phaseStarted = now;
    Serial.printf("MOTOR LOW duty=%lu/%lu (%lu%%)\n", (unsigned long)LOW_DUTY,
                  (unsigned long)MAX_DUTY, (unsigned long)LOW_PERCENT);
  } else if (phase == LOW_SPEED && now - phaseStarted >= RUN_MS) {
    if (!setForward(HIGH_DUTY)) return;
    phase = HIGH_SPEED;
    phaseStarted = now;
    Serial.printf("MOTOR HIGH duty=%lu/%lu (%lu%%)\n", (unsigned long)HIGH_DUTY,
                  (unsigned long)MAX_DUTY, (unsigned long)HIGH_PERCENT);
  } else if (phase == HIGH_SPEED && now - phaseStarted >= RUN_MS) {
    if (!stopMotor()) {
      fail("motor stop failed");
      return;
    }
    Serial.printf("MOTOR DONE #%lu STBY=0\n", (unsigned long)runCount);
    trigger.store(TRIGGER_ARMED);
  }
  delay(2);
}
