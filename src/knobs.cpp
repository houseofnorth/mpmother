#include "knobs.h"
#include "pots.h"
#include "motors.h"
#include "settings.h"
#include "midi_io.h"

// ---- tuning (check these on the bench) ----
// Defaults are for slow, weak motors on 5 V: about 2-3 s for full travel at full speed.
const int      MIN_DRIVE        = 150;   // lowest PWM (0-255) a moving motor ever gets; less stalls at 5 V
const float    KP               = 1.2f;  // motor duty per ADC count of distance to target (slows down
                                         // near it, but never below MIN_DRIVE)
const int      DEADBAND         = 6;     // close enough, in ADC counts (0-1023)
const int      TOUCH_MOVE       = 14;    // unexpected movement that counts as a hand
const uint32_t SPINUP_MS        = 300;   // grace time before stall detection (slow motors start slowly)
const uint32_t STALL_WINDOW_MS  = 600;   // the knob must gain STALL_PROGRESS towards the target within
const int      STALL_PROGRESS   = 4;     // this long, in ADC counts - otherwise it is held (or the motor is dead)
const uint32_t MOVE_TIMEOUT_MS  = 6000;  // hard stop for any single move
const uint32_t KICK_MS          = 60;    // a motor starting from standstill (or turning round) gets
const int      KICK_DRIVE       = 255;   // this PWM for up to KICK_MS, until the knob has moved
const int      KICK_MOVED       = 2;     // this many ADC counts: breaks the gearbox free on small, slow moves
const int      NEAR_STEPS       = 2;     // stuck within this many CC steps of the target = arrived, not a stall
                                         // (slow automation sends one-step moves the motor may not finish)
const uint32_t SETTLE_MS        = 80;    // let the pot settle after the motor stops
const uint32_t USER_RELEASE_MS  = 1500;  // no movement this long = the hand let go
const uint32_t ECHO_MS          = 300;   // ignore Ableton echoing our own CC back
const uint8_t  STRIKES_TO_FAULT = 3;     // this many bad moves in a row = FAULT
const uint32_t FAULT_RETRY_MS   = 60000; // direction / not-moving faults try again after this
                                         // (a person fighting the knob can look like a fault)

// ---- calibration: drive each way at full speed until the knob stops at its end stop ----
const int      CAL_SPEED        = 255;   // full speed
const int      CAL_STILL_COUNTS = 3;     // moving less than this (ADC counts) ...
const uint32_t CAL_STILL_MS     = 400;   // ... for this long = at the end stop
const uint32_t CAL_TIMEOUT_MS   = 6000;  // safety stop for each direction
const uint32_t CAL_SETTLE_MS    = 150;   // let the pot settle before reading an end
const int      CAL_MIN_TRAVEL   = 10;    // moved less than this = it was already at that end
const int      CAL_MIN_RANGE    = 200;   // a smaller range means something is wrong: keep the old values
const int      CAL_END_MARGIN   = 8;     // stay this far inside the end stops

struct Knob {
  KnobState state = IDLE;
  KnobFault fault = FAULT_NONE;
  int       target = -1;                // ADC target, -1 = motor off
  int       topSpeed = 255;
  uint32_t  moveStart = 0, lastProgress = 0;
  int       progressRef = 0;
  uint32_t  kickUntil = 0;                 // full power until then (see KICK_MS)
  int       kickRef = 0;                   // position when the kick started
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
static int speedToDuty(uint8_t speed)   { return MIN_DRIVE + (255 - MIN_DRIVE) * speed / 127; }
static int nearTarget(uint8_t k)        { return NEAR_STEPS * potRange(k) / 127; }  // in ADC counts

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
  uint32_t now = millis();
  int pos = potPosition(k);
  n.target = constrain(target, settings.calLow[k], settings.calHigh[k]);
  n.topSpeed = topSpeed;
  n.state = why;
  n.moveStart = n.lastProgress = now;
  n.progressRef = pos;
  n.resting = false;

  // Standing still, or about to turn round: kick it. A move that just continues keeps
  // whatever kick is still running.
  int running = motorSpeed(k);
  bool turningRound = running != 0 && (running > 0) != (n.target > pos);
  if (running == 0 || turningRound) { n.kickUntil = now + KICK_MS; n.kickRef = pos; }
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

