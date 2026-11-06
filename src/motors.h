// motors.h - the two DRV8833 drivers: speed, sleep and fault handling.
#pragma once
#include "pins.h"

void setupMotors();                  // boots with every motor off and drivers asleep
void setMotor(uint8_t k, int speed); // -255..255, 0 = off. Positive = towards higher readings
void stopAllMotors();
void updateMotors();                 // call every control tick: sleep timer + fault check

int  motorSpeed(uint8_t k);
bool driversAwake();
bool driverFault();                  // a fault is active right now (motors are held off)
bool driverFaultLatched();           // too many faults in a row - stays off until cleared
void clearDriverFault();
