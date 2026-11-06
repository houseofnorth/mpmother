// pins.h - where everything is connected on MPMOTHER v1 (Teensy 4.1)
//
// Knob order in firmware = front panel reading order:
//   knob 1 SPACE (top left)   knob 2 TEXTURE (top right)
//   knob 3 MOVEMENT (bottom left)   knob 4 SHADOW (bottom right)
//
// The board's channel names (schematic nets CHn_*) run the other way for SPACE and SHADOW:
//   SPACE = CH4 (J4, DRV2)   TEXTURE = CH2 (J2, DRV1)   MOVEMENT = CH3 (J3, DRV2)   SHADOW = CH1 (J1, DRV1)
// So the arrays below are listed in knob order, not CH order. Checked against the Fusion schematic 2026-11-06.
#pragma once
#include <Arduino.h>

const uint8_t NUM_KNOBS = 4;

// Motor driver inputs (two Pololu DRV8833 carriers).
// DRV1 runs SHADOW (A) + TEXTURE (B), DRV2 runs SPACE (B) + MOVEMENT (A).
//                                          SPACE TEXTURE MOVEMENT SHADOW
const uint8_t PIN_MOTOR_IN1[NUM_KNOBS] = {  36,   4,      6,       2 };
const uint8_t PIN_MOTOR_IN2[NUM_KNOBS] = {  37,   5,      9,       3 };

const uint8_t PIN_DRIVER_SLEEP = 10;  // nSLEEP, both drivers. LOW = motors off, HIGH = awake
const uint8_t PIN_DRIVER_FAULT = 11;  // nFAULT, both drivers. LOW = over-current / over-heat

// Each motorised pot has two gangs (A and B) on the same shaft.
// Both should read the same - if they drift apart the pot is worn or dirty.
//                                        SPACE TEXTURE MOVEMENT SHADOW
const uint8_t PIN_WIPER_A[NUM_KNOBS] = {  22,   16,     20,      14 };
const uint8_t PIN_WIPER_B[NUM_KNOBS] = {  23,   17,     21,      15 };

// Status LED (plain analog RGB, Kingbright 5 mm)
const uint8_t PIN_LED_RED   = 24;
const uint8_t PIN_LED_GREEN = 25;
const uint8_t PIN_LED_BLUE  = 28;
const bool    LED_COMMON_ANODE = false;  // L-154A4SURKQBDZGW is common cathode, driven via SN74AHCT125 at 5 V

// DIN MIDI out goes through Serial1 TX = pin 1.
// Qwiic / I2C = 18 (SDA), 19 (SCL).  AUX header spare pins = 40, 41.  Not used yet.
