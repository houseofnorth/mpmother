/*
  Motorpot 4ch  -  Teensy 3.2, 2x L293DD, 4x ALPS RK16812MG099
  Arduino IDE: Tools > Board "Teensy 3.2 / 3.1", Tools > USB Type "Serial + MIDI"

  What it does
  - Each pot sends MIDI CC (USB MIDI + DIN out on Serial1 / pin 1) when a person turns it.
  - Incoming CC on the same number moves the pot there (Ableton: enable Remote on the OUTPUT port).
  - Touch detection is firmware only: if a knob moves while the motor is off, stalls while
    the motor drives, or gets pushed the wrong way, that knob stops and the person has control.
  - Screensaver: after <idle> seconds without activity a knob starts drifting to random
    positions and sends CC. Touching a knob stops only that knob; it rejoins after <idle> again.
  - Safety: motors only run towards a target, stop on stall / 2.5 s timeout, ignore ADC glitches,
    and boot with all motors off.

  Pin map (matches Schematics/Motorpot_4ch.sch)
    CH   motor A/B (PWM)   wiper
    1    3 / 4              A0 (14)
    2    5 / 6              A1 (15)
    3    9 / 10             A2 (16)
    4    22 / 23            A3 (17)
    DIN MIDI TX = pin 1 (Serial1, 31250 baud)

  Config over USB serial (115200, newline):
    status                  show settings and live state
    idle <sec>              screensaver delay, 0 = off
    cc <ch 1-4> <0-127>     CC number per knob
    midich <1-15>           MIDI channel (16 is reserved for config)
    cal                     auto-calibrate ends + motor direction (knobs move!)
    send                    send all positions once
    defaults                factory settings
  Config over MIDI (channel 16):
    CC 1  = screensaver delay in 10 s steps (0 = off, 127 = 21 min)
  Settings are stored in EEPROM.
*/
#include <EEPROM.h>

const uint8_t NCH = 4;
const uint8_t PIN_A[NCH]   = {3, 5, 9, 22};
const uint8_t PIN_B[NCH]   = {4, 6, 10, 23};
const uint8_t PIN_POT[NCH] = {A0, A1, A2, A3};

// ---------- tuning ----------
const int      PWM_FREQ        = 5000;  // Hz, as NeonFade (L293 is slow, keep <= 5 kHz)
const int      MIN_DUTY        = 90;    // below this the gearbox does not turn
const int      MAX_DUTY        = 255;
const int      SAVER_DUTY      = 130;   // gentler top speed in screensaver
const float    KP              = 1.2f;  // duty per ADC count of error
const int      DEADBAND        = 6;     // ADC counts (10 bit)
const int      TOUCH_MOVE      = 14;    // unexpected movement that counts as a touch
const int      GLITCH          = 400;   // single-sample jump that is ignored as noise
const uint32_t SPINUP_MS       = 150;   // grace time before stall detection
const uint32_t STALL_MS        = 300;   // no progress this long while driving = held
const uint32_t MOVE_TIMEOUT_MS = 2500;  // hard stop for any single move
const uint32_t SETTLE_MS       = 80;    // let the pot settle after the motor stops
const uint32_t USER_RELEASE_MS = 1500;  // no movement this long = person let go
const uint32_t ECHO_MS         = 300;   // ignore Live echoing our own CC back
const uint32_t SAVER_WAIT_MIN  = 1000, SAVER_WAIT_MAX = 5000;

// ---------- config in EEPROM ----------
const uint32_t MAGIC = 0x4D503432;  // "MP42"
struct Config {
  uint32_t magic;
  uint16_t idleSec;         // 0 = screensaver off
  uint8_t  midiCh;          // 1..16
  uint8_t  cc[NCH];
  int16_t  calLo[NCH], calHi[NCH];
  uint8_t  invert[NCH];
} cfg;

void setDefaults() {
  cfg.magic = MAGIC; cfg.idleSec = 300; cfg.midiCh = 1;
  for (uint8_t i = 0; i < NCH; i++) { cfg.cc[i] = 20 + i; cfg.calLo[i] = 15; cfg.calHi[i] = 1008; cfg.invert[i] = 0; }
}
void saveConfig() { EEPROM.put(0, cfg); }
void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC || cfg.midiCh < 1 || cfg.midiCh > 15) { setDefaults(); saveConfig(); }
}

