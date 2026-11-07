// pots.h - reads the motorised pots. With READ_GANG_B (pins.h) it reads both gangs
// and checks they agree; without it, gang A only and wiperB() just repeats wiperA().
#pragma once
#include "pins.h"

void setupPots();
void readPots();                // call once per control tick (1 kHz)

int  potPosition(uint8_t k);    // 0-1023, average of both gangs, smoothed
int  wiperA(uint8_t k);         // raw-ish gang readings, for monitoring
int  wiperB(uint8_t k);
int  wiperMismatch(uint8_t k);  // |A - B| in ADC counts
bool potBroken(uint8_t k);      // gangs disagree a lot -> stop that motor (wiper mismatch)
void clearPotFaults();
int  readPotSlow(uint8_t k);    // averaged reading for calibration (takes ~1 ms)
