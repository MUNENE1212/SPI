// ===========================================================================
//  08_ws2812_advert_rmt — EMEN Engineering advert, driven the "right" way
// ---------------------------------------------------------------------------
//  Same 8x64 pixel WS2812 signboard as 06_ws2812_signboard_spi, but here we
//  drive it via the ESP32 RMT peripheral instead of abusing SPI-MOSI. RMT is
//  Espressif's remote-control (infrared-style) hardware — it generates
//  arbitrary NRZ waveforms with per-bit timing, which is *exactly* what
//  WS2812 asks for. Adafruit_NeoPixel picks RMT automatically on ESP32.
//
//  What's new vs 06: this project layers a per-message color STYLE on top of
//  the text rasterizer. Every style stays fully saturated — visibility from
//  across a workshop floor is the goal, so no washed-out gradient blends and
//  the sine "breathe" never drops below ~55% brightness.
//      SOLID     — one flat color
//      RAINBOW   — fully-saturated hue swept horizontally, animated
//      PALETTE   — each screen column cycles through the EMEN brand palette
//      BREATHE   — solid color with a shallow brightness pulse (stays legible)
//      SPARKLE   — solid color with random white flashes on text pixels
//      BLINK     — solid color that flashes off/on every ~500 ms — attention
//      ALTERNATE — two bold colors on odd/even character columns
//
//  Two-pass render each frame:
//    1) Rasterize the visible text into a 1-bit "mask" using Adafruit_GFX
//       (subclass writes 0/1 into a byte array, not RGB).
//    2) For each mask=1 pixel, ask the current style function for its color.
//
//  Wiring: DIN -> GPIO 4  (any GPIO — RMT isn't pin-locked)
//          VDD -> 5V,  VSS -> GND
// ===========================================================================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include "emen_serial.h"

constexpr uint16_t COLS         = 64;
constexpr uint16_t ROWS         = 8;
constexpr uint16_t PIXEL_COUNT  = COLS * ROWS;
constexpr uint8_t  DATA_PIN     = 4;
constexpr uint8_t  BRIGHTNESS   = 80;         // 0..255 — punchy for the sim.
                                              // Real 8x64 fitting: drop to 40
                                              // and use a 5 V / 5 A supply.
constexpr uint16_t FRAME_MS     = 40;         // scroll speed / frame period

Adafruit_NeoPixel strip(PIXEL_COUNT, DATA_PIN, NEO_GRB + NEO_KHZ800);

// Progressive row-major mapping (matches Wokwi's wokwi-led-matrix default).
inline uint16_t xyToIndex(int16_t x, int16_t y) {
  if (x < 0 || x >= (int16_t)COLS) return 0xFFFF;
  if (y < 0 || y >= (int16_t)ROWS) return 0xFFFF;
  return (uint16_t)y * COLS + (uint16_t)x;
}

// 1-bit-per-pixel canvas: drawPixel() writes 0/1 into an 8-bit mask instead
// of a color buffer. This lets us reuse Adafruit_GFX's built-in 5x7 font and
// print() plumbing to produce a stencil we can colorize any way we like.
class MaskCanvas : public Adafruit_GFX {
 public:
  uint8_t mask[COLS * ROWS];
  MaskCanvas() : Adafruit_GFX(COLS, ROWS) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    mask[y * COLS + x] = (color != 0) ? 1 : 0;
  }
  void reset() { memset(mask, 0, sizeof(mask)); }
};

MaskCanvas canvas;

// ---------- color styles ---------------------------------------------------

enum Style : uint8_t {
  STYLE_SOLID, STYLE_RAINBOW, STYLE_PALETTE,
  STYLE_BREATHE, STYLE_SPARKLE, STYLE_BLINK, STYLE_ALTERNATE
};

// Brand palette used by PALETTE style. Fully-saturated primaries + white so
// nothing on the sign ever looks muddy.
const uint32_t BRAND[] = { 0xFF0000, 0xFFC800, 0x00FF00, 0xFFFFFF };
constexpr uint8_t BRAND_LEN = sizeof(BRAND) / sizeof(BRAND[0]);

static uint32_t scaleRgb(uint32_t rgb, float k) {
  uint8_t r = ((rgb >> 16) & 0xFF) * k;
  uint8_t g = ((rgb >>  8) & 0xFF) * k;
  uint8_t b = ( rgb        & 0xFF) * k;
  return strip.Color(r, g, b);
}

// Cheap deterministic hash for sparkle randomness.
static uint32_t hash3(int x, int y, uint32_t t) {
  uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ (t * 83492791u);
  return h ^ (h >> 13);
}

