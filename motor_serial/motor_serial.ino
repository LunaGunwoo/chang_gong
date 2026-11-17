#include <Arduino.h>

constexpr uint8_t AIN1_PIN = 25;
constexpr uint8_t AIN2_PIN = 26;
constexpr uint8_t STBY_PIN = 33;
constexpr uint32_t PWM_HZ = 20000;
constexpr uint8_t PWM_BITS = 8;
constexpr uint32_t LOW_PERCENT = 30;
constexpr uint32_t HIGH_PERCENT = 60;
constexpr uint32_t MAX_DUTY = (1UL << PWM_BITS) - 1;
constexpr uint32_t LOW_DUTY = (MAX_DUTY * LOW_PERCENT + 50) / 100;
constexpr uint32_t HIGH_DUTY = (MAX_DUTY * HIGH_PERCENT + 50) / 100;
constexpr uint32_t RUN_MS = 1000;
constexpr uint32_t WAKE_MS = 2;
static_assert(LOW_PERCENT > 0 && LOW_PERCENT < HIGH_PERCENT && HIGH_PERCENT <= 100,
              "Use 0 < low < high <= 100");

enum Phase { IDLE, WAKING, LOW_SPEED, HIGH_SPEED };
Phase phase = IDLE;
uint32_t phaseStarted = 0;
bool ain1Attached = false;
bool ain2Attached = false;
bool halted = false;

bool stopMotor() {
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
    fail("PWM write failed");
    return false;
  }
  return true;
}

void setup() {
  digitalWrite(STBY_PIN, LOW);
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);
  Serial.begin(115200);
  ain1Attached = ledcAttach(AIN1_PIN, PWM_HZ, PWM_BITS);
  ain2Attached = ledcAttach(AIN2_PIN, PWM_HZ, PWM_BITS);
  if (!ain1Attached || !ain2Attached || !stopMotor()) {
    fail("PWM initialization failed");
    return;
  }
  Serial.println("READY USB; r=run once, s=stop; STBY=0");
}

void loop() {
  if (halted) {
    delay(100);
    return;
  }
  // Snapshot the available bytes so continuous input cannot delay motor timing.
  const int available = Serial.available();
  bool stopped = false;
  for (int i = 0; i < available; ++i) {
    const int command = Serial.read();
    if (command == 's' || command == 'S') {
      if (!stopMotor()) {
        fail("motor stop failed");
        return;
      }
      stopped = true;
      Serial.println("MOTOR STOP STBY=0");
    } else if (command == 'r' || command == 'R') {
      if (stopped || phase != IDLE) {
        Serial.println("BUSY command ignored");
      } else {
        digitalWrite(STBY_PIN, HIGH);
        phase = WAKING;
        phaseStarted = millis();
        Serial.println("MOTOR START");
      }
    }
  }

  const uint32_t now = millis();
  if (phase == WAKING && now - phaseStarted >= WAKE_MS) {
    if (!setForward(LOW_DUTY)) return;
    phase = LOW_SPEED;
    phaseStarted = now;
    Serial.printf("MOTOR LOW duty=%lu/%lu\n", (unsigned long)LOW_DUTY,
                  (unsigned long)MAX_DUTY);
  } else if (phase == LOW_SPEED && now - phaseStarted >= RUN_MS) {
    if (!setForward(HIGH_DUTY)) return;
    phase = HIGH_SPEED;
    phaseStarted = now;
    Serial.printf("MOTOR HIGH duty=%lu/%lu\n", (unsigned long)HIGH_DUTY,
                  (unsigned long)MAX_DUTY);
  } else if (phase == HIGH_SPEED && now - phaseStarted >= RUN_MS) {
    if (!stopMotor()) {
      fail("motor stop failed");
      return;
    }
    Serial.println("MOTOR DONE STBY=0");
  }
  delay(2);
}
