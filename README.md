# MPMOTHER

Four motorised pots as a MIDI controller. Teensy 4.1, two Pololu DRV8833 drivers,
ALPS RK16812MG099 dual-gang motorised pots. House of North.

| Knob | Name     | Sends (default) | Driven by (default) |
|------|----------|-----------------|---------------------|
| 1    | SPACE    | ch 1, CC 20     | ch 2, CC 20         |
| 2    | TEXTURE  | ch 1, CC 21     | ch 2, CC 21         |
| 3    | MOVEMENT | ch 1, CC 22     | ch 2, CC 22         |
| 4    | SHADOW   | ch 1, CC 23     | ch 2, CC 23         |

## Using it with Ableton Live

- **Map a knob:** MIDI-map a parameter to the knob as usual (ch 1).
- **Knobs follow the set:** in Live's MIDI preferences, turn on *Remote* for the MPMOTHER
  **output**. Live then sends mapping feedback and the knobs jump to the right place.
- **Let Ableton drive the knobs:** send CC 20–23 on channel 2 to MPMOTHER from a MIDI track
  (clips, automation, Max for Live LFOs). The knobs move and send their position on ch 1.
- A hand on a knob always wins.

## Config tool

Open <https://houseofnorth.github.io/mpmother/> in Chrome or Edge with the controller plugged in.
It shows each knob live (position, both pot gangs, state, faults) and edits all settings.
It works while Ableton is running.

## Status LED

| Colour          | Meaning |
|-----------------|---------|
| dim green       | ready |
| blue flicker    | MIDI in or out |
| amber blink     | a pot is getting worn |
| red blink       | fault, motors held off |
| solid red       | driver kept faulting, needs *Clear faults* or a power cycle |
| white blink ×3  | restarted by the watchdog |

## Firmware

PlatformIO project. Open this folder in VS Code with the PlatformIO extension, then
*Build* (✓) and *Upload* (→). Serial monitor at 115200 – type `help` for text commands.

First time on new hardware: run **Calibrate** (config tool or `calibrate`), hands off the knobs.

Code layout and tuning values are explained at the top of each file in `src/`.
SysEx details: [PROTOCOL.md](PROTOCOL.md).
