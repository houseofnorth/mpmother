#include "pots.h"

// ---- tuning ----
const int      GLITCH_JUMP     = 400;   // a single jump bigger than this is treated as noise
const int      BROKEN_MISMATCH = 150;   // gangs differ by more than this -> something is wrong
const uint32_t BROKEN_AFTER_MS = 500;   // ...for at least this long
const float    SMOOTHING       = 0.3f;  // 0..1, higher = faster but noisier

struct Pot {
  float    a = 0, b = 0;        // smoothed gang readings
  uint8_t  glitchesA = 0, glitchesB = 0;
  uint32_t brokenSince = 0;
  bool     broken = false;
};
static Pot pots[NUM_KNOBS];

// Smooth one gang reading, ignoring short spikes.
static void filterGang(float &value, uint8_t &glitches, int raw) {
  if (abs(raw - (int)value) > GLITCH_JUMP && ++glitches < 5) return;
  glitches = 0;
  value += (raw - value) * SMOOTHING;
}

void setupPots() {
  analogReadResolution(10);
  analogReadAveraging(4);
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    pots[k].a = analogRead(PIN_WIPER_A[k]);
    pots[k].b = READ_GANG_B ? analogRead(PIN_WIPER_B[k]) : pots[k].a;
  }
}

void readPots() {
  uint32_t now = millis();
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    Pot &p = pots[k];
    filterGang(p.a, p.glitchesA, analogRead(PIN_WIPER_A[k]));

    // Gang A only: B copies A, so nothing to compare.
    if (!READ_GANG_B) { p.b = p.a; continue; }

    filterGang(p.b, p.glitchesB, analogRead(PIN_WIPER_B[k]));

    // Broken: stays set until reboot or "clear faults".
    if (wiperMismatch(k) > BROKEN_MISMATCH) {
      if (p.brokenSince == 0) p.brokenSince = now;
      if (now - p.brokenSince > BROKEN_AFTER_MS) p.broken = true;
    } else {
      p.brokenSince = 0;
    }
  }
}

int  potPosition(uint8_t k)   { return (int)((pots[k].a + pots[k].b) * 0.5f + 0.5f); }
int  wiperA(uint8_t k)        { return (int)pots[k].a; }
int  wiperB(uint8_t k)        { return (int)pots[k].b; }
int  wiperMismatch(uint8_t k) { return abs((int)pots[k].a - (int)pots[k].b); }
bool potBroken(uint8_t k)     { return pots[k].broken; }

void clearPotFaults() {
  for (uint8_t k = 0; k < NUM_KNOBS; k++) { pots[k].broken = false; pots[k].brokenSince = 0; }
}

int readPotSlow(uint8_t k) {
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(PIN_WIPER_A[k]);
    if (READ_GANG_B) sum += analogRead(PIN_WIPER_B[k]);
  }
  int value = sum / (READ_GANG_B ? 32 : 16);
  pots[k].a = pots[k].b = value;  // resync the filters
  return value;
}
