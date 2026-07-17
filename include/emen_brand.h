// emen_brand.h — EMEN Engineering brand palette in RGB565.
// ---------------------------------------------------------------------------
// RGB565 packs a color into 16 bits (5 red, 6 green, 5 blue) — the format
// most SPI TFT drivers (ST77xx, ILI9341, etc.) expect. constexpr means the
// values are computed at compile time, so they sit in flash, not RAM.

#pragma once
#include <stdint.h>              // uint8_t / uint16_t.

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  // Drop the low 3 bits of R (keep top 5), low 2 bits of G (keep top 6),
  // low 3 bits of B (keep top 5), then shift into place.
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t EMEN_BG    = rgb565(0x0A, 0x0A, 0x0A);   // Near-black background.
constexpr uint16_t EMEN_GREEN = rgb565(0x10, 0xB9, 0x81);   // Brand primary (#10B981).
constexpr uint16_t EMEN_GOLD  = rgb565(0xFF, 0xD7, 0x00);   // Brand accent  (#FFD700).
constexpr uint16_t EMEN_BLUE  = rgb565(0x3B, 0x82, 0xF6);   // Brand support (#3B82F6).
constexpr uint16_t EMEN_GREY  = rgb565(0x52, 0x52, 0x52);   // Muted UI grey.
constexpr uint16_t EMEN_WHITE = 0xFFFF;                     // Pure white text.
constexpr uint16_t EMEN_BLACK = 0x0000;                     // Pure black.
