#include "midi_io.h"
#include "settings.h"
#include "knobs.h"
#include "pots.h"
#include "motors.h"

const uint8_t FIRMWARE_MAJOR = 1, FIRMWARE_MINOR = 0;

// ---- SysEx framing: F0 7D 4D 50 <command> <data...> F7 ----
// 7D = "non-commercial" manufacturer ID, 4D 50 = "MP"
const uint8_t SYSEX_HEADER[] = { 0xF0, 0x7D, 0x4D, 0x50 };

enum ToolCommand : uint8_t {   // tool -> controller
  CMD_HELLO          = 0x01,
  CMD_GET_SETTINGS   = 0x02,
  CMD_SET_SETTINGS   = 0x03,
  CMD_CALIBRATE      = 0x04,
  CMD_FACTORY_RESET  = 0x05,
  CMD_CLEAR_FAULTS   = 0x06,
  CMD_MONITOR        = 0x07,   // keep-alive: status streams for 5 s after each one
};
enum ControllerReply : uint8_t {  // controller -> tool
  REPLY_HELLO    = 0x41,
  REPLY_SETTINGS = 0x42,
  REPLY_STATUS   = 0x43,
};

const uint32_t MONITOR_INTERVAL_MS = 50;    // 20 status updates per second
const uint32_t MONITOR_TIMEOUT_MS  = 5000;

static uint32_t lastIn = 0, lastOut = 0;
static uint32_t monitorUntil = 0, lastStatusSent = 0;
static int      wantCalibration = NO_CALIBRATION;

// ---------- helpers ----------
static uint8_t hi7(int v) { return (v >> 7) & 0x7F; }
static uint8_t lo7(int v) { return v & 0x7F; }

static void sendSysex(uint8_t reply, const uint8_t *data, uint8_t length) {
  uint8_t msg[64];
  uint8_t n = 0;
  for (uint8_t b : SYSEX_HEADER) msg[n++] = b;
  msg[n++] = reply;
  for (uint8_t i = 0; i < length; i++) msg[n++] = data[i] & 0x7F;
  msg[n++] = 0xF7;
  usbMIDI.sendSysEx(n, msg, true);
}

// ---------- CC ----------
void sendKnobCC(uint8_t k, uint8_t value) {
  usbMIDI.sendControlChange(settings.outCC[k], value, settings.outChannel);
  if (settings.dinOut) {
    Serial1.write(0xB0 | (settings.outChannel - 1));
    Serial1.write(settings.outCC[k] & 0x7F);
    Serial1.write(value & 0x7F);
  }
  lastOut = millis();
}

static void onControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  lastIn = millis();
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    bool isFeedback = channel == settings.outChannel && control == settings.outCC[k];
    bool isDrive    = channel == settings.driveChannel && control == settings.driveCC[k];
    if (isFeedback && settings.followFeedback) moveKnobFromMidi(k, value, true);  // may be our own echo
    else if (isDrive && !isFeedback)           moveKnobFromMidi(k, value, false);
  }
}

// ---------- SysEx: settings ----------
const uint8_t SETTINGS_BYTES = 12 + 5 * NUM_KNOBS;

void sendSettingsToTool() {
  uint8_t d[SETTINGS_BYTES];
  d[0] = settings.outChannel;
  d[1] = settings.driveChannel;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) { d[2 + k] = settings.outCC[k]; d[6 + k] = settings.driveCC[k]; }
  d[10] = (settings.followFeedback ? 1 : 0) | (settings.dinOut ? 2 : 0);
  d[11] = settings.maxSpeed;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {  // calibration, read-only for the tool
    uint8_t *c = &d[12 + 5 * k];
    c[0] = hi7(settings.calLow[k]);  c[1] = lo7(settings.calLow[k]);
    c[2] = hi7(settings.calHigh[k]); c[3] = lo7(settings.calHigh[k]);
    c[4] = settings.reverse[k];
  }
  sendSysex(REPLY_SETTINGS, d, SETTINGS_BYTES);
}

