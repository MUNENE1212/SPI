// ===========================================================================
//  06_ws2812_signboard_spi — Full-color EMEN signboard driven via SPI-MOSI
// ---------------------------------------------------------------------------
//  WS2812 is NOT an SPI device. Its wire protocol is single-line NRZ at
//  800 kHz: each pixel bit is a differently-timed high pulse.
//      "0" bit: ~350 ns high, ~800 ns low
//      "1" bit: ~700 ns high, ~600 ns low
//  Total ~1.25 us per bit either way.
//
//  Trick: run the ESP32 hardware SPI peripheral at 3.2 MHz (312.5 ns per SPI
//  bit) and encode every WS2812 bit as 4 SPI bits ("nibble"):
//      WS 0 -> SPI nibble 0b1000  = high 1 cycle, low 3  -> ~312 ns / ~938 ns
//      WS 1 -> SPI nibble 0b1110  = high 3 cycles, low 1 -> ~938 ns / ~312 ns
//  Both fall inside the WS2812 timing window. Each WS2812 byte (8 bits) is
//  transmitted as 4 SPI bytes (32 SPI bits). The peripheral shifts MOSI
//  autonomously, so the CPU only has to encode + kick off the transfer.
//
//  Wiring (ESP32 VSPI):
//      DIN -> GPIO 23 (MOSI — WS2812 timing generator)
//      VDD -> 5V,  VSS -> GND
//      SCK (18) is unused by the WS2812 side but the peripheral still drives
//      it — don't share VSPI with a real SPI slave here.
// ===========================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>              // Text/font rasterisation only.
#include "emen_serial.h"

constexpr uint16_t COLS         = 64;
constexpr uint16_t ROWS         = 8;
constexpr uint16_t PIXEL_COUNT  = COLS * ROWS;
constexpr uint32_t SPI_HZ       = 3200000;   // 3.2 MHz -> 312.5 ns per SPI bit.
constexpr uint8_t  BRIGHTNESS   = 40;        // 0..255 — WS2812 is brutal at 255.

// Framebuffer in native R,G,B order (matches GFX's rgb565 unpack). We only
// convert to GRB (WS2812 wire order) at encode time.
uint8_t framebuffer[PIXEL_COUNT * 3] = {0};

// Pre-encoded SPI buffer: 4 SPI bytes encode 1 WS2812 byte -> 12 bytes/pixel.
uint8_t spiBuf[PIXEL_COUNT * 12];

// Lookup table: two WS2812 bits -> one SPI byte. Bit pattern per WS bit is
// 0b1000 for "0" and 0b1110 for "1"; concatenated as (high nibble, low nibble).
const uint8_t ENC[4] = {
  0x88,  // 00 -> 1000 1000
  0x8E,  // 01 -> 1000 1110
  0xE8,  // 10 -> 1110 1000
  0xEE   // 11 -> 1110 1110
};

// Progressive row-major mapping (matches Wokwi's default layout).
inline uint16_t xyToIndex(int16_t x, int16_t y) {
  if (x < 0 || x >= (int16_t)COLS) return 0xFFFF;
  if (y < 0 || y >= (int16_t)ROWS) return 0xFFFF;
  return (uint16_t)y * COLS + (uint16_t)x;
}

// Adafruit_GFX subclass — routes drawPixel() into our framebuffer so we get
// the built-in 5x7 font and print()/setCursor() plumbing for free.
class NeoCanvas : public Adafruit_GFX {
 public:
  NeoCanvas() : Adafruit_GFX(COLS, ROWS) {}
  void drawPixel(int16_t x, int16_t y, uint16_t rgb565) override {
    uint16_t idx = xyToIndex(x, y);
    if (idx == 0xFFFF) return;
    uint8_t *p = &framebuffer[idx * 3];
    p[0] = ((rgb565 >> 11) & 0x1F) << 3;   // R (5 bits -> 8)
    p[1] = ((rgb565 >> 5)  & 0x3F) << 2;   // G (6 bits -> 8)
    p[2] = ( rgb565        & 0x1F) << 3;   // B (5 bits -> 8)
  }
};

NeoCanvas canvas;

// Encode framebuffer -> spiBuf and blast it out MOSI in one DMA-friendly go.
void showFrame() {
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) {
    const uint8_t *fb = &framebuffer[i * 3];
    uint8_t r = (uint16_t)fb[0] * BRIGHTNESS / 255;
    uint8_t g = (uint16_t)fb[1] * BRIGHTNESS / 255;
    uint8_t b = (uint16_t)fb[2] * BRIGHTNESS / 255;
    uint8_t bytes[3] = { g, r, b };        // WS2812 wire order is G, R, B.
    uint8_t *e = &spiBuf[i * 12];
    for (uint8_t bi = 0; bi < 3; bi++) {
      uint8_t byte = bytes[bi];
      e[bi * 4 + 0] = ENC[(byte >> 6) & 0x03];
      e[bi * 4 + 1] = ENC[(byte >> 4) & 0x03];
      e[bi * 4 + 2] = ENC[(byte >> 2) & 0x03];
      e[bi * 4 + 3] = ENC[(byte >> 0) & 0x03];
    }
  }
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  SPI.transferBytes(spiBuf, nullptr, sizeof(spiBuf));
  SPI.endTransaction();
  delayMicroseconds(80);                    // WS2812 reset latch (>= 50 us).
}

const char *MESSAGE =
  "  EMEN ENGINEERING * Electrical & Electronics * "
  "Wiring - Repairs - Installs * Call +254 799 954 672 * Nairobi KE  ";

constexpr int16_t CHAR_CELL_PX = 6;         // 5x7 glyph + 1 spacer.

// EMEN brand palette in rgb565 — red / gold / green / white cycle per lap.
const uint16_t PALETTE[] = { 0xF800, 0xFEA0, 0x07E0, 0xFFFF };
constexpr uint8_t PALETTE_LEN = sizeof(PALETTE) / sizeof(PALETTE[0]);

int16_t   scrollX      = COLS;              // start off-screen right
uint32_t  lastStepMs   = 0;
const uint16_t STEP_MS = 60;                // lower = faster scroll
uint8_t   colorIdx     = 0;

void setup() {
  logBoot("06_ws2812_signboard_spi");
  SPI.begin();                              // Uses VSPI defaults: SCK=18, MOSI=23.
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  memset(framebuffer, 0, sizeof(framebuffer));
  showFrame();
  Serial.printf("WS2812 signboard: %ux%u = %u pixels, SPI-MOSI @ %lu Hz on GPIO 23.\n",
                COLS, ROWS, PIXEL_COUNT, (unsigned long)SPI_HZ);
}

void loop() {
  uint32_t now = millis();
  if (now - lastStepMs < STEP_MS) return;
  lastStepMs = now;

  memset(framebuffer, 0, sizeof(framebuffer));
  canvas.setCursor(scrollX, 0);
  canvas.setTextColor(PALETTE[colorIdx]);
  canvas.print(MESSAGE);                    // GFX clips off-canvas draws.
  showFrame();

  scrollX--;
  int16_t textPxWidth = (int16_t)strlen(MESSAGE) * CHAR_CELL_PX;
  if (scrollX < -textPxWidth) {
    scrollX  = COLS;
    colorIdx = (colorIdx + 1) % PALETTE_LEN;
  }
}
