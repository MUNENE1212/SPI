// ===========================================================================
//  01_max7219_hello — First SPI project
// ---------------------------------------------------------------------------
//  Talks to four chained MAX7219 8x8 LED-matrix modules (8x32 total) over
//  hardware SPI on the ESP32 and scrolls "SPI  //  hello" across them.
//
//  Wiring (ESP32 hardware VSPI):
//      DIN (data)  -> GPIO 23  (MOSI)
//      CLK (clock) -> GPIO 18  (SCK)
//      CS  (chip select) -> GPIO 5
//      VCC -> 5V, GND -> GND
//
//  Why this matters: unlike I2C (which addresses devices by 7-bit numbers on
//  a shared 2-wire bus), SPI selects the target with a dedicated CS line and
//  streams bits synchronously to a clock. MD_MAX72XX handles the framing;
//  MD_Parola layers scrolling/fade effects on top.
// ===========================================================================

#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW   // Common Wokwi/generic modules.
#define MAX_DEVICES   4                     // 4 modules chained => 8x32 px.
#define CS_PIN        5                     // SCK=18, MOSI=23 are default.

MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

void setup() {
  Serial.begin(115200);
  matrix.begin();
  matrix.setIntensity(4);              // 0..15
  matrix.displayClear();
  matrix.displayText(
    "SPI  //  hello",
    PA_CENTER,                         // vertical alignment when idle
    40,                                // frame delay ms (lower = faster)
    1500,                              // pause after each animation ms
    PA_SCROLL_LEFT,                    // enter effect
    PA_SCROLL_LEFT                     // exit effect
  );
  Serial.println("MAX7219 scroll running.");
}

void loop() {
  // displayAnimate() returns true when the current animation ended.
  // Reset it to loop the same effect forever.
  if (matrix.displayAnimate()) matrix.displayReset();
}
