// settings.h - everything the user can change. Stored in EEPROM, survives power-off.
#pragma once
#include "pins.h"

struct Settings {
  uint32_t magic;               // marks valid saved settings

  // MIDI
  uint8_t  outChannel;          // 1-16  knobs send their position here
  uint8_t  driveChannel;        // 1-16  Ableton moves the knobs from here (0 = off)
  uint8_t  outCC[NUM_KNOBS];    // CC number each knob sends
  uint8_t  driveCC[NUM_KNOBS];  // CC number that moves each knob on the drive channel
  bool     followFeedback;      // also move knobs when Ableton echoes outCC on outChannel
  bool     dinOut;              // also send on the DIN MIDI socket

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
