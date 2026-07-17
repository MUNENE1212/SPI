// ===========================================================================
//  09_tft_dashboard — EMEN Engineering services kiosk (ILI9341, 320x240)
// ---------------------------------------------------------------------------
//  Rotating showcase of the 8 EMEN Engineering service lines with a hand-
//  drawn icon per service, followed by a tech-stack card, followed by a
//  bouncing-wordmark screensaver. Loops forever.
//
//  Icons are drawn from Adafruit_GFX primitives (rect / circle / line /
//  triangle) — no bitmap uploads, no external assets, everything ships in
//  ~400 bytes of RAM plus the code that draws them.
//
//  State machine:
//     SPLASH   (2.5 s)     — brand card grows in from center
//     SERVICE  (4 s each)  — cycle through the 8 services, one by one
//     STACK    (5 s)       — tech-stack overview (Software / HW / Infra)
//     SAVER    (18 s)      — bouncing wordmark; wakes back to SERVICE 0
//
//  Wiring (ESP32 VSPI + wokwi-ili9341 pin names):
//      VCC -> 3V3       MOSI -> GPIO 23
//      GND -> GND       SCK  -> GPIO 18
//      CS  -> GPIO 5    D/C  -> GPIO 16
//      RST -> GPIO 17   LED  -> 3V3 (backlight always on)
// ===========================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "emen_serial.h"
#include "emen_brand.h"

#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

constexpr int16_t W = 320;
constexpr int16_t H = 240;

// A few accent colors we don't already have in emen_brand.h.
constexpr uint16_t COL_MAGENTA = rgb565(0xFF, 0x40, 0xC0);
constexpr uint16_t COL_CYAN    = rgb565(0x28, 0xD0, 0xE0);
constexpr uint16_t COL_ORANGE  = rgb565(0xFF, 0x88, 0x22);

// ---------------------------------------------------------------------------
// Icon primitives — each draws a ~70x70 pictogram at (x, y). Kept short and
// literal so a reader can eyeball the shape from the code.
// ---------------------------------------------------------------------------
void iconSoftware(int x, int y) {
  tft.drawRoundRect(x, y, 70, 70, 6, EMEN_GREEN);
  tft.fillRect(x + 2, y + 2, 66, 12, EMEN_GREEN);       // title bar
  tft.fillCircle(x + 8,  y + 8, 2, EMEN_BG);            // window dots
  tft.fillCircle(x + 16, y + 8, 2, EMEN_BG);
  tft.fillCircle(x + 24, y + 8, 2, EMEN_BG);
  tft.setTextSize(3);
  tft.setTextColor(EMEN_GREEN);
  tft.setCursor(x + 14, y + 28);
  tft.print(">_");
}

void iconEmbedded(int x, int y) {
  tft.drawRoundRect(x, y, 70, 70, 4, EMEN_GOLD);
  for (int i = 0; i < 4; i++)                           // component grid
    for (int j = 0; j < 4; j++)
      tft.fillCircle(x + 14 + i * 14, y + 14 + j * 14, 2, EMEN_GOLD);
  tft.drawLine(x + 14, y + 14, x + 56, y + 14, EMEN_GOLD);   // traces
  tft.drawLine(x + 14, y + 42, x + 56, y + 42, EMEN_GOLD);
  tft.drawLine(x + 28, y + 14, x + 28, y + 56, EMEN_GOLD);
}

void iconIoT(int x, int y) {
  int cx = x + 35, cy = y + 22;                         // cloud
  tft.fillCircle(cx - 10, cy,     10, EMEN_BLUE);
  tft.fillCircle(cx + 10, cy,     10, EMEN_BLUE);
  tft.fillCircle(cx,      cy - 6, 12, EMEN_BLUE);
  tft.fillRect  (cx - 18, cy,     36, 8, EMEN_BLUE);
  int devY = y + 58;                                    // 3 devices below
  tft.fillCircle(x + 15, devY, 5, EMEN_BLUE);
  tft.fillCircle(x + 35, devY, 5, EMEN_BLUE);
  tft.fillCircle(x + 55, devY, 5, EMEN_BLUE);
  tft.drawLine(cx, cy + 4, x + 15, devY, EMEN_BLUE);
  tft.drawLine(cx, cy + 4, x + 35, devY, EMEN_BLUE);
  tft.drawLine(cx, cy + 4, x + 55, devY, EMEN_BLUE);
}

