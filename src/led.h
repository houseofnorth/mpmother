// led.h - the RGB status LED.
//
//   white            starting up
//   dim green        ready
//   blue flicker     MIDI in or out
//   red blink        a knob or driver fault - motors held off
//   solid red        driver kept faulting - needs "clear faults" or a power cycle
//   white blink x3   rebooted by the watchdog (shown once after start-up)
#pragma once
#include "pins.h"

void setupLed(bool afterWatchdogReset);
void updateLed();  // call every few ms