  int duty = constrain((int)(abs(distance) * KP), MIN_DRIVE, n.topSpeed);
  if ((int32_t)(n.kickUntil - now) > 0) {             // kick: full power until the knob moves
    if (abs(pos - n.kickRef) >= KICK_MOVED) n.kickUntil = now;
    else duty = KICK_DRIVE;
  }
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
    if (moved >= STALL_PROGRESS) { n.progressRef = pos; n.lastProgress = now; }
    else if (now - n.lastProgress > STALL_WINDOW_MS) {
      // Stuck a step or two short of the target: close enough. Not a hand, not a dead motor.
      if (abs(distance) <= nearTarget(k)) { stopMotor(k); moveFinished(k); n.state = IDLE; return; }
      enterUser(k, true);                             // held still, or dead motor
      return;
    }
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

  // Wiper mismatch stays faulted. The other faults get one more try after a while:
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

static void waitFor(uint32_t ms, void (*keepAlive)()) {
  uint32_t start = millis();
  while (millis() - start < ms) { keepAlive(); delay(1); }
}

// Where one drive towards an end stop ended up.
struct EndStop {
  int      reading;    // settled pot reading at the end
  uint32_t ms;         // how long the knob kept moving
  bool     timedOut;   // still moving after CAL_TIMEOUT_MS
};

// Drive one way until the knob stops moving (it has reached the end stop), the safety
// timeout hits, or the driver faults. Keeps the watchdog fed, then reads the settled end.
static EndStop findEnd(uint8_t k, int speed, void (*keepAlive)()) {
  EndStop end = { 0, 0, true };
  uint32_t start = millis(), lastMove = start;
  int ref = readPotSlow(k);
  while (millis() - start < CAL_TIMEOUT_MS) {
    setMotor(k, speed);
    updateMotors();
    keepAlive();
    if (driverFault()) { end.timedOut = false; break; }
    int pos = readPotSlow(k);
    if (abs(pos - ref) > CAL_STILL_COUNTS) { ref = pos; lastMove = millis(); }
    else if (millis() - lastMove > CAL_STILL_MS) { end.timedOut = false; break; }  // stopped: end stop
    delay(2);
  }
  setMotor(k, 0);
  end.ms = (end.timedOut ? millis() : lastMove) - start;
  waitFor(CAL_SETTLE_MS, keepAlive);
  end.reading = readPotSlow(k);
  return end;
}

void calibrateKnobs(void (*keepAlive)(), int which) {
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    if (which != ALL_KNOBS && which != k) continue;

    // 1. Which way does the motor turn the pot? Drive "up" until it stops.
    //    If it was already at that end, drive the other way instead.
    settings.reverse[k] = false;
    int start = readPotSlow(k);
    EndStop first = findEnd(k, CAL_SPEED, keepAlive);
    bool atHigh;                                       // which end the knob is at now
    if (abs(first.reading - start) >= CAL_MIN_TRAVEL) {
      settings.reverse[k] = first.reading < start;    // driving "up" lowered the reading
      atHigh = first.reading > start;
    } else {
      EndStop second = findEnd(k, -CAL_SPEED, keepAlive);
      settings.reverse[k] = second.reading > first.reading;   // driving "down" raised the reading
      atHigh = second.reading > first.reading;
    }

    // 2. Find both end stops, timing a full travel to each: first to the end it is not at.
    EndStop low, high;
    if (atHigh) { low = findEnd(k, -CAL_SPEED, keepAlive); high = findEnd(k, CAL_SPEED, keepAlive); }
    else        { high = findEnd(k, CAL_SPEED, keepAlive); low = findEnd(k, -CAL_SPEED, keepAlive); }

    bool ok = high.reading - low.reading > CAL_MIN_RANGE;
    if (ok) { settings.calLow[k] = low.reading + CAL_END_MARGIN; settings.calHigh[k] = high.reading - CAL_END_MARGIN; }
    Serial.printf("calibrate knob %d: low %d in %lu ms%s  high %d in %lu ms%s  reverse %d  %s\n", k + 1,
                  low.reading, (unsigned long)low.ms, low.timedOut ? " (timeout)" : "",
                  high.reading, (unsigned long)high.ms, high.timedOut ? " (timeout)" : "",
                  settings.reverse[k],
                  ok ? "ok" : driverFault() ? "driver fault, kept old values - check motor and supply"
                            : "range too small, kept old values - check wiring");
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
