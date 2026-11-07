# Hardware

Schematics and board files for MPMOTHER v1, designed in Fusion Electronics.

| Board | What it is |
|-------|------------|
| MPMOTHER v1 | Main board: Teensy 4.1, two Pololu DRV8833 drivers, four IDC 2×4 headers to the pot boards |
| MPCHILD v1  | Pot board, one per knob: the motorised pot and its IDC 2×4 header |

## Gerbers (`gerbers/`)

Manufacturing output from Fusion, as ordered (v1, JLCPCB). Upload a zip as it is.

| File | Board | Contents |
|------|-------|----------|
| `Teen_Mom_PCB_Manufacturing_Output.zip` | MPMOTHER v1 (main board, "Teen Mom PCB" in Fusion) | Gerbers, drill, pick and place, parts list |
| `Motorboard_pcb_Manufacturing_Output.zip` | MPCHILD v1 (pot board) | Gerbers, drill, pick and place |

Schematics (`.sch`), board files (`.brd`) and the BOM belong in this folder too.
Pin map and wiring as used by the firmware: `src/pins.h`.
