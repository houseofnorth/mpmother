#include "knobs.h"
#include "pots.h"
#include "motors.h"
#include "settings.h"
#include "midi_io.h"

// ---- tuning (check these on the bench) ----
const int      MIN_DUTY         = 70;    // below this the gearbox does not turn
const float    KP               = 1.2f;  // motor duty per ADC count of distance to target
const int      DEADBAND         = 6;     // close enough, in ADC counts (0-1023)
const int      TOUCH_MOVE       = 14;    // unexpected movement that counts as a hand
const uint32_t SPINUP_MS        = 150;   // grace time before stall detection
const uint32_t STALL_MS         = 300;   // no progress this long while driving = held
const uint32_t MOVE_TIMEOUT_MS  = 2500;  // hard stop for any single move
const uint32_t SETTLE_MS        = 80;    // let the pot settle after the motor stops
const uint32_t USER_RELEASE_MS  = 1500;  // no movement this long = the hand let go
const uint32_t ECHO_MS          = 300;   // ignore Ableton echoing our own CC back
const uint8_t  STRIKES_TO_FAULT = 3;     // this many bad moves in a row = FAULT
const uint32_t FAULT_RETRY_MS   = 60000; // direction / not-moving faults try again after this
                                         // (a person fighting the knob can look like a fault)

struct Knob {
  KnobState state = IDLE;
  KnobFault fault = FAULT_NONE;
  int       target = -1;                // ADC target, -1 = motor off
  int       topSpeed = 255;
  uint32_t  moveStart = 0, lastProgress = 0;
  int       progressRef = 0;
  bool      resting = false;            // restPos is valid
  int       restPos = 0;
  uint32_t  stoppedAt = 0;
  uint32_t  lastHandMove = 0;
  int       handRef = 0;
  bool      handMoved = false;          // did the pot move at all during this USER period?
  bool      enteredUserFromStall = false;
  int       lastCC = -1;
  uint32_t  lastSentAt = 0;
  uint8_t   wrongWayStrikes = 0, notMovingStrikes = 0;
  uint32_t  faultAt = 0;
};
static Knob knobs[NUM_KNOBS];

// ---------- helpers ----------
static int potRange(uint8_t k)          { return max(1, settings.calHigh[k] - settings.calLow[k]); }
static int ccToPosition(uint8_t k, int v) { return settings.calLow[k] + (long)v * potRange(k) / 127; }
static int speedToDuty(uint8_t speed)   { return MIN_DUTY + (255 - MIN_DUTY) * speed / 127; }

// Send CC only when the knob has clearly moved into a new step (0.75 step hysteresis).
static void reportPosition(uint8_t k) {
  Knob &n = knobs[k];
  float exact = (potPosition(k) - settings.calLow[k]) * 127.0f / potRange(k);
  exact = constrain(exact, 0.0f, 127.0f);
  if (n.lastCC < 0 || fabsf(exact - n.lastCC) > 0.75f) {
    int value = constrain((int)(exact + 0.5f), 0, 127);
    if (value != n.lastCC) {
      sendKnobCC(k, value);
      n.lastCC = value;
      n.lastSentAt = millis();
    }
  }
}

static void stopMotor(uint8_t k) {
  setMotor(k, 0);
  knobs[k].target = -1;
  knobs[k].stoppedAt = millis();
  knobs[k].resting = false;
}

static void startMove(uint8_t k, int target, int topSpeed, KnobState why) {
  Knob &n = knobs[k];
  n.target = constrain(target, settings.calLow[k], settings.calHigh[k]);
  n.topSpeed = topSpeed;
  n.state = why;
  n.moveStart = n.lastProgress = millis();
  n.progressRef = potPosition(k);
  n.resting = false;
}

static void enterFault(uint8_t k, KnobFault why) {
  stopMotor(k);
  knobs[k].state = FAULT;
  knobs[k].fault = why;
  knobs[k].faultAt = millis();
}

static void enterUser(uint8_t k, bool fromStall) {
  stopMotor(k);
  Knob &n = knobs[k];
  n.state = USER;
  n.lastHandMove = millis();
  n.handRef = potPosition(k);
  n.handMoved = false;
  n.enteredUserFromStall = fromStall;
}

static void moveFinished(uint8_t k) {
  knobs[k].wrongWayStrikes = 0;
  knobs[k].notMovingStrikes = 0;
}

// ---------- per-state behaviour ----------
static void whileUser(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  int pos = potPosition(k);
  reportPosition(k);
  if (abs(pos - n.handRef) > 3) {
    n.handRef = pos;
    n.lastHandMove = now;
    n.handMoved = true;
  }
  if (now - n.lastHandMove > USER_RELEASE_MS) {
    // A "stall" with no movement at all afterwards is probably not a hand - it is a dead motor.
    if (n.enteredUserFromStall && !n.handMoved) {
      if (++n.notMovingStrikes >= STRIKES_TO_FAULT) { enterFault(k, FAULT_NOT_MOVING); return; }
    }
    n.state = IDLE;
    n.restPos = pos;
    n.resting = true;
  }
}

static void whileDriving(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  int pos = potPosition(k);
  int distance = n.target - pos;

  if (abs(distance) <= DEADBAND) {  // arrived
    stopMotor(k);
    moveFinished(k);
    n.state = IDLE;
    return;
  }

  int duty = constrain((int)(abs(distance) * KP), MIN_DUTY, n.topSpeed);
  int speed = distance > 0 ? duty : -duty;
  setMotor(k, speed);

  // Is a hand fighting the motor?
  if (now - n.moveStart > SPINUP_MS) {
    int direction = speed > 0 ? 1 : -1;
    int moved = (pos - n.progressRef) * direction;
    if (moved < -TOUCH_MOVE) {                        // going the wrong way
      if (++n.wrongWayStrikes >= STRIKES_TO_FAULT) { enterFault(k, FAULT_DIRECTION); return; }
      enterUser(k, false);
      return;
    }
    if (moved >= 3) { n.progressRef = pos; n.lastProgress = now; }
    else if (now - n.lastProgress > STALL_MS) { enterUser(k, true); return; }  // held still
  }

  if (now - n.moveStart > MOVE_TIMEOUT_MS) {         // safety stop
    stopMotor(k);
    n.state = IDLE;
  }
}

