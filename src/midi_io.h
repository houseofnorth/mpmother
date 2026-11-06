// midi_io.h - USB + DIN MIDI, and the SysEx link to the web config tool.
//
// CC in/out. Every knob has its own channel + CC for each of these. Defaults:
//   knobs send            ch 1, CC 20-23
//   Ableton drives knobs  ch 2, CC 20-23   (clips, automation, LFOs)
//   mapping feedback      ch 1, CC 20-23   (the knob's own send ch + CC echoed back by Live)
//
// SysEx protocol for the config tool: see PROTOCOL.md
#pragma once
#include "pins.h"

void setupMidi();
void updateMidi();                       // call as often as possible from loop()
void sendKnobCC(uint8_t k, uint8_t value);

bool midiInRecently(uint32_t ms);        // for the status LED
bool midiOutRecently(uint32_t ms);
const int NO_CALIBRATION = -2;
int  calibrationRequested();             // knob 0-3, ALL_KNOBS or NO_CALIBRATION. Cleared when read
void sendSettingsToTool();               // after a change from the serial console
