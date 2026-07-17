// ===========================================================================
//  01_max7219_hello — EMEN Engineering shopfront signboard
// ---------------------------------------------------------------------------
//  Drives a chain of 12 MAX7219 8x8 red LED modules (96x8 px) over hardware
//  SPI on the ESP32 and cycles a rotating list of business messages, each
//  with its own in/out animation effect.
//
//  Wiring (ESP32 hardware VSPI):
//      DIN (data)        -> GPIO 23  (MOSI)
//      CLK (clock)       -> GPIO 18  (SCK)
//      CS  (chip select) -> GPIO 5
//      VCC -> 5V, GND -> GND
//
//  Note: MAX7219 modules are single-color (this chain is red). To display
//  multiple colors on a real fitting, use separate same-color chains
//  side-by-side, each on its own CS line.
// ===========================================================================

#include <Arduino.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include "emen_serial.h"                     // Shared boot banner.

#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW  // Wokwi widget uses "parola" layout.
#define MAX_DEVICES   12                     // 12 modules chained => 96x8 px.
#define CS_PIN        5                      // SCK=18, MOSI=23 are default.

MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Each entry is one full animation cycle: enter effect, on-screen pause,
// then exit effect. displayAnimate() returns true when the cycle ends, at
// which point we advance to the next entry.
struct Message {
  const char   *text;
  textEffect_t  effectIn;
  textEffect_t  effectOut;
  uint16_t      pauseMs;      // dwell time between in and out effects
};

const Message SCRIPT[] = {
  { "EMEN ENGINEERING",           PA_SCROLL_LEFT, PA_SCROLL_LEFT, 1800 },
  { "Electrical & Electronics",   PA_SCROLL_LEFT, PA_SCROLL_LEFT, 1500 },
  { "Wiring - Repairs - Installs",PA_SCROLL_LEFT, PA_SCROLL_LEFT, 1500 },
  { "24/7 SUPPORT",               PA_OPENING,     PA_CLOSING,     1800 },
  { "Nairobi, KE",                PA_GROW_UP,     PA_GROW_DOWN,   1500 },
  { "Call +254 799 954 672",      PA_SCROLL_LEFT, PA_SCROLL_LEFT, 2000 },
  { "BAITECH Solutions",          PA_MESH,        PA_MESH,        1500 },
  { "emen.onlineduka.shop",       PA_SCROLL_LEFT, PA_SCROLL_LEFT, 1500 },
};
constexpr uint8_t SCRIPT_LEN = sizeof(SCRIPT) / sizeof(SCRIPT[0]);

uint8_t msgIdx = 0;

// Reconfigure the display for the next message and reset the animation.
void queueMessage(uint8_t i) {
  const Message &m = SCRIPT[i];
  matrix.displayText(m.text, PA_CENTER, 45, m.pauseMs, m.effectIn, m.effectOut);
  matrix.displayReset();
}

void setup() {
  logBoot("01_max7219_hello");
  matrix.begin();
  matrix.setIntensity(4);                    // 0..15 — keep low; MAX7219s get hot.
  matrix.displayClear();
  queueMessage(msgIdx);
  Serial.println("EMEN signboard running.");
}

void loop() {
  if (matrix.displayAnimate()) {
    msgIdx = (msgIdx + 1) % SCRIPT_LEN;
    queueMessage(msgIdx);
  }
}
