#include "console.h"
#include "settings.h"
#include "knobs.h"
#include "pots.h"
#include "motors.h"
#include "midi_io.h"

static void printStatus() {
  Serial.printf("MPMOTHER  out ch %d  drive ch %d  feedback %s  DIN %s\n", settings.outChannel,
                settings.driveChannel, settings.followFeedback ? "on" : "off", settings.dinOut ? "on" : "off");
  Serial.printf("speed %d\ndrivers %s%s\n", driversAwake() ? "awake" : "asleep",
                driverFaultLatched() ? "  FAULT (latched)" : driverFault() ? "  FAULT" : "");
  for (uint8_t k = 0; k < NUM_KNOBS; k++) {
    Serial.printf("CH%d  out cc %3d  drive cc %3d  pos %4d (A %4d B %4d)  cal %4d-%4d  rev %d  %-6s %s%s\n",
                  k + 1, settings.outCC[k], settings.driveCC[k], potPosition(k), wiperA(k), wiperB(k),
                  settings.calLow[k], settings.calHigh[k], settings.reverse[k], knobStateName(k),
                  potWorn(k) ? "worn " : "", knobFaultName(k));
  }
}

static void printHelp() {
  Serial.println(
    "status                      settings and live state\n"
    "outch <1-16>                channel the knobs send on\n"
    "drivech <0-16>              channel Ableton drives the knobs on (0 = off)\n"
    "outcc <knob 1-4> <0-127>    CC a knob sends\n"
    "drivecc <knob 1-4> <0-127>  CC that moves a knob\n"
    "feedback on|off             follow Ableton's mapping feedback on the out channel\n"
    "din on|off                  also send on the DIN socket\n"
    "speed <0-127>               motor top speed\n"
    "calibrate                   find end stops and motor direction (knobs move!)\n"
    "clear                       clear faults\n"
    "send                        send all knob positions once\n"
    "defaults                    factory settings");
}

static bool isOn(const char *word) { return word && !strcmp(word, "on"); }

static void handleLine(char *line, void (*keepAlive)()) {
  char *cmd = strtok(line, " \t\r");
  if (!cmd) return;
  char *a = strtok(nullptr, " \t\r");
  char *b = strtok(nullptr, " \t\r");
  int knob = a ? atoi(a) - 1 : -1;
  bool changed = true;

  if      (!strcmp(cmd, "status"))         { printStatus(); changed = false; }
  else if (!strcmp(cmd, "outch") && a)     settings.outChannel = constrain(atoi(a), 1, 16);
  else if (!strcmp(cmd, "drivech") && a)   settings.driveChannel = constrain(atoi(a), 0, 16);
  else if (!strcmp(cmd, "outcc") && b && knob >= 0 && knob < NUM_KNOBS)   settings.outCC[knob] = constrain(atoi(b), 0, 127);
  else if (!strcmp(cmd, "drivecc") && b && knob >= 0 && knob < NUM_KNOBS) settings.driveCC[knob] = constrain(atoi(b), 0, 127);
  else if (!strcmp(cmd, "feedback") && a)  settings.followFeedback = isOn(a);
  else if (!strcmp(cmd, "din") && a)       settings.dinOut = isOn(a);
  else if (!strcmp(cmd, "speed") && a)     settings.maxSpeed = constrain(atoi(a), 0, 127);
  else if (!strcmp(cmd, "defaults"))       factoryDefaults();
  else if (!strcmp(cmd, "calibrate"))      { Serial.println("calibrating - hands off the knobs"); calibrateKnobs(keepAlive); }
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
