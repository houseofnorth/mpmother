# Hardware

Schematics and board files for MPMOTHER v1, designed in Fusion Electronics.

| Board | What it is |
|-------|------------|
| MPMOTHER v1 | Main board: Teensy 4.1, two Pololu DRV8833 drivers, four IDC 2×4 headers to the pot boards |
| MPCHILD v1  | Pot board, one per knob: the motorised pot and its IDC 2×4 header |

## Files

| File | What it is |
|------|------------|
| [`Teensy_Mother_Schematic.sch`](Teensy_Mother_Schematic.sch) | MPMOTHER v1 schematic (EAGLE format, from Fusion Electronics) |
| [`Teen Mom PCB.brd`](Teen%20Mom%20PCB.brd) | MPMOTHER v1 board layout (EAGLE format, "Teen Mom PCB" in Fusion) |
| [`gerbers/Teen_Mom_PCB_Manufacturing_Output.zip`](gerbers/Teen_Mom_PCB_Manufacturing_Output.zip) | MPMOTHER v1 manufacturing output: Gerbers, drill, pick and place, parts list |
| [`gerbers/Motorboard_pcb_Manufacturing_Output.zip`](gerbers/Motorboard_pcb_Manufacturing_Output.zip) | MPCHILD v1 manufacturing output: Gerbers, drill, pick and place |

The Gerber zips are the v1 boards as ordered (JLCPCB): upload a zip as it is.
Pin map and wiring as used by the firmware: [`src/pins.h`](../src/pins.h).

## MPMOTHER v1 (main board)

![MPMOTHER v1 main board, rendered](../media/mpmother%20v1.png)

Layout:

![MPMOTHER v1 board layout](../media/Board%20v1.png)

Schematic:

![MPMOTHER v1 schematic](../media/schematic%20v1.png)

## MPCHILD v1 (pot board)

![MPCHILD v1 pot board with its motorised pot, rendered](../media/mpchild%20v1.png)
