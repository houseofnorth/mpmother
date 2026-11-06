# MPMOTHER SysEx protocol

Used by the config tool (`docs/index.html`) over USB MIDI. Firmware side: `src/midi_io.cpp`.

Every message: `F0 7D 4D 50 <command> <data…> F7`
(`7D` = non-commercial ID, `4D 50` = "MP"). All data bytes are 7-bit.
14-bit numbers are sent as two bytes, high 7 bits first.

## Tool → controller

| Cmd  | Name           | Data            | Controller answers with |
|------|----------------|-----------------|-------------------------|
| `01` | hello          | –               | `41` hello              |
| `02` | get settings   | –               | `42` settings           |
| `03` | set settings   | settings bytes 0–14 | `42` settings (saved to EEPROM) |
| `04` | calibrate      | –               | `42` settings when done (~15 s, knobs move) |
| `05` | factory reset  | –               | `42` settings           |
| `06` | clear faults   | –               | `43` status             |
| `07` | monitor        | –               | `43` status every 50 ms for the next 5 s. Resend every 2 s to keep it going. |

## Controller → tool

**`41` hello**: firmware major, firmware minor, number of knobs.

**`42` settings** (35 bytes)

| Byte  | Meaning |
|-------|---------|
| 0     | out channel 1–16 (knobs send here) |
| 1     | drive channel 0–16 (Ableton moves knobs from here, 0 = off) |
| 2–5   | out CC, knob 1–4 |
| 6–9   | drive CC, knob 1–4 |
| 10    | bit 0 follow mapping feedback, bit 1 DIN out |
| 11    | motor speed 0–127 |
| 12    | screensaver speed 0–127 |
| 13–14 | screensaver delay in seconds (14-bit), 0 = off |
| 15–34 | per knob, 5 bytes: cal low (14-bit), cal high (14-bit), reverse. Read-only. |

**`43` status** (42 bytes)

| Byte | Meaning |
|------|---------|
| 0    | bit 0 drivers awake, bit 1 driver fault, bit 2 driver fault latched |
| 1    | bit 0 MIDI in, bit 1 MIDI out (in the last 50 ms) |
| 2–41 | per knob, 10 bytes: state, fault, flags, CC value, wiper A (14-bit), wiper B (14-bit), target (14-bit, 16383 = none) |

State: 0 idle, 1 follow, 2 user, 3 saver, 4 fault.
Fault: 0 none, 1 pot gangs disagree, 2 wrong direction, 3 motor not moving.
Flags: bit 0 pot worn, bit 1 CC value valid, bit 2 motor running.
