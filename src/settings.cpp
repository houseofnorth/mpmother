#include "settings.h"
#include <EEPROM.h>

Settings settings;

// Change this whenever the Settings layout changes, so old EEPROM content is ignored.
const uint32_t SETTINGS_MAGIC = 0x4D500102;  // "MP" v1.2

void factoryDefaults() {
  settings.magic          = SETTINGS_MAGIC;
  settings.outChannel     = 1;
  settings.driveChannel   = 2;
  settings.followFeedback = true;
  settings.dinOut         = true;
  settings.maxSpeed       = 127;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    settings.outCC[k]   = 20 + k;  // CC 20-23
    settings.driveCC[k] = 20 + k;
    settings.calLow[k]  = 15;      // safe guesses until calibrated
    settings.calHigh[k] = 1008;
    settings.reverse[k] = false;
  }
}

static bool settingsLookSane() {
  if (settings.magic != SETTINGS_MAGIC) return false;
  if (settings.outChannel < 1 || settings.outChannel > 16) return false;
  if (settings.driveChannel > 16) return false;
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
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
