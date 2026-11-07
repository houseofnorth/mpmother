#include "motors.h"
#include "settings.h"

// ---- tuning ----
const float    PWM_FREQUENCY   = 20000;  // Hz. 20 kHz is above hearing, DRV8833 handles it fine
const uint32_t SLEEP_AFTER_MS  = 2000;   // put the drivers to sleep when no motor ran for this long
const uint32_t WAKE_SETTLE_MS  = 2;      // ignore nFAULT this long after waking
const uint32_t FAULT_RETRY_MS  = 1000;   // after a fault, wait this long before motors may run again
const uint32_t FAULT_WINDOW_MS = 10000;  // this many faults ...
const uint8_t  FAULTS_TO_LATCH = 3;      // ... inside the window = give up until cleared

static int      speeds[NUM_KNOBS];
static bool     awake = false;
static uint32_t lastRunning = 0, wokeAt = 0;
static bool     faultActive = false, faultLatched = false;
static uint32_t faultAt = 0, firstFaultAt = 0;
static uint8_t  faultCount = 0;

static void writePins(uint8_t k, int speed) {
  if (settings.reverse[k]) speed = -speed;
  // DRV8833 "fast decay": PWM on one input, the other input low. Both low = coast.
  if (speed > 0)      { analogWrite(PIN_MOTOR_IN1[k], speed);  analogWrite(PIN_MOTOR_IN2[k], 0); }
  else if (speed < 0) { analogWrite(PIN_MOTOR_IN1[k], 0);      analogWrite(PIN_MOTOR_IN2[k], -speed); }
  else                { analogWrite(PIN_MOTOR_IN1[k], 0);      analogWrite(PIN_MOTOR_IN2[k], 0); }
}

static void sleepDrivers() {
  digitalWrite(PIN_DRIVER_SLEEP, LOW);
  awake = false;
}

static void wakeDrivers() {
  if (awake) return;
  digitalWrite(PIN_DRIVER_SLEEP, HIGH);
  awake = true;
  wokeAt = millis();
}

void setupMotors() {
  pinMode(PIN_DRIVER_SLEEP, OUTPUT);
  sleepDrivers();
  pinMode(PIN_DRIVER_FAULT, INPUT_PULLUP);  // board has a 10k pull-up too
  analogWriteResolution(8);
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    pinMode(PIN_MOTOR_IN1[k], OUTPUT);
    pinMode(PIN_MOTOR_IN2[k], OUTPUT);
    analogWriteFrequency(PIN_MOTOR_IN1[k], PWM_FREQUENCY);
    analogWriteFrequency(PIN_MOTOR_IN2[k], PWM_FREQUENCY);
    speeds[k] = 0;
    writePins(k, 0);
  }
}

void setMotor(uint8_t k, int speed) {
  speed = constrain(speed, -255, 255);
  if (faultActive || faultLatched || !settings.motorsOn) speed = 0;  // motors off = drivers stay asleep
  speeds[k] = speed;
  if (speed != 0) { wakeDrivers(); lastRunning = millis(); }
  writePins(k, speed);
}

void stopAllMotors() {
  for (uint8_t k = 0; k < NUM_KNOBS; k++) setMotor(k, 0);
}

void updateMotors() {
  uint32_t now = millis();

  // Fault check. nFAULT is only meaningful while the drivers are awake.
  if (awake && now - wokeAt > WAKE_SETTLE_MS && digitalRead(PIN_DRIVER_FAULT) == LOW) {
    if (!faultActive) {
      faultActive = true;
      faultAt = now;
      if (faultCount == 0 || now - firstFaultAt > FAULT_WINDOW_MS) { faultCount = 0; firstFaultAt = now; }
      if (++faultCount >= FAULTS_TO_LATCH) faultLatched = true;
    }
    stopAllMotors();
    sleepDrivers();  // sleeping also resets the driver's fault
  }
  if (faultActive && !faultLatched && now - faultAt > FAULT_RETRY_MS) faultActive = false;

  // Sleep when nothing has moved for a while.
  bool anyRunning = false;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) if (speeds[k] != 0) anyRunning = true;
  if (anyRunning) lastRunning = now;
  else if (awake && now - lastRunning > SLEEP_AFTER_MS) sleepDrivers();
}

int  motorSpeed(uint8_t k)  { return speeds[k]; }
bool driversAwake()         { return awake; }
bool driverFault()          { return faultActive || faultLatched; }
bool driverFaultLatched()   { return faultLatched; }

void clearDriverFault() {
  faultActive = faultLatched = false;
  faultCount = 0;
}
