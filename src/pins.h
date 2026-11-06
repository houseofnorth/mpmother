// pins.h - where everything is connected on MPMOTHER v1 (Teensy 4.1)
//
// Knob order:  CH1 = SPACE (top left)      CH2 = TEXTURE (top right)
//              CH3 = MOVEMENT (bottom left) CH4 = SHADOW (bottom right)
#pragma once
#include <Arduino.h>

const uint8_t NUM_KNOBS = 4;

// Motor driver inputs (two Pololu DRV8833 carriers).
// Driver 1 runs CH1 + CH2, driver 2 runs CH3 + CH4.
const uint8_t PIN_MOTOR_IN1[NUM_KNOBS] = { 2, 4, 6, 36 };
const uint8_t PIN_MOTOR_IN2[NUM_KNOBS] = { 3, 5, 9, 37 };

const uint8_t PIN_DRIVER_SLEEP = 10;  // nSLEEP, both drivers. LOW = motors off, HIGH = awake
const uint8_t PIN_DRIVER_FAULT = 11;  // nFAULT, both drivers. LOW = over-current / over-heat

// Each motorised pot has two gangs (A and B) on the same shaft.
// Both should read the same - if they drift apart the pot is worn or dirty.
const uint8_t PIN_WIPER_A[NUM_KNOBS] = { 14, 16, 20, 22 };
const uint8_t PIN_WIPER_B[NUM_KNOBS] = { 15, 17, 21, 23 };

// Status LED (plain analog RGB, Kingbright 5 mm)
const uint8_t PIN_LED_RED   = 24;
const uint8_t PIN_LED_GREEN = 25;
const uint8_t PIN_LED_BLUE  = 28;
const bool    LED_COMMON_ANODE = false;  // set true if the LED's common leg goes to 3V3

// DIN MIDI out goes through Serial1 TX = pin 1.
// Qwiic / I2C = 18 (SDA), 19 (SCL).  AUX header spare pins = 40, 41.  Not used yet.