void iconAI(int x, int y) {                             // neural net
  const int cxA = x + 15, cxB = x + 35, cxC = x + 55;
  const int rows[3] = { y + 12, y + 35, y + 58 };
  for (int i = 0; i < 3; i++) {
    tft.drawCircle(cxA, rows[i], 5, COL_MAGENTA);
    tft.drawCircle(cxB, rows[i], 5, COL_MAGENTA);
    tft.drawCircle(cxC, rows[i], 5, COL_MAGENTA);
  }
  for (int i = 0; i < 2; i++)
    for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++) {
        int ax = (a == 0) ? cxA : (a == 1 ? cxB : cxC);
        int bx = (b == 0) ? cxA : (b == 1 ? cxB : cxC);
        tft.drawLine(ax, rows[i] + 5, bx, rows[i + 1] - 5, COL_MAGENTA);
      }
}

void iconWeb(int x, int y) {                            // browser window
  tft.drawRoundRect(x, y, 70, 70, 4, EMEN_GREEN);
  tft.fillRect(x + 2, y + 2, 66, 10, EMEN_GREEN);
  tft.fillCircle(x + 8,  y + 7, 2, EMEN_BG);
  tft.fillCircle(x + 16, y + 7, 2, EMEN_BG);
  tft.fillCircle(x + 24, y + 7, 2, EMEN_BG);
  for (int i = 0; i < 5; i++) {
    int w = 60 - i * 6;
    tft.drawFastHLine(x + 6, y + 22 + i * 8, w, EMEN_WHITE);
  }
}

void iconMobile(int x, int y) {                         // phone silhouette
  tft.drawRoundRect(x + 18, y + 2, 34, 66, 5, EMEN_GOLD);
  tft.fillRect(x + 20, y + 10, 30, 46, EMEN_GOLD);
  tft.fillCircle(x + 35, y + 63, 2, EMEN_GOLD);
  tft.drawFastHLine(x + 30, y + 6, 10, EMEN_GOLD);      // speaker slit
}

void iconWhatsApp(int x, int y) {                       // chat bubble
  tft.fillRoundRect(x + 4, y + 4, 62, 46, 10, EMEN_GREEN);
  tft.fillTriangle(x + 20, y + 44, x + 20, y + 60, x + 34, y + 46, EMEN_GREEN);
  tft.drawFastHLine(x + 12, y + 18, 48, EMEN_BG);
  tft.drawFastHLine(x + 12, y + 26, 40, EMEN_BG);
  tft.drawFastHLine(x + 12, y + 34, 44, EMEN_BG);
}

void iconCloud(int x, int y) {                          // cloud + gear
  int cx = x + 30, cy = y + 22;
  tft.fillCircle(cx - 10, cy,     10, EMEN_WHITE);
  tft.fillCircle(cx + 10, cy,     10, EMEN_WHITE);
  tft.fillCircle(cx,      cy - 6, 12, EMEN_WHITE);
  tft.fillRect  (cx - 18, cy,     36, 8, EMEN_WHITE);
  int gx = x + 50, gy = y + 52;                         // gear silhouette
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    int tx = gx + (int)(11 * cosf(a));
    int ty = gy + (int)(11 * sinf(a));
    tft.fillCircle(tx, ty, 3, EMEN_GOLD);
  }
  tft.fillCircle(gx, gy, 8, EMEN_GOLD);
  tft.fillCircle(gx, gy, 3, EMEN_BG);
}

// A small lightning bolt used as the header brand mark.
void drawBrandBolt(int x, int y, uint16_t color) {
  tft.fillTriangle(x + 6, y,     x + 0,  y + 10, x + 8,  y + 10, color);
  tft.fillTriangle(x + 8, y + 18, x + 12, y + 8,  x + 4,  y + 8,  color);
}

