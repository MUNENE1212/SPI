// emen_serial.h — Consistent boot banner over Serial for every project.
// ---------------------------------------------------------------------------
// Called at the top of setup() so every log starts the same way. Header-only
// (marked `inline`) so we don't need a .cpp file to link against.

#pragma once
#include <Arduino.h>            // Brings in Serial, delay(), etc.

inline void logBoot(const char* projectName) {
  Serial.begin(115200);         // 115200 baud = the repo-wide monitor_speed.
  delay(50);                    // Let the USB-serial port enumerate before we print.
  Serial.println();             // Blank line — separates from previous boot logs.
  Serial.print("=== SPI :: "); // Fixed prefix so log lines are grep-able.
  Serial.print(projectName);    // Project's own name identifies the sketch.
  Serial.println(" ===");       // Trailing separator; println() also flushes.
}