// Returns the color for a *lit* text pixel at (x, y). Background pixels
// (mask == 0) are always black; caller handles those.
uint32_t colorForPixel(int16_t x, int16_t y, Style s, uint32_t a, uint32_t b,
                       uint32_t frame) {
  switch (s) {
    case STYLE_SOLID:
      return a;

    case STYLE_RAINBOW: {
      // Force full saturation + max value so hues stay punchy at every column.
      uint16_t hue = (uint16_t)((x * 1200) + (frame * 400)) & 0xFFFF;
      return strip.ColorHSV(hue, 255, 255);
    }

    case STYLE_PALETTE: {
      // Cycle through the brand palette per character-cell width, and shift
      // slowly over time — "digital ticker" feel, letters march past color bars.
      uint8_t idx = ((x / 6) + (frame / 4)) % BRAND_LEN;
      return BRAND[idx];
    }

    case STYLE_BREATHE: {
      // Sine envelope, but clamped so brightness stays in [0.55, 1.0] — the
      // message never fades enough to become unreadable.
      float phase = (frame % 40) / 40.0f * (2.0f * PI);
      float k     = 0.55f + 0.45f * (sinf(phase) + 1.0f) * 0.5f;
      return scaleRgb(a, k);
    }

    case STYLE_SPARKLE: {
      // ~1-in-16 chance of a bright white flash per lit pixel per few frames.
      if ((hash3(x, y, frame / 3) & 0x0F) < 2) return 0xFFFFFF;
      return a;
    }

    case STYLE_BLINK: {
      // ~360 ms on, ~360 ms off — 9 frames per phase at FRAME_MS=40.
      return ((frame / 9) & 1) ? 0 : a;
    }

    case STYLE_ALTERNATE: {
      // Two bold colors on odd/even character columns (6 px wide).
      return ((x / 6) & 1) ? b : a;
    }
  }
  return 0;
}

// ---------- content script -------------------------------------------------

struct Message {
  const char *text;
  Style       style;
  uint32_t    a;      // primary color (also gradient start)
  uint32_t    b;      // secondary color (gradient end / unused)
  uint16_t    padPx;  // extra blank pixels after the message before the next
};

const Message SCRIPT[] = {
  { "EMEN ENGINEERING",             STYLE_RAINBOW,   0,        0,        24 },
  { "Electrical & Electronics",     STYLE_ALTERNATE, 0xFF0000, 0x00FF00, 24 },
  { "Wiring - Repairs - Installs",  STYLE_PALETTE,   0,        0,        24 },
  { "24/7 SUPPORT",                 STYLE_BLINK,     0xFFC800, 0,        24 },
  { "Nairobi, KE",                  STYLE_SPARKLE,   0x00FF00, 0,        24 },
  { "Call +254 799 954 672",        STYLE_ALTERNATE, 0xFFFFFF, 0xFF0000, 24 },
  { "BAITECH Solutions",            STYLE_BREATHE,   0x00CCFF, 0,        24 },
};
constexpr uint8_t SCRIPT_LEN = sizeof(SCRIPT) / sizeof(SCRIPT[0]);
constexpr int16_t CHAR_CELL_PX = 6;      // 5-pixel glyph + 1-pixel spacer

uint8_t   msgIdx    = 0;
int16_t   scrollX   = COLS;              // pixel offset of the message's first char
uint32_t  lastMs    = 0;
uint32_t  frameIdx  = 0;

int16_t currentTextWidth() {
  return (int16_t)strlen(SCRIPT[msgIdx].text) * CHAR_CELL_PX;
}

void renderFrame() {
  const Message &m = SCRIPT[msgIdx];

  // Pass 1: rasterize text into the mask.
  canvas.reset();
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.setCursor(scrollX, 0);
  canvas.setTextColor(1);                // any non-zero flags the pixel as text
  canvas.print(m.text);

  // Pass 2: colorize.
  for (int16_t y = 0; y < (int16_t)ROWS; y++) {
    for (int16_t x = 0; x < (int16_t)COLS; x++) {
      uint16_t idx = xyToIndex(x, y);
      if (canvas.mask[y * COLS + x]) {
        uint32_t rgb = colorForPixel(x, y, m.style, m.a, m.b, frameIdx);
        strip.setPixelColor(idx, rgb);
      } else {
        strip.setPixelColor(idx, 0);     // background always black
      }
    }
  }
  strip.show();
}

void setup() {
  logBoot("08_ws2812_advert_rmt");
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
  Serial.printf("WS2812 advert (RMT): %ux%u = %u pixels on GPIO %u.\n",
                COLS, ROWS, PIXEL_COUNT, DATA_PIN);
}

void loop() {
  uint32_t now = millis();
  if (now - lastMs < FRAME_MS) return;
  lastMs = now;

  renderFrame();
  scrollX--;
  frameIdx++;

  // When the trailing edge of the current message has passed the left edge,
  // plus the message's own pad, advance to the next.
  if (scrollX < -(currentTextWidth() + SCRIPT[msgIdx].padPx)) {
    msgIdx  = (msgIdx + 1) % SCRIPT_LEN;
    scrollX = COLS;
  }
}