// ---------------------------------------------------------------------------
// Service catalogue — the data table drives what each frame draws.
// ---------------------------------------------------------------------------
struct Service {
  const char *title;
  const char *tagline;
  const char *bullets[4];
  uint16_t    accent;
  void      (*icon)(int, int);
};

const Service SERVICES[] = {
  { "SOFTWARE",  "Custom-built solutions for your business",
    { "Web Apps", "Desktop", "APIs", "Integration" },
    EMEN_GREEN, iconSoftware },

  { "EMBEDDED",  "MCU firmware + PCB integration",
    { "Arduino/ESP32", "Raspberry Pi", "Sensors", "Firmware" },
    EMEN_GOLD, iconEmbedded },

  { "IoT",       "Connected devices, live dashboards",
    { "Monitoring", "MQTT/WSock", "Sensor Nets", "Dashboards" },
    EMEN_BLUE, iconIoT },

  { "AI / ML",   "Intelligent automation + insights",
    { "Analytics", "NLP", "Vision", "Automation" },
    COL_MAGENTA, iconAI },

  { "WEB DEV",   "Modern responsive web apps",
    { "React/Next", "Full-Stack", "E-commerce", "PWAs" },
    EMEN_GREEN, iconWeb },

  { "MOBILE",    "Cross-platform mobile apps",
    { "React Native", "iOS/Android", "UI/UX", "Maintenance" },
    EMEN_GOLD, iconMobile },

  { "WHATSAPP",  "Chatbots + social automation",
    { "Chatbots", "Social Auto", "Engagement", "Workflows" },
    EMEN_GREEN, iconWhatsApp },

  { "CLOUD",     "Scalable infra + DevOps",
    { "AWS/Azure/GCP", "Docker/CI/CD", "Linux", "Monitoring" },
    COL_CYAN, iconCloud },
};
constexpr uint8_t N_SERVICES = sizeof(SERVICES) / sizeof(SERVICES[0]);

// ---------------------------------------------------------------------------
// Header: brand bolt + wordmark on the left, section label on the right.
// ---------------------------------------------------------------------------
void drawHeader(const char *sectionLabel) {
  tft.fillRect(0, 0, W, 30, EMEN_GREEN);
  drawBrandBolt(10, 6, EMEN_GOLD);
  tft.setTextSize(2);
  tft.setTextColor(EMEN_GOLD, EMEN_GREEN);
  tft.setCursor(28, 8);
  tft.print("EMEN ENGINEERING");
  tft.setTextSize(1);
  tft.setTextColor(EMEN_WHITE, EMEN_GREEN);
  int16_t lx = W - (int16_t)(strlen(sectionLabel) * 6) - 8;
  tft.setCursor(lx, 12);
  tft.print(sectionLabel);
}

