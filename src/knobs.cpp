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
const int      HAND_MOVED       = 3;     // movement that means the hand is still turning (touch mode)
const int      PASSIVE_TOUCH    = 6;     // motors off: movement that shows the knob as touched
const uint32_t SPINUP_MS        = 300;   // grace time before stall detection (slow motors start slowly)
const uint32_t STALL_WINDOW_MS  = 600;   // the knob must gain STALL_PROGRESS towards the target within
const int      STALL_PROGRESS   = 4;     // this long, in ADC counts - otherwise it is held (or the motor is dead)
const uint32_t MOVE_TIMEOUT_MS  = 6000;  // hard stop when a target has not been reached after this long
const uint32_t KICK_MS          = 60;    // a motor starting from standstill (or turning round) gets
const int      KICK_DRIVE       = 255;   // this PWM for up to KICK_MS, until the knob has moved
const int      KICK_MOVED       = 2;     // this many ADC counts: breaks the gearbox free on small, slow moves
const int      NEAR_STEPS       = 2;     // stuck within this many CC steps of the target = arrived, not a stall
                                         // (slow automation sends one-step moves the motor may not finish)
const uint32_t SETTLE_MS        = 80;    // let the pot settle after the motor stops
const uint32_t USER_RELEASE_MS  = 1500;  // no movement this long = the hand let go
const uint32_t ECHO_MS          = 300;   // mapping feedback this soon after the knob sent = Live's echo
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

// Why a knob is in USER (touch) mode. Decided when the hand lets go: if nothing moved after
// the motor gave up, it was probably the motor, not a hand.
enum UserCause : uint8_t { BY_HAND, STALLED, PUSHED_BACK };

