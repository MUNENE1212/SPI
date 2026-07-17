// ===========================================================================
//  04_shift_register — Raw SPI to a 74HC595 shift register
// ---------------------------------------------------------------------------
//  The 74HC595 is a serial-in / parallel-out chip: you shift 8 bits in on
//  DS as SHCP ticks, then pulse STCP (the latch) to move those bits to Q0..Q7
//  simultaneously. This is exactly what SPI does — MOSI == DS, SCK == SHCP —
//  so we drive it with the hardware SPI peripheral and pulse a GPIO for the
//  latch line ourselves.
//
//  Two patterns run in a non-blocking loop, alternating every second:
//      * WALK   — a single lit LED moves left to right
//      * CYLON  — a bit bounces back and forth (Knight Rider)
//
//  Wiring:
//      DS   -> GPIO 23  (MOSI)
//      SHCP -> GPIO 18  (SCK)
//      STCP -> GPIO 5   (latch, GPIO)
//      MR   -> 3V3      (master reset held high = never reset)
//      OE   -> GND      (output enable is active-LOW = outputs enabled)
//      Q0..Q7 -> 8 LEDs to ground (through the LED's built-in resistor).
// ===========================================================================

#include <Arduino.h>                       // Core Arduino types + Serial.
#include <SPI.h>                           // Hardware SPI driver.

#include "emen_serial.h"                   // Shared boot banner.
#include "spi_pins.h"                      // SPI_SCK_PIN, SPI_MOSI_PIN.

#define LATCH_PIN     5                    // STCP: pulse HIGH to publish shifted bits.
#define STEP_INTERVAL 120                  // ms between pattern steps.
#define SWAP_INTERVAL 8000                 // ms between WALK <-> CYLON.

// SPI transfer settings. Reused every send so we only construct this once.
// 4 MHz is well within the 74HC595's 25 MHz limit and safe on breadboard.
static const SPISettings SR_SPI(4000000, MSBFIRST, SPI_MODE0);

// ---------------------------------------------------------------------------
// Push one byte to the shift register and latch it to the parallel outputs.
// This is THE key primitive — everything else calls this.
// ---------------------------------------------------------------------------
void writeSR(uint8_t bits) {
  SPI.beginTransaction(SR_SPI);            // Acquire the bus with our settings.
  digitalWrite(LATCH_PIN, LOW);            // Hold latch LOW so outputs don't change mid-shift.
  SPI.transfer(bits);                      // Shifts 8 bits out MSB first on MOSI (== DS).
  digitalWrite(LATCH_PIN, HIGH);           // Rising edge on STCP copies shift reg -> outputs.
  SPI.endTransaction();                    // Release the bus for other users.
}

// Pattern generator #1: WALK. Returns the bit pattern for "step" (0..7).
// step 0 -> 0b00000001,  step 7 -> 0b10000000.
uint8_t patternWalk(uint8_t step) {
  return (uint8_t)(1u << (step & 0x07));   // Mask to 3 bits so it wraps at 8.
}

// Pattern generator #2: CYLON. Bounces 0..6 then 7..1 (period 14).
// This keeps the animation smooth without ever dwelling on the endpoints.
uint8_t patternCylon(uint8_t step) {
  uint8_t s = step % 14;                   // Bring step into 0..13.
  uint8_t idx = (s < 7) ? s : (14 - s);    // Rise then fall.
  return (uint8_t)(1u << idx);
}

// ---------------------------------------------------------------------------
// setup() — configure pins and start SPI. loop() does the animation.
// ---------------------------------------------------------------------------
void setup() {
  logBoot("04_shift_register");            // Serial banner.

  pinMode(LATCH_PIN, OUTPUT);              // The latch is a plain GPIO we drive high/low.
  digitalWrite(LATCH_PIN, HIGH);           // Idle high — no transfer in progress.

  SPI.begin(SPI_SCK_PIN, /*miso*/-1, SPI_MOSI_PIN, /*ss*/-1);
  // Explicit begin lets us pin the pins we want (defaults are already right,
  // but being explicit documents the intent). MISO is unused for a 74HC595.

  writeSR(0x00);                           // Blank the outputs on boot.
  Serial.println("Shift register ready.");
}

// ---------------------------------------------------------------------------
// loop() — non-blocking pattern player. millis() drives every timing.
// ---------------------------------------------------------------------------
void loop() {
  static uint32_t lastStep = 0;            // Timestamp of the last pattern step.
  static uint32_t modeSince = 0;           // When the current mode started.
  static uint8_t  step = 0;                // Which frame we're on.
  static uint8_t  mode = 0;                // 0 = walk, 1 = cylon.

  const uint32_t now = millis();           // Read the clock once per loop.

  // Advance the animation frame every STEP_INTERVAL milliseconds.
  if (now - lastStep >= STEP_INTERVAL) {
    lastStep = now;                        // Remember when we advanced.
    uint8_t bits = (mode == 0) ? patternWalk(step) : patternCylon(step);
    writeSR(bits);                         // Push to hardware.
    step++;                                // Ready for the next frame.
  }

  // Swap patterns every SWAP_INTERVAL milliseconds.
  if (now - modeSince >= SWAP_INTERVAL) {
    modeSince = now;
    mode ^= 1;                             // XOR flips 0<->1.
    step  = 0;                             // Restart the pattern from step 0.
    Serial.printf("Mode -> %s\n", mode ? "CYLON" : "WALK");
  }
}
