#include "settings.h"
#include <EEPROM.h>

Settings settings;

// Change this whenever the Settings layout changes, so old EEPROM content is ignored.
const uint32_t SETTINGS_MAGIC = 0x4D500103;  // "MP" v1.3

void factoryDefaults() {
  settings.magic          = SETTINGS_MAGIC;
  settings.followFeedback = true;
  settings.dinOut         = true;
  settings.maxSpeed       = 127;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    settings.outChannel[k]   = 1;       // knobs send on ch 1, CC 20-23
    settings.outCC[k]        = 20 + k;
    settings.driveChannel[k] = 2;       // Ableton moves them on ch 2, CC 20-23
    settings.driveCC[k]      = 20 + k;
    settings.calLow[k]  = 15;           // safe guesses until calibrated
    settings.calHigh[k] = 1008;
    settings.reverse[k] = false;
  }
}

static bool settingsLookSane() {
  if (settings.magic != SETTINGS_MAGIC) return false;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    if (settings.outChannel[k] < 1 || settings.outChannel[k] > 16) return false;
    if (settings.driveChannel[k] > 16) return false;
    if (settings.outCC[k] > 127 || settings.driveCC[k] > 127) return false;
    if (settings.calHigh[k] - settings.calLow[k] < 200) return false;
  }
  return true;
}

void loadSettings() {
  EEPROM.get(0, settings);
  if (!settingsLookSane()) {
    factoryDefaults();
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
}