// Footer: 8 page dots on the left, N/M counter on the right.
void drawFooter(uint8_t page, uint8_t total) {
  tft.fillRect(0, 210, W, 30, EMEN_BG);
  for (uint8_t i = 0; i < total; i++) {
    uint16_t c = (i == page) ? EMEN_GOLD : EMEN_GREY;
    tft.fillCircle(14 + i * 14, 224, 4, c);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%u / %u", page + 1, total);
  tft.setTextSize(2);
  tft.setTextColor(EMEN_WHITE, EMEN_BG);
  tft.setCursor(W - 60, 217);
  tft.print(buf);
}

// ---------------------------------------------------------------------------
// One service page: header + icon + title + tagline + bulletpoints + footer.
// ---------------------------------------------------------------------------
void drawServicePage(uint8_t idx) {
  const Service &s = SERVICES[idx];
  tft.fillScreen(EMEN_BG);
  drawHeader("SERVICES");

  s.icon(15, 45);                                       // 70x70 icon

  tft.setTextSize(3);
  tft.setTextColor(s.accent, EMEN_BG);
  tft.setCursor(100, 45);
  tft.print(s.title);

  tft.setTextSize(1);
  tft.setTextColor(EMEN_WHITE, EMEN_BG);
  tft.setCursor(100, 78);
  tft.print(s.tagline);

  tft.setTextSize(2);
  for (int i = 0; i < 4; i++) {
    tft.setTextColor(s.accent, EMEN_BG);
    tft.setCursor(100, 100 + i * 22);
    tft.print(">");
    tft.setTextColor(EMEN_WHITE, EMEN_BG);
    tft.setCursor(115, 100 + i * 22);
    tft.print(s.bullets[i]);
  }

  drawFooter(idx, N_SERVICES);
}

// ---------------------------------------------------------------------------
// Tech-stack summary page — three columns.
// ---------------------------------------------------------------------------
void drawStackPage() {
  tft.fillScreen(EMEN_BG);
  drawHeader("TECH STACK");

  struct Col { const char *title; uint16_t color; const char *items[6]; };
  const Col cols[3] = {
    { "SOFTWARE", EMEN_GREEN,
      { "React", "Next.js", "Node.js", "Python", "MongoDB", "Postgres" } },
    { "HARDWARE", EMEN_GOLD,
      { "Arduino", "Raspberry Pi", "C/C++", "Python", "MQTT", "" } },
    { "INFRA",    COL_CYAN,
      { "Docker", "Linux", "Nginx", "Redis", "Cloudinary", "" } },
  };

  for (int c = 0; c < 3; c++) {
    int x = 12 + c * 105;
    tft.setTextSize(2);
    tft.setTextColor(cols[c].color, EMEN_BG);
    tft.setCursor(x, 45);
    tft.print(cols[c].title);
    tft.setTextSize(1);
    tft.setTextColor(EMEN_WHITE, EMEN_BG);
    for (int r = 0; r < 6; r++) {
      if (cols[c].items[r][0] == '\0') break;
      tft.setCursor(x, 75 + r * 18);
      tft.print(cols[c].items[r]);
    }
  }

  tft.fillRect(0, 210, W, 30, EMEN_BG);
  tft.setTextSize(1);
  tft.setTextColor(EMEN_WHITE, EMEN_BG);
  tft.setCursor(12, 224);
  tft.print("Production-proven across our client projects");
}

// ---------------------------------------------------------------------------
// Splash — brand card fades in from center. Two-frame anim so the demo boot
// doesn't feel dead.
// ---------------------------------------------------------------------------
void drawSplash() {
  tft.fillScreen(EMEN_BG);
  for (int s = 40; s <= 140; s += 20) {                 // growing "reveal"
    int x = (W - s * 2) / 2;
    int y = (H - s) / 2;
    tft.drawRoundRect(x, y, s * 2, s, 8, EMEN_GREEN);
    delay(40);
  }
  int cardW = 240, cardH = 100;
  int cx = (W - cardW) / 2;
  int cy = (H - cardH) / 2;
  tft.fillRoundRect(cx, cy, cardW, cardH, 10, EMEN_GREEN);
  drawBrandBolt(cx + 20, cy + 30, EMEN_GOLD);
  tft.setTextSize(3);
  tft.setTextColor(EMEN_GOLD, EMEN_GREEN);
  tft.setCursor(cx + 44, cy + 26);
  tft.print("EMEN");
  tft.setTextSize(2);
  tft.setTextColor(EMEN_WHITE, EMEN_GREEN);
  tft.setCursor(cx + 44, cy + 60);
  tft.print("ENGINEERING");
}

// ---------------------------------------------------------------------------
// Screensaver — bouncing wordmark card. Selective erase + repaint so we
// don't flash the whole screen every frame.
// ---------------------------------------------------------------------------
constexpr int16_t SAVER_W = 140;
constexpr int16_t SAVER_H = 46;
int16_t   saverX, saverY, saverDx, saverDy;
uint16_t  saverColor;

const uint16_t SAVER_PALETTE[] = {
  EMEN_GREEN, EMEN_GOLD, EMEN_BLUE, COL_MAGENTA, COL_CYAN, COL_ORANGE
};
constexpr uint8_t SAVER_PALETTE_LEN = sizeof(SAVER_PALETTE) / sizeof(SAVER_PALETTE[0]);
uint8_t saverColorIdx = 0;

void drawSaverCard(int16_t x, int16_t y, uint16_t color) {
  tft.fillRoundRect(x, y, SAVER_W, SAVER_H, 8, color);
  tft.setTextSize(3);
  tft.setTextColor(EMEN_BG);
  tft.setCursor(x + 22, y + 12);
  tft.print("EMEN");
}

void initScreensaver() {
  tft.fillScreen(EMEN_BG);
  saverX = 40;  saverY = 60;
  saverDx = 3;  saverDy = 2;
  saverColorIdx = 0;
  saverColor = SAVER_PALETTE[saverColorIdx];
  drawSaverCard(saverX, saverY, saverColor);
}

void stepScreensaver() {
  // Erase where the card WAS.
  tft.fillRect(saverX, saverY, SAVER_W, SAVER_H, EMEN_BG);

  saverX += saverDx;
  saverY += saverDy;

  bool bounced = false;
  if (saverX <= 0)             { saverX = 0;             saverDx = -saverDx; bounced = true; }
  if (saverX + SAVER_W >= W)   { saverX = W - SAVER_W;   saverDx = -saverDx; bounced = true; }
  if (saverY <= 0)             { saverY = 0;             saverDy = -saverDy; bounced = true; }
  if (saverY + SAVER_H >= H)   { saverY = H - SAVER_H;   saverDy = -saverDy; bounced = true; }

  if (bounced) {
    saverColorIdx = (saverColorIdx + 1) % SAVER_PALETTE_LEN;
    saverColor = SAVER_PALETTE[saverColorIdx];
  }

  drawSaverCard(saverX, saverY, saverColor);
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum State : uint8_t { ST_SPLASH, ST_SERVICE, ST_STACK, ST_SAVER };

State    state         = ST_SPLASH;
uint8_t  serviceIdx    = 0;
uint32_t stateStartMs  = 0;
uint32_t lastStepMs    = 0;

constexpr uint32_t SPLASH_MS  = 2500;
constexpr uint32_t SERVICE_MS = 4000;
constexpr uint32_t STACK_MS   = 5000;
constexpr uint32_t SAVER_MS   = 18000;
constexpr uint16_t SAVER_FRAME_MS = 40;

void setup() {
  logBoot("09_tft_dashboard");
  SPI.begin(/*SCK=*/18, /*MISO=*/-1, /*MOSI=*/23, /*SS=*/-1);
  tft.begin();
  tft.setRotation(1);
  drawSplash();
  stateStartMs = millis();
  Serial.println("EMEN services kiosk running (ILI9341, 320x240).");
}

void loop() {
  const uint32_t now = millis();

  switch (state) {
    case ST_SPLASH:
      if (now - stateStartMs >= SPLASH_MS) {
        state = ST_SERVICE;
        serviceIdx = 0;
        drawServicePage(serviceIdx);
        stateStartMs = now;
      }
      break;

    case ST_SERVICE:
      if (now - stateStartMs >= SERVICE_MS) {
        serviceIdx++;
        if (serviceIdx >= N_SERVICES) {
          state = ST_STACK;
          drawStackPage();
        } else {
          drawServicePage(serviceIdx);
        }
        stateStartMs = now;
      }
      break;

    case ST_STACK:
      if (now - stateStartMs >= STACK_MS) {
        state = ST_SAVER;
        initScreensaver();
        stateStartMs = now;
        lastStepMs   = now;
      }
      break;

    case ST_SAVER:
      if (now - stateStartMs >= SAVER_MS) {
        state = ST_SERVICE;
        serviceIdx = 0;
        drawServicePage(serviceIdx);
        stateStartMs = now;
      } else if (now - lastStepMs >= SAVER_FRAME_MS) {
        stepScreensaver();
        lastStepMs = now;
      }
      break;
  }
}
