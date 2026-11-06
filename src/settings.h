// settings.h - everything the user can change. Stored in EEPROM, survives power-off.
#pragma once
#include "pins.h"

struct Settings {
  uint32_t magic;               // marks valid saved settings

  // MIDI, per knob: channel first, then CC
  uint8_t  outChannel[NUM_KNOBS];    // 1-16  channel the knob sends its position on
  uint8_t  outCC[NUM_KNOBS];         // CC the knob sends
  uint8_t  driveChannel[NUM_KNOBS];  // 1-16  channel Ableton moves the knob from (0 = off)
  uint8_t  driveCC[NUM_KNOBS];       // CC that moves the knob
  bool     followFeedback;           // also move a knob when Ableton echoes its out ch + CC
  bool     dinOut;                   // also send on the DIN MIDI socket

  // Motion
  uint8_t  maxSpeed;            // 0-127  top motor speed when following Ableton

  // Calibration (filled in by "calibrate")
  int16_t  calLow[NUM_KNOBS];   // pot reading at the low end stop
  int16_t  calHigh[NUM_KNOBS];  // pot reading at the high end stop
  bool     reverse[NUM_KNOBS];  // motor is wired the other way round
};

extern Settings settings;

void loadSettings();     // read from EEPROM, or factory defaults if nothing valid is stored
void saveSettings();
void factoryDefaults();  // does not save - call saveSettings() afterwards
