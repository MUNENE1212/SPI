// ===========================================================================
//  03_sd_card — MicroSD card over SPI (write / read / list)
// ---------------------------------------------------------------------------
//  Demonstrates the ESP32 Arduino core's built-in SD driver. The card sits
//  on the same VSPI bus as every other SPI project; only the CS pin differs
//  so it can coexist with other peripherals.
//
//  Flow:
//      1) Mount the card.
//      2) Report card type + size.
//      3) Append a boot log line to /boot.log.
//      4) Read /boot.log back and stream it to Serial.
//      5) List the files at the root.
//
//  Wiring (ESP32 hardware VSPI):
//      SCK  -> GPIO 18   DI (MOSI) -> GPIO 23
//      DO   -> GPIO 19   CS        -> GPIO 15    VCC=3V3  GND=GND
// ===========================================================================

#include <Arduino.h>                       // Core Arduino types + Serial.
#include <SPI.h>                           // Hardware SPI driver.
#include <FS.h>                            // File system abstraction (base class).
#include <SD.h>                            // SD card implementation of FS.

#include "emen_serial.h"                   // Shared boot banner.

#define SD_CS_PIN  15                      // Chip-select for the SD card slot.

// ---------------------------------------------------------------------------
// Print a human-readable name for the SD card type reported by the driver.
// Reused after mount to help the user confirm what physical card is inside.
// ---------------------------------------------------------------------------
void reportCardType() {
  Serial.print("Card type: ");             // Prefix so grepping the log is easy.
  switch (SD.cardType()) {                 // sdcard_type_t enum from the SD lib.
    case CARD_NONE:   Serial.println("NONE (no card mounted)"); break;
    case CARD_MMC:    Serial.println("MMC");   break;
    case CARD_SD:     Serial.println("SDSC (standard capacity)");   break;
    case CARD_SDHC:   Serial.println("SDHC (high capacity)"); break;
    default:          Serial.println("UNKNOWN");
  }
}

// Report the card's usable size in mebibytes (MiB = 1024*1024 bytes).
void reportCardSize() {
  uint64_t bytes = SD.cardSize();          // 64-bit because cards can exceed 4 GB.
  uint32_t mib   = (uint32_t)(bytes / (1024ULL * 1024ULL));
  Serial.printf("Card size: %lu MiB\n", (unsigned long)mib);
}

// Append `line` to `path`, creating the file if it does not exist yet.
// Returns true on success. Wraps the open/write/close dance so the caller
// doesn't have to remember to close the file handle.
bool appendLine(fs::FS &fs, const char* path, const char* line) {
  File f = fs.open(path, FILE_APPEND);     // FILE_APPEND creates if missing.
  if (!f) {                                // A null-truthy File means open failed.
    Serial.printf("open(%s) failed\n", path);
    return false;
  }
  size_t written = f.println(line);        // println() adds "\r\n" at the end.
  f.close();                               // Always close — flushes buffered writes.
  return written > 0;                      // println returns bytes written.
}

// Stream a whole file to Serial, line by line, so we can inspect what's on
// the card without leaving the console.
void dumpFile(fs::FS &fs, const char* path) {
  File f = fs.open(path, FILE_READ);       // FILE_READ = read-only mode.
  if (!f) {
    Serial.printf("open(%s) failed\n", path);
    return;
  }
  Serial.printf("--- %s (%u bytes) ---\n", path, (unsigned)f.size());
  while (f.available()) Serial.write(f.read());   // Raw byte pass-through.
  Serial.println("--- end ---");
  f.close();
}

// Walk the root directory once and print each entry with size or "<DIR>".
void listRoot(fs::FS &fs) {
  Serial.println("Root directory:");
  File root = fs.open("/");                // "/" is the mount point.
  if (!root || !root.isDirectory()) {      // Guard against non-dir handles.
    Serial.println("  (root not readable)");
    return;
  }
  File entry = root.openNextFile();        // Iterator pattern — returns null when done.
  while (entry) {
    Serial.printf("  %s  %s  %u\n",
      entry.isDirectory() ? "<DIR>" : "     ",
      entry.name(),
      (unsigned)entry.size());
    entry = root.openNextFile();           // Advance to next entry.
  }
}

// ---------------------------------------------------------------------------
// setup() — one-shot demo. loop() has nothing to do.
// ---------------------------------------------------------------------------
void setup() {
  logBoot("03_sd_card");                   // Serial.begin + banner.

  // SD.begin(csPin) uses the default hardware SPI object with the CS pin
  // we specify. Returns false if the card can't be mounted.
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD mount failed. Halting.");
    while (true) delay(1000);              // Idle loop — user retries by resetting.
  }
  Serial.println("SD mount OK.");

  reportCardType();                        // Print card type + size so we know it's the right card.
  reportCardSize();

  // Write a line to the boot log, then dump the whole file.
  appendLine(SD, "/boot.log", "boot: 03_sd_card ran");
  dumpFile   (SD, "/boot.log");

  listRoot(SD);                            // Show what's on the card now.
}

void loop() {
  // Nothing to do — one-shot demo.
}
