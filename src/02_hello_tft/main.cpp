// ===========================================================================
//  02_hello_tft — First SPI graphics: ST7735 128x160 color TFT
// ---------------------------------------------------------------------------
//  Renders a static EMEN Engineering splash using the brand palette. The
//  goal is to prove your SPI wiring works and to introduce Adafruit_GFX
//  primitives on a color surface. No animation loop — everything is drawn
//  once in setup(); loop() is empty.
//
//  Wiring (ESP32 hardware VSPI):
//      SCK  -> GPIO 18   MOSI (SDA) -> GPIO 23
//      CS   -> GPIO 5    DC (A0)    -> GPIO 16
//      RST  -> GPIO 17   LED (BL)   -> 3V3 (always on)
// ===========================================================================

#include <Arduino.h>                       // Core Arduino types + Serial.
#include <SPI.h>                           // Enables hardware SPI on the ESP32.
#include <Adafruit_GFX.h>                  // Base graphics primitives.
#include <Adafruit_ST7735.h>               // ST7735-specific driver.

#include "emen_serial.h"                   // logBoot() — shared helper.
#include "emen_brand.h"                    // EMEN_* palette constants.

#define TFT_CS   5                         // Chip-select for the TFT.
#define TFT_DC   16                        // Data/Command (labelled A0).
#define TFT_RST  17                        // Reset line (pulsed at init).

// Construct the driver. Adafruit_ST7735 uses hardware SPI by default:
// the SCK/MOSI pins come from the platform's default `SPI` object.
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Rotation 1 rotates the panel 90 degrees clockwise -> 160 wide, 128 tall.
static const int16_t W = 160;              // Screen width  after rotation.
static const int16_t H = 128;              // Screen height after rotation.

// ---------------------------------------------------------------------------
// Small helper: draw text centered horizontally at a given y coordinate.
// Reused three times below — captures the getTextBounds -> setCursor pattern.
// ---------------------------------------------------------------------------
void drawCentered(int y, const char* text, uint16_t color, uint8_t size) {
  tft.setTextSize(size);                   // Set size first so bounds is accurate.
  int16_t bx, by;                          // getTextBounds writes back the top-left.
  uint16_t bw, bh;                         // and the pixel width/height.
  tft.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  tft.setCursor((W - (int)bw) / 2, y);     // Horizontal center: (screen - width)/2.
  tft.setTextColor(color, EMEN_BG);        // 2-arg form paints an opaque background.
  tft.print(text);                         // Actually draws the glyphs.
}

// Small helper: paint a two-color "brand ribbon" across the top of the screen.
// Reuses the same fillRect call twice — hoisted so intent is obvious.
void drawBrandRibbon() {
  const int H_TOP = 6;                     // Thin green strip.
  const int H_ACC = 3;                     // Even thinner gold strip beneath.
  tft.fillRect(0, 0,     W, H_TOP,        EMEN_GREEN);
  tft.fillRect(0, H_TOP, W, H_ACC,        EMEN_GOLD);
}

// Draws four small filled squares — one per brand color — as a legend so
// the user can see the palette rendered on the actual panel.
void drawPaletteLegend(int y) {
  struct Swatch { uint16_t color; const char* label; };
  const Swatch swatches[] = {
    { EMEN_GREEN, "GREEN" },
    { EMEN_GOLD,  "GOLD"  },
    { EMEN_BLUE,  "BLUE"  },
    { EMEN_WHITE, "WHITE" },
  };
  const int cellW = W / 4;                 // Divide the screen into 4 columns.
  const int box   = 12;                    // Swatch size in pixels.
  for (int i = 0; i < 4; i++) {            // For each swatch...
    int cx = i * cellW + cellW / 2;        // ...compute the column center.
    tft.fillRect(cx - box / 2, y, box, box, swatches[i].color);   // Draw the box.
    tft.drawRect(cx - box / 2, y, box, box, EMEN_WHITE);           // White outline.
    tft.setTextSize(1);                                            // Small caption.
    tft.setTextColor(EMEN_WHITE, EMEN_BG);
    int16_t bx, by; uint16_t bw, bh;                               // Center label.
    tft.getTextBounds(swatches[i].label, 0, 0, &bx, &by, &bw, &bh);
    tft.setCursor(cx - bw / 2, y + box + 3);
    tft.print(swatches[i].label);
  }
}

// ---------------------------------------------------------------------------
// setup() — runs once at power-on. All rendering happens here.
// ---------------------------------------------------------------------------
void setup() {
  logBoot("02_hello_tft");                 // Serial banner (shared helper).

  tft.initR(INITR_BLACKTAB);               // Init sequence for the common 128x160 board.
  tft.setRotation(1);                      // 1 = landscape, USB port on the left.
  tft.fillScreen(EMEN_BG);                 // Wipe the framebuffer to dark background.

  drawBrandRibbon();                       // Green + gold strips across the top.

  drawCentered(24, "EMEN",         EMEN_GREEN, 3);     // Big brand mark.
  drawCentered(58, "ENGINEERING",  EMEN_GOLD,  1);     // Small subtitle under it.
  drawCentered(72, "AI-first",     EMEN_WHITE, 1);     // Tagline.

  drawPaletteLegend(90);                   // 4 color swatches with labels.

  // Bottom-left "SPI OK" tag as a smoke-test confirmation.
  tft.setTextSize(1);
  tft.setTextColor(EMEN_GREEN, EMEN_BG);
  tft.setCursor(4, H - 10);
  tft.print("SPI OK  //  hello, TFT.");

  Serial.println("Splash drawn.");         // Post-mortem note in the serial log.
}

// ---------------------------------------------------------------------------
// loop() — nothing to do. The panel holds whatever the last draw pushed.
// ---------------------------------------------------------------------------
void loop() {
  // Intentionally empty. TFTs keep their pixels lit without refresh.
}