static void applySettingsFromTool(const uint8_t *d, uint8_t length) {
  if (length < 12) return;
  settings.outChannel   = constrain(d[0], 1, 16);
  settings.driveChannel = constrain(d[1], 0, 16);
  for (uint8_t k = 0; k < NUM_KNOBS; k++) { settings.outCC[k] = d[2 + k]; settings.driveCC[k] = d[6 + k]; }
  settings.followFeedback = d[10] & 1;
  settings.dinOut         = d[10] & 2;
  settings.maxSpeed       = d[11];
  saveSettings();  // calibration is only changed by calibrating
}

// ---------- SysEx: live status ----------
const uint8_t STATUS_BYTES = 2 + 10 * NUM_KNOBS;

static void sendStatus() {
  uint8_t d[STATUS_BYTES];
  d[0] = (driversAwake() ? 1 : 0) | (driverFault() ? 2 : 0) | (driverFaultLatched() ? 4 : 0);
  d[1] = (midiInRecently(MONITOR_INTERVAL_MS) ? 1 : 0) | (midiOutRecently(MONITOR_INTERVAL_MS) ? 2 : 0);
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    uint8_t *c = &d[2 + 10 * k];
    int cc = knobLastCC(k), target = knobTarget(k);
    c[0] = knobState(k);
    c[1] = knobFault(k);
    c[2] = (potWorn(k) ? 1 : 0) | (cc >= 0 ? 2 : 0) | (motorSpeed(k) != 0 ? 4 : 0);
    c[3] = cc >= 0 ? cc : 0;
    c[4] = hi7(wiperA(k)); c[5] = lo7(wiperA(k));
    c[6] = hi7(wiperB(k)); c[7] = lo7(wiperB(k));
    if (target < 0) target = 16383;  // "no target"
    c[8] = hi7(target);    c[9] = lo7(target);
  }
  sendSysex(REPLY_STATUS, d, STATUS_BYTES);
}

static void onSysex(uint8_t *msg, unsigned length) {
  // Check it is for us: F0 7D 4D 50 <cmd> ... F7
  if (length < 6) return;
  for (uint8_t i = 0; i < sizeof(SYSEX_HEADER); i++) if (msg[i] != SYSEX_HEADER[i]) return;
  uint8_t command = msg[4];
  const uint8_t *data = &msg[5];
  uint8_t dataLength = length - 6;  // minus header, command and F7

  switch (command) {
    case CMD_HELLO: {
      uint8_t d[] = { FIRMWARE_MAJOR, FIRMWARE_MINOR, NUM_KNOBS };
      sendSysex(REPLY_HELLO, d, sizeof(d));
      break;
    }
    case CMD_GET_SETTINGS:  sendSettingsToTool(); break;
    case CMD_SET_SETTINGS:  applySettingsFromTool(data, dataLength); sendSettingsToTool(); break;
    case CMD_CALIBRATE:     // data: knob 0-3, or nothing / 127 = all. Runs from loop(), not in here
      wantCalibration = (dataLength > 0 && data[0] < NUM_KNOBS) ? data[0] : ALL_KNOBS;
      break;
    case CMD_FACTORY_RESET: factoryDefaults(); saveSettings(); sendSettingsToTool(); break;
    case CMD_CLEAR_FAULTS:  clearKnobFaults(); clearDriverFault(); sendStatus(); break;
    case CMD_MONITOR:       monitorUntil = millis() + MONITOR_TIMEOUT_MS; break;
  }
}

// ---------- public ----------
void setupMidi() {
  Serial1.begin(31250);  // DIN MIDI out on pin 1
  usbMIDI.setHandleControlChange(onControlChange);
  usbMIDI.setHandleSystemExclusive(onSysex);
}

void updateMidi() {
  while (usbMIDI.read()) {}
  uint32_t now = millis();
  if ((int32_t)(monitorUntil - now) > 0 && now - lastStatusSent >= MONITOR_INTERVAL_MS) {
    lastStatusSent = now;
    sendStatus();
  }
}

bool midiInRecently(uint32_t ms)  { return lastIn  && millis() - lastIn  < ms; }
bool midiOutRecently(uint32_t ms) { return lastOut && millis() - lastOut < ms; }

int calibrationRequested() {
  int which = wantCalibration;
  wantCalibration = NO_CALIBRATION;
  return which;
}
