// ===========================================================================
//  05_rfid_mfrc522 — RFID reader (MFRC522) over SPI
// ---------------------------------------------------------------------------
//  Reads the UID of any RFID tag brought near the antenna and prints it to
//  Serial. A green LED on GPIO 2 blinks once per successful read as visual
//  confirmation.
//
//  In Wokwi: drag one of the "wokwi-rfid-tag" parts onto the reader's
//  antenna to trigger a read.
//
//  Wiring:
//      SCK  -> GPIO 18   MOSI -> GPIO 23   MISO -> GPIO 19
//      SDA/SS -> GPIO 5  RST -> GPIO 17    VCC=3V3  GND=GND
//      LED  -> GPIO 2 through a 220R resistor to GND (active-HIGH).
// ===========================================================================

#include <Arduino.h>                       // Core Arduino types + Serial.
#include <SPI.h>                           // Hardware SPI driver.
#include <MFRC522.h>                       // NXP MFRC522 driver by miguelbalboa.

#include "emen_serial.h"                   // Shared boot banner.

#define RFID_SS_PIN   5                    // Chip-select (SDA on the module).
#define RFID_RST_PIN  17                   // Reset line (pulsed at init).
#define LED_OK_PIN    2                    // Onboard-ish LED for read confirmation.

// Construct the reader. The library uses the default hardware SPI object.
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// ---------------------------------------------------------------------------
// Format a UID (up to 10 bytes) as colon-separated uppercase hex into `out`.
// `out` must be at least (3 * size + 1) bytes long. Returns a pointer to `out`
// so it can be passed directly to Serial.print().
// ---------------------------------------------------------------------------
char* formatUid(char* out, const uint8_t* uid, uint8_t size) {
  char* p = out;                            // Running cursor into the output buffer.
  for (uint8_t i = 0; i < size; i++) {      // For each byte of the UID...
    if (i > 0) *p++ = ':';                  // ...separator between bytes (skip before first).
    // sprintf %02X = 2 uppercase hex digits, zero-padded. It writes 2 chars.
    sprintf(p, "%02X", uid[i]);
    p += 2;                                 // Advance past the two hex chars.
  }
  *p = '\0';                                // Null-terminate the C-string.
  return out;
}

// Blink the confirmation LED once — non-blocking-safe because it uses delay
// only for a very short "flash" duration inside the "just read" branch,
// which is fine here since we've already consumed the card interaction.
void flashOk() {
  digitalWrite(LED_OK_PIN, HIGH);           // Turn LED on.
  delay(80);                                // Short flash so it's visible but snappy.
  digitalWrite(LED_OK_PIN, LOW);            // Back to idle.
}

// ---------------------------------------------------------------------------
// setup() — configure I/O, start SPI, initialize the reader.
// ---------------------------------------------------------------------------
void setup() {
  logBoot("05_rfid_mfrc522");               // Shared serial banner.

  pinMode(LED_OK_PIN, OUTPUT);              // LED pin is a plain digital output.
  digitalWrite(LED_OK_PIN, LOW);            // Start with LED off.

  SPI.begin();                              // Bring up VSPI on default pins.
  rfid.PCD_Init();                          // Reset + configure the reader chip.
  delay(4);                                 // Datasheet: allow >= 37.74us; be generous.

  Serial.println("Present a tag to the reader...");
}

// ---------------------------------------------------------------------------
// loop() — poll for tags. Non-blocking: return immediately if none present.
// ---------------------------------------------------------------------------
void loop() {
  // PICC_IsNewCardPresent() sends a short RF "wake-up" and returns true only
  // if something answers. It's cheap enough to call every loop iteration.
  if (!rfid.PICC_IsNewCardPresent()) return;

  // A card responded — now try to read its serial (UID). Some reads fail if
  // the tag moves during the exchange; in that case we bail and retry.
  if (!rfid.PICC_ReadCardSerial()) return;

  // We have the UID. Format it and log it.
  char buf[32];                             // 32 bytes fits any UID up to 10 bytes long.
  formatUid(buf, rfid.uid.uidByte, rfid.uid.size);
  Serial.print("UID: ");
  Serial.println(buf);
  flashOk();                                // Blink the LED to confirm visually.

  // Halt the tag so a second read isn't triggered by the same physical tag,
  // and stop the encrypted session so the reader is ready for the next one.
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