struct Knob {
  KnobState state = IDLE;
  KnobFault fault = FAULT_NONE;
  int       target = -1;                // ADC target, -1 = motor off
  int       topSpeed = 255;
  uint32_t  moveStart = 0, lastProgress = 0;
  uint32_t  targetAt = 0;               // when the latest target came in
  int       progressRef = 0;
  uint32_t  kickUntil = 0;              // full power until then (see KICK_MS)
  int       kickRef = 0;                // position when the kick started
  bool      fromMidiIn = false;         // this move came from MIDI In: the knob sends where it is
  uint8_t   midiInValue = 0;            // ... and ends on exactly this value
  bool      resting = false;            // restPos is valid
  int       restPos = 0;
  uint32_t  stoppedAt = 0;
  uint32_t  lastHandMove = 0;
  int       handRef = 0;
  bool      handMoved = false;          // did the pot move at all during this USER period?
  UserCause cause = BY_HAND;
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

static void sendValue(uint8_t k, int value) {
  sendKnobCC(k, value);
  knobs[k].lastCC = value;
  knobs[k].lastSentAt = millis();
}

// Send CC only when the knob has clearly moved into a new step (0.75 step hysteresis).
static void reportPosition(uint8_t k) {
  Knob &n = knobs[k];
  float exact = (potPosition(k) - settings.calLow[k]) * 127.0f / potRange(k);
  exact = constrain(exact, 0.0f, 127.0f);
  if (n.lastCC < 0 || fabsf(exact - n.lastCC) > 0.75f) {
    int value = constrain((int)(exact + 0.5f), 0, 127);
    if (value != n.lastCC) sendValue(k, value);
  }
}

static void stopMotor(uint8_t k) {
  setMotor(k, 0);
  knobs[k].target = -1;
  knobs[k].stoppedAt = millis();
  knobs[k].resting = false;
}

// Head for a target. A target in the direction the motor is already turning continues the same
// move: the spin-up and stall timers keep running, so a stream of MIDI values (LFO, automation)
// cannot keep hand detection switched off. Standing still or turning round = a new move, with a
// kick to break the gearbox free.
static void startMove(uint8_t k, int target, int topSpeed) {
  Knob &n = knobs[k];
  uint32_t now = millis();
  int pos = potPosition(k);
  int running = motorSpeed(k);
  n.target = constrain(target, settings.calLow[k], settings.calHigh[k]);
  n.topSpeed = topSpeed;
  n.state = FOLLOW;
  n.resting = false;
  n.targetAt = now;

  bool continuing = running != 0 && (running > 0) == (n.target > pos);
  if (continuing) return;
  n.moveStart = n.lastProgress = now;
  n.progressRef = pos;
  n.kickUntil = now + KICK_MS;
  n.kickRef = pos;
}

// The knob got where it was sent. Moved by MIDI In, it ends on exactly the value it was given.
static void arrived(uint8_t k) {
  Knob &n = knobs[k];
  stopMotor(k);
  n.state = IDLE;
  n.wrongWayStrikes = n.notMovingStrikes = 0;
  if (n.fromMidiIn && n.lastCC != n.midiInValue) sendValue(k, n.midiInValue);
}

static void enterFault(uint8_t k, KnobFault why) {
  stopMotor(k);
  knobs[k].state = FAULT;
  knobs[k].fault = why;
  knobs[k].faultAt = millis();
}

static void enterUser(uint8_t k, UserCause cause) {
  stopMotor(k);
  Knob &n = knobs[k];
  n.state = USER;
  n.lastHandMove = millis();
  n.handRef = potPosition(k);
  n.handMoved = false;
  n.cause = cause;
}

// ---------- per-state behaviour ----------
static void whileUser(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  int pos = potPosition(k);
  reportPosition(k);
  if (abs(pos - n.handRef) > HAND_MOVED) {
    n.handRef = pos;
    n.lastHandMove = now;
    if (now - n.stoppedAt > SETTLE_MS) n.handMoved = true;   // the motor running down is not a hand
  }
  if (now - n.lastHandMove > USER_RELEASE_MS) {
    // The motor gave up and then nothing moved: probably not a hand but the motor.
    // Stalled = dead motor or cable. Pushed back = motor turning the wrong way.
    if (!n.handMoved) {
      if (n.cause == STALLED     && ++n.notMovingStrikes >= STRIKES_TO_FAULT) { enterFault(k, FAULT_NOT_MOVING); return; }
      if (n.cause == PUSHED_BACK && ++n.wrongWayStrikes  >= STRIKES_TO_FAULT) { enterFault(k, FAULT_DIRECTION);  return; }
    }
    n.state = IDLE;
    n.restPos = pos;
    n.resting = true;
  }
}

static void whileDriving(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  // The driver holds the motors off (fault): drop the move. Not a hand, not a stalled motor.
  if (driverFault()) { stopMotor(k); n.state = IDLE; return; }

  int pos = potPosition(k);
  int distance = n.target - pos;
  if (n.fromMidiIn) reportPosition(k);  // moved by MIDI In: send where it is, so a mapped parameter follows

  if (abs(distance) <= DEADBAND) { arrived(k); return; }

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
    if (moved < -TOUCH_MOVE) { enterUser(k, PUSHED_BACK); return; }   // a hand, or a reversed motor
    if (moved >= STALL_PROGRESS) { n.progressRef = pos; n.lastProgress = now; }
    else if (now - n.lastProgress > STALL_WINDOW_MS) {
      // Stuck a step or two short of the target: close enough. Not a hand, not a dead motor.
      if (abs(distance) <= nearTarget(k)) { arrived(k); return; }
      enterUser(k, STALLED);                          // held still, or dead motor
      return;
    }
  }

  if (now - n.targetAt > MOVE_TIMEOUT_MS) {          // safety stop
    stopMotor(k);
    n.state = IDLE;
  }
}

static void whileResting(uint8_t k, uint32_t now, int touchMove) {
  Knob &n = knobs[k];
  int pos = potPosition(k);

  if (!n.resting) {  // wait for the pot to settle after the motor stopped
    if (now - n.stoppedAt > SETTLE_MS) { n.restPos = pos; n.resting = true; }
    return;
  }
  if (abs(pos - n.restPos) > touchMove) enterUser(k, BY_HAND);  // turned by hand
}

