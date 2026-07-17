// ===========================================================================
//  07_ws2812_status_rmt — WS2812 ring driven via ESP32 RMT (protocol-native)
// ---------------------------------------------------------------------------
//  Companion to 06_ws2812_signboard_spi. Same LED family (WS2812), but here
//  we drive it the "right" way: the ESP32 RMT (Remote Control) peripheral
//  was designed for arbitrary NRZ waveforms, which is exactly what WS2812
//  needs. Adafruit_NeoPixel uses RMT internally on ESP32 — one line of setup,
//  no SPI trickery.
//
//  This is NOT an SPI project — it lives in the SPI monorepo as a protocol
//  comparison: same physical chip, different transport, very different code.
//
//  Hardware: 24-LED WS2812 ring used as an engineering-workshop service
//  status indicator with 5 rotating animation modes:
//      SPINNER    — comet chasing around the ring, "system busy"
//      BREATHING  — amber pulse, "standby / attention"
//      RAINBOW    — hue sweep around the ring, "demo / idle"
//      SERVICE    — red / amber / green, "fault / degraded / nominal"
//      THEATER    — every 3rd pixel marching around, brand-color cycle
//
//  Wiring (Wokwi part: wokwi-led-ring, pins GND / VCC / DIN / DOUT):
//      DIN -> GPIO 5   (any GPIO works — RMT is not pin-locked)
//      VCC -> 5V,  GND -> GND
// ===========================================================================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "emen_serial.h"

constexpr uint8_t  PIXEL_PIN   = 5;
constexpr uint16_t PIXEL_COUNT = 24;
constexpr uint8_t  BRIGHTNESS  = 50;       // 0..255

Adafruit_NeoPixel ring(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

enum Mode : uint8_t {
  MODE_SPINNER, MODE_BREATHING, MODE_RAINBOW, MODE_SERVICE, MODE_THEATER, MODE_COUNT
};
const char *MODE_NAMES[MODE_COUNT] = {
  "spinner", "breathing", "rainbow", "service", "theater"
};

constexpr uint32_t MODE_DURATION_MS = 6000;
constexpr uint16_t FRAME_PERIOD_MS  = 40;      // ~25 fps

Mode      currentMode  = MODE_SPINNER;
uint32_t  modeStartMs  = 0;
uint32_t  lastFrameMs  = 0;
uint16_t  frameIdx     = 0;

// ---- animation implementations -------------------------------------------

// Comet: bright head + fading tail chases around the ring.
void animSpinner() {
  ring.clear();
  const uint8_t TAIL = 8;
  uint16_t head = frameIdx % PIXEL_COUNT;
  for (uint8_t i = 0; i < TAIL; i++) {
    uint16_t pos = (head + PIXEL_COUNT - i) % PIXEL_COUNT;
    uint8_t bright = 255 - i * (255 / TAIL);
    ring.setPixelColor(pos, ring.Color(0, bright, bright / 2));   // teal
  }
}

// Whole ring pulses amber via a sine-wave brightness envelope.
void animBreathing() {
  const float phase = (frameIdx % 60) / 60.0f * (2.0f * PI);
  const float level = (sinf(phase) + 1.0f) * 0.5f;
  const uint8_t v   = (uint8_t)(level * 220.0f);
  const uint32_t c  = ring.Color(v, v / 3, 0);                    // amber
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) ring.setPixelColor(i, c);
}

// Rainbow: each pixel a different hue; hue rotates around the ring over time.
void animRainbow() {
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) {
    uint16_t hue = (frameIdx * 400 + i * (65536u / PIXEL_COUNT)) & 0xFFFF;
    ring.setPixelColor(i, ring.gamma32(ring.ColorHSV(hue)));
  }
}

// Full-ring status color that swaps every ~2 s.
// Red = fault, amber = degraded, green = nominal.
void animService() {
  const uint32_t COLORS[] = { 0xFF0000, 0xFFA500, 0x00C864 };
  uint8_t step = (frameIdx / 50) % 3;
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) ring.setPixelColor(i, COLORS[step]);
}

// Theater-chase: every 3rd pixel lit, marching one step per frame around the
// ring, colour cycles through EMEN brand palette.
void animTheater() {
  ring.clear();
  const uint32_t PALETTE[] = { 0xFF0000, 0xFFC800, 0x00C864, 0xFFFFFF };
  uint32_t c = PALETTE[(frameIdx / 30) % 4];
  uint8_t offset = frameIdx % 3;
  for (uint16_t i = offset; i < PIXEL_COUNT; i += 3) ring.setPixelColor(i, c);
}

void setup() {
  logBoot("07_ws2812_status_rmt");
  ring.begin();
  ring.setBrightness(BRIGHTNESS);
  ring.clear();
  ring.show();
  modeStartMs = millis();
  Serial.printf("WS2812 ring: %u pixels on GPIO %u via RMT. Starting mode: %s\n",
                PIXEL_COUNT, PIXEL_PIN, MODE_NAMES[currentMode]);
}

void loop() {
  const uint32_t now = millis();

  if (now - modeStartMs >= MODE_DURATION_MS) {
    currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
    modeStartMs = now;
    frameIdx    = 0;
    Serial.printf("Mode -> %s\n", MODE_NAMES[currentMode]);
  }

  if (now - lastFrameMs < FRAME_PERIOD_MS) return;
  lastFrameMs = now;

  switch (currentMode) {
    case MODE_SPINNER:   animSpinner();   break;
    case MODE_BREATHING: animBreathing(); break;
    case MODE_RAINBOW:   animRainbow();   break;
    case MODE_SERVICE:   animService();   break;
    case MODE_THEATER:   animTheater();   break;
    default: break;
  }
  ring.show();
  frameIdx++;
}