// ---------- channel state ----------
enum State : uint8_t { IDLE, FOLLOW, USER, SAVER };
const char *stateName[] = {"idle", "follow", "user", "saver"};

struct Chan {
  State    st = IDLE;
  float    pos = 0;           // smoothed ADC
  int      glitchCount = 0;
  int      target = -1;       // ADC target, -1 = motor off
  int      duty = 0;          // signed, before invert
  int      maxDuty = MAX_DUTY;
  uint32_t driveStart = 0, lastProgress = 0;
  int      progressRef = 0;
  bool     resting = false;   // restPos valid
  int      restPos = 0;
  uint32_t stopAt = 0;
  uint32_t lastActivity = 0;  // for screensaver
  uint32_t lastUserMove = 0;
  int      userRef = 0;
  int      lastCC = -1;
  uint32_t lastSentAt = 0;
  uint32_t saverWaitUntil = 0;
} ch[NCH];

// ---------- low level ----------
void setMotor(uint8_t i, int d) {
  d = constrain(d, -255, 255);
  ch[i].duty = d;
  if (cfg.invert[i]) d = -d;
  if (d > 0)      { analogWrite(PIN_A[i], d);  analogWrite(PIN_B[i], 0); }
  else if (d < 0) { analogWrite(PIN_A[i], 0);  analogWrite(PIN_B[i], -d); }
  else            { analogWrite(PIN_A[i], 0);  analogWrite(PIN_B[i], 0); }
}

int readAvg(uint8_t i, int n = 32) {
  long s = 0;
  for (int k = 0; k < n; k++) s += analogRead(PIN_POT[i]);
  return s / n;
}

void updatePos(uint8_t i) {
  int raw = analogRead(PIN_POT[i]);
  Chan &c = ch[i];
  if (abs(raw - (int)c.pos) > GLITCH && ++c.glitchCount < 5) return;  // ignore single spikes
  c.glitchCount = 0;
  c.pos = c.pos * 0.7f + raw * 0.3f;
}

int range(uint8_t i) { return max(1, cfg.calHi[i] - cfg.calLo[i]); }
int ccToPos(uint8_t i, int v) { return cfg.calLo[i] + (long)v * range(i) / 127; }

// ---------- MIDI out ----------
void sendCC(uint8_t i, int v) {
  usbMIDI.sendControlChange(cfg.cc[i], v, cfg.midiCh);
  Serial1.write(0xB0 | ((cfg.midiCh - 1) & 0x0F));
  Serial1.write(cfg.cc[i] & 0x7F);
  Serial1.write(v & 0x7F);
  ch[i].lastCC = v;
  ch[i].lastSentAt = millis();
}

// send only when the knob has clearly moved into a new CC step (hysteresis 0.75 step)
void updateCC(uint8_t i) {
  Chan &c = ch[i];
  float exact = (c.pos - cfg.calLo[i]) * 127.0f / range(i);
  exact = constrain(exact, 0.0f, 127.0f);
  if (c.lastCC < 0 || fabsf(exact - c.lastCC) > 0.75f) {
    int v = constrain((int)(exact + 0.5f), 0, 127);
    if (v != c.lastCC) sendCC(i, v);
  }
}

// ---------- motion ----------
void stopMotor(uint8_t i) {
  setMotor(i, 0);
  ch[i].target = -1;
  ch[i].stopAt = millis();
  ch[i].resting = false;
}

void startMove(uint8_t i, int target, int maxDuty, uint8_t st) {
  Chan &c = ch[i];
  uint32_t now = millis();
  c.target = constrain(target, cfg.calLo[i], cfg.calHi[i]);
  c.maxDuty = maxDuty;
  c.st = (State)st;
  c.driveStart = c.lastProgress = now;
  c.progressRef = (int)c.pos;
  c.resting = false;
}

void enterUser(uint8_t i) {
  stopMotor(i);
  Chan &c = ch[i];
  c.st = USER;
  c.lastUserMove = c.lastActivity = millis();
  c.userRef = (int)c.pos;
}