static void whileFault(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  setMotor(k, 0);
  reportPosition(k);  // still usable as a plain knob

  // Wiper mismatch stays faulted. The other faults get one more try after a while:
  // one more bad move and the fault is back.
  if (n.fault != FAULT_WIPERS && now - n.faultAt > FAULT_RETRY_MS) {
    n.state = IDLE;
    n.fault = FAULT_NONE;
    n.wrongWayStrikes = n.notMovingStrikes = STRIKES_TO_FAULT - 1;
    n.restPos = potPosition(k);
    n.resting = true;
  }
}

// Motors off: a plain controller. Every movement is sent, MIDI In is ignored,
// and faults that come from the motor mean nothing.
static void whilePassive(uint8_t k, uint32_t now) {
  Knob &n = knobs[k];
  n.wrongWayStrikes = n.notMovingStrikes = 0;
  n.cause = BY_HAND;
  if (n.state == FOLLOW) { stopMotor(k); n.state = IDLE; }
  if (n.state == FAULT && n.fault != FAULT_WIPERS) { n.state = IDLE; n.fault = FAULT_NONE; }

  if (n.state == FAULT)     whileFault(k, now);
  else if (n.state == USER) whileUser(k, now);
  else                      { reportPosition(k); whileResting(k, now, PASSIVE_TOUCH); }
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

    if (!settings.motorsOn)     whilePassive(k, now);
    else if (n.state == FAULT)  whileFault(k, now);
    else if (n.state == USER)   whileUser(k, now);
    else if (n.target >= 0)     whileDriving(k, now);
    else                        whileResting(k, now, TOUCH_MOVE);
  }
}

// A value from Ableton.
//   MIDI In: the knob moves there and sends its position on the way, so whatever is mapped
//            to it follows, and it ends on exactly the value it was given.
//   Mapping feedback (its own Out ch + CC): the knob only moves. Sending would fight Live's
//            own value (and switch off Live's automation).
void moveKnobFromMidi(uint8_t k, uint8_t value, bool isFeedback) {
  Knob &n = knobs[k];
  uint32_t now = millis();
  if (!settings.motorsOn || driverFault()) return;   // passive, or the driver holds the motors off
  if (n.state == USER || n.state == FAULT) return;   // a hand always wins
  if (isFeedback) {
    if (now - n.lastSentAt < ECHO_MS) return;         // Live echoing what the knob just sent
    if (n.target >= 0 && n.fromMidiIn) return;        // MIDI In is moving it: MIDI In wins
  }

  n.fromMidiIn = !isFeedback;
  if (n.fromMidiIn) n.midiInValue = value;
  else              n.lastCC = value;                 // Live already has this value

  int target = ccToPosition(k, value);
  if (abs(target - potPosition(k)) <= DEADBAND) {     // already there
    if (n.target >= 0) stopMotor(k);
    n.state = IDLE;
    if (n.fromMidiIn && n.lastCC != value) sendValue(k, value);
  } else {
    startMove(k, target, speedToDuty(settings.maxSpeed));
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
  if (!settings.motorsOn) { Serial.println("calibrate: motors are off, nothing done"); return; }

  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    if (which != ALL_KNOBS && which != k) continue;

    // 1. Which way does the motor turn the pot? Drive "up" until it stops.
    //    If it was already at that end, drive the other way instead.
    bool oldReverse = settings.reverse[k];
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

    // Only a good result changes anything: a failed run keeps the old range and polarity.
    bool ok = high.reading - low.reading > CAL_MIN_RANGE;
    if (ok) { settings.calLow[k] = low.reading + CAL_END_MARGIN; settings.calHigh[k] = high.reading - CAL_END_MARGIN; }
    else    settings.reverse[k] = oldReverse;
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
