// knobs.h - what each of the four knobs is doing, and why.
//
//   IDLE    resting, waiting. Turning it by hand -> USER
//   FOLLOW  motor drives it to a position Ableton asked for
//   USER    a hand is on it: motor off, knob sends CC, MIDI in is ignored
//   FAULT   motor locked off (wiper mismatch, motor reversed, motor stalled). Still sends CC.
//
// With the motors off (settings.motorsOn = false) every knob is a plain controller:
// it only sends, MIDI In is ignored, IDLE / USER only (FAULT for a wiper mismatch).
#pragma once
#include "pins.h"

enum KnobState : uint8_t { IDLE, FOLLOW, USER, FAULT };

enum KnobFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_WIPERS,      // the two gangs of the pot disagree a lot
  FAULT_DIRECTION,   // the knob keeps moving away from where the motor pushes it
  FAULT_NOT_MOVING,  // the motor runs but the knob does not move
};

void setupKnobs();
void updateKnobs();                         // call every control tick (1 kHz)

void moveKnobFromMidi(uint8_t k, uint8_t value, bool isFeedback);  // 0-127. MIDI In, or mapping feedback
void sendAllPositions();                    // force every knob to send its CC once
void clearKnobFaults();
const int ALL_KNOBS = -1;
void calibrateKnobs(void (*keepAlive)(), int which = ALL_KNOBS);  // blocking ~10 s per knob, knobs move!

KnobState   knobState(uint8_t k);
KnobFault   knobFault(uint8_t k);
const char *knobStateName(uint8_t k);
const char *knobFaultName(uint8_t k);
int         knobTarget(uint8_t k);          // ADC target, -1 = none
int         knobLastCC(uint8_t k);          // last CC value sent, -1 = none yet