void tickChannel(uint8_t i, uint32_t now) {
  Chan &c = ch[i];
  int p = (int)c.pos;

  if (c.st == USER) {
    updateCC(i);
    if (abs(p - c.userRef) > 3) { c.userRef = p; c.lastUserMove = c.lastActivity = now; }
    if (now - c.lastUserMove > USER_RELEASE_MS) { c.st = IDLE; c.restPos = p; c.resting = true; }
    return;
  }

  if (c.target >= 0) {                       // motor is driving (FOLLOW or SAVER)
    int err = c.target - p;
    if (abs(err) <= DEADBAND) {
      stopMotor(i);
      if (c.st == SAVER) { updateCC(i); c.saverWaitUntil = now + random(SAVER_WAIT_MIN, SAVER_WAIT_MAX); }
      else c.st = IDLE;
      return;
    }
    int mag = constrain((int)(abs(err) * KP), MIN_DUTY, c.maxDuty);
    int d = err > 0 ? mag : -mag;
    setMotor(i, d);
    if (c.st == SAVER) updateCC(i);

    // touch detection while driving
    if (now - c.driveStart > SPINUP_MS) {
      int dir = d > 0 ? 1 : -1;
      int moved = (p - c.progressRef) * dir;
      if (moved < -TOUCH_MOVE) { enterUser(i); return; }            // pushed the other way
      if (moved >= 3) { c.progressRef = p; c.lastProgress = now; }
      else if (now - c.lastProgress > STALL_MS) { enterUser(i); return; }  // held
    }
    if (now - c.driveStart > MOVE_TIMEOUT_MS) {                      // safety stop
      stopMotor(i);
      if (c.st == SAVER) c.saverWaitUntil = now + SAVER_WAIT_MAX; else c.st = IDLE;
    }
    return;
  }

  // motor off (IDLE or SAVER waiting)
  if (!c.resting) {
    if (now - c.stopAt > SETTLE_MS) { c.restPos = p; c.resting = true; }
    return;
  }
  if (abs(p - c.restPos) > TOUCH_MOVE) { enterUser(i); return; }  // turned by hand

  if (c.st == SAVER && (int32_t)(now - c.saverWaitUntil) >= 0)
    startMove(i, random(cfg.calLo[i], cfg.calHi[i] + 1), SAVER_DUTY, SAVER);

  if (c.st == IDLE && cfg.idleSec > 0 && now - c.lastActivity > (uint32_t)cfg.idleSec * 1000UL) {
    c.st = SAVER;
    c.saverWaitUntil = now;
  }
}

// ---------- MIDI in ----------
void onControlChange(byte channel, byte control, byte value) {
  uint32_t now = millis();
  if (channel == 16) {                       // config channel
    if (control == 1) { cfg.idleSec = value * 10; saveConfig(); }
    return;
  }
  if (channel != cfg.midiCh) return;
  for (uint8_t i = 0; i < NCH; i++) {
    if (control != cfg.cc[i]) continue;
    Chan &c = ch[i];
    if (c.st == USER) return;                                      // person has priority
    if (now - c.lastSentAt < ECHO_MS && abs((int)value - c.lastCC) <= 1) return;  // our own echo
    c.lastActivity = now;
    c.lastCC = value;
    int t = ccToPos(i, value);
    if (abs(t - (int)c.pos) <= DEADBAND) { if (c.target >= 0) stopMotor(i); c.st = IDLE; }
    else startMove(i, t, MAX_DUTY, FOLLOW);
  }
}

// ---------- calibration ----------
void calibrate() {
  Serial.println("cal: knobs will move, hands off");
  for (uint8_t i = 0; i < NCH; i++) {
    cfg.invert[i] = 0;
    int p0 = readAvg(i);
    setMotor(i, 200); delay(400); setMotor(i, 0); delay(100);
    int p1 = readAvg(i);
    if (abs(p1 - p0) < 10) {                 // maybe sitting at the end stop, try the other way
      setMotor(i, -200); delay(400); setMotor(i, 0); delay(100);
      int p2 = readAvg(i);
      cfg.invert[i] = (p2 > p1) ? 1 : 0;
    } else {
      cfg.invert[i] = (p1 < p0) ? 1 : 0;
    }
    setMotor(i, -220); delay(1500); setMotor(i, 0); delay(150);
    int lo = readAvg(i);
    setMotor(i, 220); delay(1500); setMotor(i, 0); delay(150);
    int hi = readAvg(i);
    if (hi - lo > 200) { cfg.calLo[i] = lo + 8; cfg.calHi[i] = hi - 8; }
    Serial.printf("cal CH%d: lo %d hi %d invert %d%s\n", i + 1, lo, hi, cfg.invert[i],
                  (hi - lo > 200) ? "" : "  (range too small, kept old values - check wiring)");
    ch[i].pos = readAvg(i);
    stopMotor(i);
    ch[i].st = IDLE;
    ch[i].lastActivity = millis();
  }
  saveConfig();
}