static void whileResting(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  int pos = potPosition(k);

  if (!n.resting) {  // wait for the pot to settle after the motor stopped
    if (now - n.stoppedAt > SETTLE_MS) { n.restPos = pos; n.resting = true; }
    return;
  }
  if (abs(pos - n.restPos) > TOUCH_MOVE) enterUser(k, false);  // turned by hand
}

static void whileFault(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  setMotor(k, 0);
  reportPosition(k);  // still usable as a plain knob

  // Worn pots stay faulted. The other faults get one more try after a while:
  // one more bad move and the fault is back straight away.
  if (n.fault != FAULT_WIPERS && now - n.faultAt > FAULT_RETRY_MS) {
    n.state = IDLE;
    n.fault = FAULT_NONE;
    n.wrongWayStrikes = n.notMovingStrikes = STRIKES_TO_FAULT - 1;
    n.restPos = potPosition(k);
    n.resting = true;
  }
}

// ---------- public ----------
void setupKnobs() {
  uint32_t now = millis();
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    knobs[k].stoppedAt = now;
    stopMotor(k);
  }
}

void updateKnobs() {
  uint32_t now = millis();
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    Knob &n = knobs[k];
    if (n.state != FAULT && potBroken(k)) enterFault(k, FAULT_WIPERS);

    if (n.state == FAULT)       whileFault(k, now);
    else if (n.state == USER)   whileUser(k, now);
    else if (n.target >= 0)     whileDriving(k, now);
    else                        whileResting(k, now);
  }
}

void moveKnobFromMidi(uint8_t k, uint8_t value, bool isEcho) {
  Knob &n = knobs[k];
  uint32_t now = millis();
  if (n.state == USER || n.state == FAULT) return;  // a hand always wins
  if (isEcho && now - n.lastSentAt < ECHO_MS && abs((int)value - n.lastCC) <= 1) return;

  n.lastCC = value;
  int target = ccToPosition(k, value);
  if (abs(target - potPosition(k)) <= DEADBAND) {
    if (n.target >= 0) stopMotor(k);
    n.state = IDLE;
  } else {
    startMove(k, target, speedToDuty(settings.maxSpeed), FOLLOW);
  }
}

void sendAllPositions() {
  for (uint8_t k = 0; k < NUM_KNOBS; k++) { knobs[k].lastCC = -1; reportPosition(k); }
}

void clearKnobFaults() {
  clearPotFaults();
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    Knob &n = knobs[k];
    n.wrongWayStrikes = n.notMovingStrikes = 0;
    if (n.state == FAULT) { n.state = IDLE; n.fault = FAULT_NONE; }
  }
}

// Drive a motor for a fixed time while keeping the watchdog fed.
static void runFor(uint8_t k, int speed, uint32_t ms, void (*keepAlive)()) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    setMotor(k, speed);
    updateMotors();
    keepAlive();
    if (driverFault()) break;
    delay(1);
  }
  setMotor(k, 0);
}

static void waitFor(uint32_t ms, void (*keepAlive)()) {
  uint32_t start = millis();
  while (millis() - start < ms) { keepAlive(); delay(1); }
}

void calibrateKnobs(void (*keepAlive)(), int which) {
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    if (which != ALL_KNOBS && which != k) continue;
    // 1. Which way does the motor turn the pot?
    settings.reverse[k] = false;
    int p0 = readPotSlow(k);
    runFor(k, 200, 400, keepAlive);  waitFor(100, keepAlive);
    int p1 = readPotSlow(k);
    if (abs(p1 - p0) < 10) {  // probably sitting at an end stop - try the other way
      runFor(k, -200, 400, keepAlive);  waitFor(100, keepAlive);
      int p2 = readPotSlow(k);
      settings.reverse[k] = p2 > p1;
    } else {
      settings.reverse[k] = p1 < p0;
    }
    // 2. Find both end stops.
    runFor(k, -220, 1500, keepAlive);  waitFor(150, keepAlive);
    int low = readPotSlow(k);
    runFor(k, 220, 1500, keepAlive);   waitFor(150, keepAlive);
    int high = readPotSlow(k);
    bool ok = high - low > 200;
    if (ok) { settings.calLow[k] = low + 8; settings.calHigh[k] = high - 8; }
    Serial.printf("calibrate knob %d: low %d  high %d  reverse %d  %s\n", k + 1, low, high,
                  settings.reverse[k], ok ? "ok" : "range too small, kept old values - check wiring");
    stopMotor(k);
    knobs[k].state = IDLE;
  }
  saveSettings();
  sendAllPositions();
}

KnobState knobState(uint8_t k)  { return knobs[k].state; }
KnobFault knobFault(uint8_t k)  { return knobs[k].fault; }
int       knobTarget(uint8_t k) { return knobs[k].target; }
int       knobLastCC(uint8_t k) { return knobs[k].lastCC; }

const char *knobStateName(uint8_t k) {
  static const char *names[] = { "idle", "moving", "touch", "fault" };
  return names[knobs[k].state];
}

const char *knobFaultName(uint8_t k) {
  static const char *names[] = { "", "wiper mismatch", "motor reversed", "motor stalled" };
  return names[knobs[k].fault];
}
