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
| `03` | set settings   | settings bytes 0–17 | `42` settings (saved to EEPROM) |
| `04` | calibrate      | knob 0–3, or 127 / nothing = all | `42` settings when done (~4 s per knob, knobs move) |
| `05` | factory reset  | –               | `42` settings           |
| `06` | clear faults   | –               | `43` status             |
| `07` | monitor        | –               | `43` status every 50 ms for the next 5 s. Resend every 2 s to keep it going. |

## Controller → tool

**`41` hello**: firmware major, firmware minor, number of knobs. Current firmware: 1.1.

**`42` settings** (38 bytes)

| Byte  | Meaning |
|-------|---------|
| 0–15  | per knob, 4 bytes: MIDI Out ch 1–16, Out CC, MIDI In ch 0–16 (0 = off), In CC |
| 16    | bit 0 follow mapping feedback, bit 1 DIN out |
| 17    | motor speed 0–127 |
| 18–37 | per knob, 5 bytes: cal low (14-bit), cal high (14-bit), reverse. Read-only. |

MIDI Out ch + CC: what the knob sends (and the mapping feedback it follows).
MIDI In ch + CC: what moves the knob (called drive in the firmware).

**`43` status** (42 bytes)

| Byte | Meaning |
|------|---------|
| 0    | bit 0 drivers awake, bit 1 driver fault, bit 2 driver fault latched |
| 1    | bit 0 MIDI in, bit 1 MIDI out (in the last 50 ms) |
| 2–41 | per knob, 10 bytes: state, fault, flags, CC value, wiper A (14-bit), wiper B (14-bit), target (14-bit, 16383 = none) |

State: 0 idle, 1 moving, 2 touch, 3 fault.
Fault: 0 none, 1 wiper mismatch, 2 motor reversed, 3 motor stalled.
Flags: bit 0 pot wear, bit 1 CC value valid, bit 2 motor running, bit 3 gang A only (wiper B repeats wiper A, no wear check).