// ---------- serial commands ----------
void printStatus() {
  Serial.printf("Motorpot 4ch  midi ch %d  screensaver %s", cfg.midiCh, cfg.idleSec ? "" : "off");
  if (cfg.idleSec) Serial.printf("after %d s", cfg.idleSec);
  Serial.println();
  for (uint8_t i = 0; i < NCH; i++)
    Serial.printf("CH%d  cc %3d  pos %4d  cal %4d-%4d  inv %d  %s\n", i + 1, cfg.cc[i], (int)ch[i].pos,
                  cfg.calLo[i], cfg.calHi[i], cfg.invert[i], stateName[ch[i].st]);
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " \t\r");
  if (!cmd) return;
  char *a1 = strtok(nullptr, " \t\r"), *a2 = strtok(nullptr, " \t\r");
  if (!strcmp(cmd, "status")) printStatus();
  else if (!strcmp(cmd, "idle") && a1) { cfg.idleSec = constrain(atoi(a1), 0, 65000); saveConfig(); printStatus(); }
  else if (!strcmp(cmd, "cc") && a1 && a2) {
    int k = atoi(a1) - 1;
    if (k >= 0 && k < NCH) { cfg.cc[k] = constrain(atoi(a2), 0, 127); saveConfig(); }
    printStatus();
  }
  else if (!strcmp(cmd, "midich") && a1) { cfg.midiCh = constrain(atoi(a1), 1, 15); saveConfig(); printStatus(); }
  else if (!strcmp(cmd, "cal")) { calibrate(); printStatus(); }
  else if (!strcmp(cmd, "send")) { for (uint8_t i = 0; i < NCH; i++) { ch[i].lastCC = -1; updateCC(i); } }
  else if (!strcmp(cmd, "defaults")) { setDefaults(); saveConfig(); printStatus(); }
  else Serial.println("commands: status | idle <sec> | cc <1-4> <0-127> | midich <1-15> | cal | send | defaults");
}

void pollSerial() {
  static char buf[48];
  static uint8_t n = 0;
  while (Serial.available()) {
    char k = Serial.read();
    if (k == '\n') { buf[n] = 0; handleCommand(buf); n = 0; }
    else if (n < sizeof(buf) - 1) buf[n++] = k;
  }
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);
  Serial1.begin(31250);
  loadConfig();
  analogReadResolution(10);
  analogReadAveraging(8);
  analogWriteResolution(8);
  for (uint8_t i = 0; i < NCH; i++) {
    pinMode(PIN_A[i], OUTPUT); pinMode(PIN_B[i], OUTPUT);
    analogWriteFrequency(PIN_A[i], PWM_FREQ);
    analogWriteFrequency(PIN_B[i], PWM_FREQ);
    setMotor(i, 0);                          // boot safe: all motors off
  }
  uint32_t seed = 0;
  for (uint8_t i = 0; i < NCH; i++) {
    ch[i].pos = readAvg(i);
    seed = (seed << 3) ^ analogRead(PIN_POT[i]) ^ micros();
    ch[i].lastActivity = millis();
    ch[i].stopAt = millis();
  }
  randomSeed(seed);
  usbMIDI.setHandleControlChange(onControlChange);
  delay(500);
  for (uint8_t i = 0; i < NCH; i++) updateCC(i);   // tell Live where the knobs are
}

void loop() {
  usbMIDI.read();
  pollSerial();
  static elapsedMicros tick;
  if (tick >= 1000) {                        // 1 kHz control loop
    tick = 0;
    uint32_t now = millis();
    for (uint8_t i = 0; i < NCH; i++) { updatePos(i); tickChannel(i, now); }
  }
}
