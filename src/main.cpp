// MPMOTHER v1 - four motorised pots as a MIDI controller (Teensy 4.1, 2x DRV8833)
//
// What it does
//   - Turn a knob: it sends MIDI CC (USB + DIN).
//   - Ableton can move the knobs: on the drive channel (clips, automation, LFOs), and via
//     mapping feedback on the out channel (enable "Remote" on the MPMOTHER output in Live).
//   - A hand on a knob always wins: the motor lets go and the knob sends CC.
//   - Safety: watchdog, motor timeouts, driver fault handling, wiper mismatch check,
//     drivers asleep when nothing moves, everything off at start-up.
//
// Files
//   pins.h        what is connected where
//   settings      user settings in EEPROM
//   pots          reading the pots (gang A, or both gangs with a mismatch check)
//   motors        DRV8833 drivers: speed, sleep, faults
//   knobs         what each knob is doing (idle / moving / touch / fault)
//   midi_io       CC in/out and the SysEx link to the web config tool
//   led           status LED colours
//   console       text commands over USB serial
#include <Arduino.h>
#include <Watchdog_t4.h>
#include "settings.h"
#include "pots.h"
#include "motors.h"
#include "knobs.h"
#include "midi_io.h"
#include "led.h"
#include "console.h"

WDT_T4<WDT1> watchdog;  // resets the Teensy if the code ever hangs for 1 s

void feedWatchdog() { watchdog.feed(); }

void setup() {
  bool watchdogReset = SRC_SRSR & SRC_SRSR_WDOG_RST_B;
  SRC_SRSR = SRC_SRSR_WDOG_RST_B;  // clear the flag for next time

  setupMotors();   // first: make sure every motor is off
  setupLed(watchdogReset);
  Serial.begin(115200);
  loadSettings();
  setupPots();
  setupKnobs();
  setupMidi();

  WDT_timings_t timing;
  timing.trigger = 0.5;
  timing.timeout = 1.0;
  watchdog.begin(timing);

  delay(300);
  sendAllPositions();  // tell Ableton where the knobs are
}

void loop() {
  feedWatchdog();
  updateMidi();
  updateConsole(feedWatchdog);
  int calibrate = calibrationRequested();
  if (calibrate != NO_CALIBRATION) { calibrateKnobs(feedWatchdog, calibrate); sendSettingsToTool(); }

  static elapsedMicros sinceTick;
  if (sinceTick >= 1000) {  // 1 kHz control loop
    sinceTick = 0;
    readPots();
    updateKnobs();
    updateMotors();
    updateLed();
  }
}
