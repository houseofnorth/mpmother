#include "console.h"
#include "settings.h"
#include "knobs.h"
#include "pots.h"
#include "motors.h"
#include "midi_io.h"

// Channel always before CC: "ch 1 cc 20".
static void printStatus() {
  Serial.printf("MPMOTHER  feedback %s  DIN %s  speed %d\n", settings.followFeedback ? "on" : "off",
                settings.dinOut ? "on" : "off", settings.maxSpeed);
  Serial.printf("motors %s%s\n", !settings.motorsOn ? "off (passive controller)" : driversAwake() ? "active" : "standby",
                driverFaultLatched() ? "  FAULT (latched)" : driverFault() ? "  FAULT" : "");
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    char drive[24];
    if (settings.driveChannel[k]) snprintf(drive, sizeof(drive), "ch %d cc %d", settings.driveChannel[k], settings.driveCC[k]);
    else                          snprintf(drive, sizeof(drive), "off");
    Serial.printf("knob %d  out ch %d cc %d  in %s  pos %4d (A %4d B %4d)  cal %4d-%4d  rev %d  %-6s %s\n",
                  k + 1, settings.outChannel[k], settings.outCC[k], drive, potPosition(k), wiperA(k), wiperB(k),
                  settings.calLow[k], settings.calHigh[k], settings.reverse[k], knobStateName(k),
                  knobFaultName(k));
  }
}

static void printHelp() {
  Serial.println(
    "status                      settings and live state\n"
    "outch <knob 1-4> <1-16>     MIDI Out channel of a knob\n"
    "outcc <knob 1-4> <0-127>    MIDI Out CC of a knob\n"
    "drivech <knob 1-4> <0-16>   MIDI In channel that moves a knob (0 = off)\n"
    "drivecc <knob 1-4> <0-127>  MIDI In CC that moves a knob\n"
    "motors on|off               off = passive controller: knobs only send, MIDI In is ignored\n"
    "feedback on|off             knobs follow values received on their Out ch + CC\n"
    "din on|off                  mirror MIDI Out to the 5-pin DIN socket\n"
    "speed <0-127>               motor top speed\n"
    "calibrate [knob 1-4]        find end stops and motor direction (knobs move!)\n"
    "clear                       clear faults\n"
    "send                        send all knob positions once\n"
    "defaults                    factory settings (calibration is kept)");
}

static bool isOn(const char *word) { return word && !strcmp(word, "on"); }

static void handleLine(char *line, void (*keepAlive)()) {
  char *cmd = strtok(line, " \t\r");
  if (!cmd) return;
  char *a = strtok(nullptr, " \t\r");
  char *b = strtok(nullptr, " \t\r");
  int knob = a ? atoi(a) - 1 : -1;
  bool knobAndValue = b && knob >= 0 && knob < NUM_KNOBS;
  bool changed = true;

  if      (!strcmp(cmd, "status"))                  { printStatus(); return; }   // printed once, not again below
  else if (!strcmp(cmd, "outch") && knobAndValue)   settings.outChannel[knob] = constrain(atoi(b), 1, 16);
  else if (!strcmp(cmd, "outcc") && knobAndValue)   settings.outCC[knob] = constrain(atoi(b), 0, 127);
  else if (!strcmp(cmd, "drivech") && knobAndValue) settings.driveChannel[knob] = constrain(atoi(b), 0, 16);
  else if (!strcmp(cmd, "drivecc") && knobAndValue) settings.driveCC[knob] = constrain(atoi(b), 0, 127);
  else if (!strcmp(cmd, "motors") && a)    settings.motorsOn = isOn(a);
  else if (!strcmp(cmd, "feedback") && a)  settings.followFeedback = isOn(a);
  else if (!strcmp(cmd, "din") && a)       settings.dinOut = isOn(a);
  else if (!strcmp(cmd, "speed") && a)     settings.maxSpeed = constrain(atoi(a), 0, 127);
  else if (!strcmp(cmd, "defaults"))       factoryDefaults();
  else if (!strcmp(cmd, "calibrate"))      { if (settings.motorsOn) Serial.println("calibrating - hands off the knobs");
                                             calibrateKnobs(keepAlive, (knob >= 0 && knob < NUM_KNOBS) ? knob : ALL_KNOBS); }
  else if (!strcmp(cmd, "clear"))          { clearKnobFaults(); clearDriverFault(); changed = false; }
  else if (!strcmp(cmd, "send"))           { sendAllPositions(); changed = false; }
  else                                     { printHelp(); return; }

  if (changed) { saveSettings(); sendSettingsToTool(); }
  printStatus();
}

void updateConsole(void (*keepAlive)()) {
  static char line[64];
  static uint8_t length = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { line[length] = 0; handleLine(line, keepAlive); length = 0; }
    else if (length < sizeof(line) - 1) line[length++] = c;
  }
}
