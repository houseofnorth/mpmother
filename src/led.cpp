#include "led.h"
#include "knobs.h"
#include "pots.h"
#include "motors.h"
#include "midi_io.h"

const uint8_t BRIGHT = 255, DIM = 40;  // tune to taste behind the faceplate

static uint32_t bootAt = 0;
static bool     showWatchdogBlink = false;

static void setColor(uint8_t r, uint8_t g, uint8_t b) {
  if (LED_COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  analogWrite(PIN_LED_RED, r);
  analogWrite(PIN_LED_GREEN, g);
  analogWrite(PIN_LED_BLUE, b);
}

static bool blink(uint32_t periodMs) { return (millis() % periodMs) < periodMs / 2; }

// 0..255..0 over periodMs, for slow "breathing"
static uint8_t breathe(uint32_t periodMs) {
  uint32_t t = millis() % periodMs;
  uint32_t half = periodMs / 2;
  return t < half ? t * 255 / half : (periodMs - t) * 255 / half;
}

void setupLed(bool afterWatchdogReset) {
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  bootAt = millis();
  showWatchdogBlink = afterWatchdogReset;
  setColor(BRIGHT, BRIGHT, BRIGHT);
}

void updateLed() {
  uint32_t sinceBoot = millis() - bootAt;
  if (showWatchdogBlink && sinceBoot < 1500) {
    bool on = (sinceBoot / 250) % 2 == 0;
    setColor(on ? BRIGHT : 0, on ? BRIGHT : 0, on ? BRIGHT : 0);
    return;
  }
  if (sinceBoot < 300) { setColor(BRIGHT, BRIGHT, BRIGHT); return; }

  bool anyFault = false, anyWorn = false;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    if (knobState(k) == FAULT) anyFault = true;
    if (potWorn(k)) anyWorn = true;
  }

  if (driverFaultLatched())                     setColor(BRIGHT, 0, 0);
  else if (driverFault() || anyFault)           setColor(blink(500) ? BRIGHT : 0, 0, 0);
  else if (anyWorn && blink(1500))              setColor(BRIGHT, BRIGHT / 3, 0);
  else if (midiInRecently(30) || midiOutRecently(30)) setColor(0, 0, BRIGHT);
  else if (anyKnobInSaver()) {
    uint8_t level = breathe(4000) / 3;
    setColor(level, 0, level);
  }
  else                                          setColor(0, DIM, 0);
}
