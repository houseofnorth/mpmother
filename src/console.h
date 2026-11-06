// console.h - text commands over the USB serial port (PlatformIO monitor, 115200).
// Type "help" for the list.
#pragma once

void updateConsole(void (*keepAlive)());  // call from loop()
