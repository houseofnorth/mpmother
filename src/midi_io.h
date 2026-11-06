// midi_io.h - USB + DIN MIDI, and the SysEx link to the web config tool.
//
// CC in/out (defaults):
//   knobs send            channel 1, CC 20-23
//   Ableton drives knobs  channel 2, CC 20-23   (clips, automation, LFOs)
//   mapping feedback      channel 1, CC 20-23   (knobs jump to the right place when a set loads)
//
// SysEx protocol for the config tool: see PROTOCOL.md
#pragma once
#include "pins.h"

void setupMidi();
void updateMidi();                       // call as often as possible from loop()
void sendKnobCC(uint8_t k, uint8_t value);

bool midiInRecently(uint32_t ms);        // for the status LED
bool midiOutRecently(uint32_t ms);
bool calibrationRequested();             // set by the config tool, cleared when read
void sendSettingsToTool();               // after a change from the serial console
