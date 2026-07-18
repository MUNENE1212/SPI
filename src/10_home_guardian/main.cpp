// ===========================================================================
//  10_home_guardian — EMEN home security + smart lighting kiosk (interactive)
// ---------------------------------------------------------------------------
//  Full-duplex UI on ILI9341 (output) + 3 pushbuttons (input) — the display
//  is now for in-depth monitoring AND operator control: arm/disarm, tune
//  alarm thresholds, override lighting modes, browse the event log, read
//  diagnostics.  No touchscreen support in Wokwi's ILI9341, so buttons
//  drive a small menu system with short + long press semantics.
//
//  Buttons (all INPUT_PULLUP, pressed = LOW):
//    ARM     (GPIO 4)  short = cycle arm state; long = disarm / reset alarm
//    MENU    (GPIO 33) short = enter menu / next item; long = back / exit
//    SELECT  (GPIO 39) short = enter menu / confirm; long = back / cancel
//    (ARM works anywhere in the UI so panic-disarm is one press away.)
//
//  Sensors: HC-SR04, 2x PIR, MQ2, DS18B20 (1-Wire), NTC (analog),
//           LDR (analog), BMP180 (I2C).
//  Outputs: WS2812 24-px strip (alarm px 0-7, perimeter 8-14, gate px 15,
//           interior 16-23), piezo buzzer, TFT UI.
//  Persistence: microSD holds /env.csv + /events.csv.
//
//  Energy: every decorative light is motion-gated + LDR-gated. Interior
//  fades in 0.5 s, holds 30 s from last motion, then ramps down over 3 s.
//  Gate pixel is instant on/off, 60 s hold. Perimeter heartbeat flashes
//  once every 10 s so the strip proves the system is alive without
//  burning power.
// ===========================================================================

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <esp_log.h>
#include <FS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>
#include "emen_serial.h"
#include "emen_brand.h"

// -------------------------------------------- pins
constexpr uint8_t PIN_WS2812     = 2;
constexpr uint8_t PIN_ARM_BTN    = 4;
constexpr uint8_t PIN_SD_CS      = 5;
constexpr uint8_t PIN_HC_TRIG    = 13;
constexpr uint8_t PIN_HC_ECHO    = 14;
constexpr uint8_t PIN_DS18B20    = 15;
constexpr uint8_t PIN_TFT_DC     = 16;
constexpr uint8_t PIN_TFT_RST    = 17;
constexpr uint8_t PIN_SCK        = 18;
constexpr uint8_t PIN_MISO       = 19;
constexpr uint8_t PIN_SDA        = 21;
constexpr uint8_t PIN_SCL        = 22;
constexpr uint8_t PIN_MOSI       = 23;
constexpr uint8_t PIN_PIR_OUT    = 25;
constexpr uint8_t PIN_BUZZER     = 26;
constexpr uint8_t PIN_PIR_IN     = 27;
constexpr uint8_t PIN_TFT_CS     = 32;
constexpr uint8_t PIN_MENU_BTN   = 33;
constexpr uint8_t PIN_LDR        = 34;
constexpr uint8_t PIN_MQ2        = 35;
constexpr uint8_t PIN_NTC        = 36;
constexpr uint8_t PIN_SELECT_BTN = 0;    // GPIO 39 has no internal pull-up
                                         // (input-only ADC1 pin). GPIO 0 has
                                         // one AND is required HIGH at boot,
                                         // which INPUT_PULLUP guarantees.

// -------------------------------------------- WS2812 zones (24 px total)
constexpr uint16_t PIXEL_COUNT   = 24;
constexpr uint16_t ALARM_START   = 0,  ALARM_END   = 7;
constexpr uint16_t PERIM_START   = 8,  PERIM_END   = 14;
constexpr uint16_t GATE_PIXEL    = 15;
constexpr uint16_t INTER_START   = 16, INTER_END   = 23;

// -------------------------------------------- editable thresholds (defaults)
uint16_t th_gas_alarm     = 1500;    // MQ2 raw
float    th_fire_temp     = 45.0f;   // C
uint16_t th_gate_open_cm  = 100;     // HC-SR04 cm
uint16_t th_ldr_night     = 1500;    // day -> night when raw drops below
uint16_t th_ldr_day       = 2500;    // night -> day when raw rises above

// -------------------------------------------- timings
constexpr uint32_t SENSOR_POLL_MS       = 250;
constexpr uint32_t SLOW_POLL_MS         = 2000;
constexpr uint32_t TFT_UPDATE_MS        = 300;
constexpr uint32_t LIGHTING_TICK_MS     = 33;
constexpr uint32_t ENV_LOG_MS           = 60000;
constexpr uint32_t GATE_HOLD_MS         = 60000;
constexpr uint32_t INTERIOR_HOLD_MS     = 30000;
constexpr uint32_t INTERIOR_FADE_MS     = 3000;
constexpr uint32_t INTERIOR_FADE_IN_MS  = 500;
constexpr uint32_t HEARTBEAT_PERIOD_MS  = 10000;
constexpr uint32_t HEARTBEAT_FLASH_MS   = 100;
constexpr uint32_t LONGPRESS_MS         = 800;
constexpr uint32_t ALARM_PULSE_MS       = 250;

// -------------------------------------------- hardware objects
Adafruit_ILI9341  tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
Adafruit_NeoPixel strip(PIXEL_COUNT, PIN_WS2812, NEO_GRB + NEO_KHZ800);
OneWire           ow(PIN_DS18B20);
DallasTemperature ds(&ow);
Adafruit_BMP085   bmp;
bool sdReady = false, bmpReady = false;

// -------------------------------------------- state
enum ArmState  : uint8_t { STATE_DISARMED, STATE_ARMED_HOME, STATE_ARMED_AWAY, STATE_ALARM };
const char *STATE_NAMES[4]    = { "DISARMED", "ARMED HOME", "ARMED AWAY", "ALARM" };
const uint16_t STATE_COLORS[4] = { EMEN_GREEN, EMEN_GOLD, EMEN_GOLD, rgb565(255,40,20) };

enum AlarmCause : uint8_t { CAUSE_NONE, CAUSE_INTRUSION, CAUSE_GAS, CAUSE_FIRE };
const char *CAUSE_NAMES[4] = { "NONE", "INTRUSION", "GAS", "FIRE" };

enum LightMode : uint8_t { LIGHT_AUTO, LIGHT_ON, LIGHT_OFF };
const char *LIGHT_NAMES[3] = { "AUTO", "ON", "OFF" };
LightMode interior_mode = LIGHT_AUTO;
LightMode gate_mode     = LIGHT_AUTO;

struct Sensors {
  float    temp_ds18 = NAN, temp_ntc = NAN, press_hPa = NAN;
  uint16_t distance_cm = 400, ldr_raw = 0, mq2_raw = 0;
  bool     pir_out = false, pir_in = false, is_night = false, gate_open = false;
};
Sensors sensor;

ArmState   arm_state    = STATE_DISARMED;
AlarmCause alarm_cause  = CAUSE_NONE;

uint32_t last_pir_out_ms = 0, last_pir_in_ms = 0;
uint8_t  interior_brightness = 0, interior_target = 0;
bool     gate_led_state = false;
uint32_t last_heartbeat_ms = 0;
bool     heartbeat_active = false;
uint32_t last_alarm_pulse_ms = 0;
bool     alarm_pulse_on = false;

uint32_t last_sensor_ms = 0, last_slow_ms = 0, last_tft_ms = 0;
uint32_t last_light_ms  = 0, last_env_ms  = 0;

// -------------------------------------------- event ring buffer (for logs UI)
struct LogRow { uint32_t ms; char text[36]; };
constexpr uint8_t LOG_RING_LEN = 8;
LogRow log_ring[LOG_RING_LEN];
uint8_t log_head = 0, log_count = 0;

void logRingAdd(const char *category, const char *detail) {
  LogRow &r = log_ring[log_head];
  r.ms = millis();
  snprintf(r.text, sizeof(r.text), "%s: %s", category, detail);
  log_head = (log_head + 1) % LOG_RING_LEN;
  if (log_count < LOG_RING_LEN) log_count++;
}

// -------------------------------------------- SD log
void logEvent(const char *category, const char *detail) {
  Serial.printf("[EVENT %s] %s\n", category, detail);
  logRingAdd(category, detail);
  if (!sdReady) return;
  File f = SD.open("/events.csv", FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%s,%s\n", (unsigned long)millis(), category, detail);
  f.close();
}

void logEnv() {
  Serial.printf("[ENV] ds=%.1fC ntc=%.1fC p=%.1fhPa d=%ucm ldr=%u mq2=%u\n",
    sensor.temp_ds18, sensor.temp_ntc, sensor.press_hPa,
    sensor.distance_cm, sensor.ldr_raw, sensor.mq2_raw);
  if (!sdReady) return;
  File f = SD.open("/env.csv", FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%.1f,%.1f,%.1f,%u,%u,%u\n", (unsigned long)millis(),
    sensor.temp_ds18, sensor.temp_ntc, sensor.press_hPa,
    sensor.distance_cm, sensor.ldr_raw, sensor.mq2_raw);
  f.close();
}

// -------------------------------------------- sensor readers
float readNtcCelsius() {
  uint16_t raw = analogRead(PIN_NTC);
  if (raw == 0 || raw >= 4095) return NAN;
  float R = 10000.0f * (4095.0f - (float)raw) / (float)raw;
  const float BETA = 3950.0f, R25 = 10000.0f, T25 = 25.0f + 273.15f;
  float invT = 1.0f / T25 + logf(R / R25) / BETA;
  return 1.0f / invT - 273.15f;
}

uint16_t readDistanceCm() {
  digitalWrite(PIN_HC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_HC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_HC_TRIG, LOW);
  unsigned long dur = pulseIn(PIN_HC_ECHO, HIGH, 30000UL);
  if (dur == 0) return 400;
  return (uint16_t)(dur / 58);
}

void updateDayNight() {
  if (sensor.is_night) {
    if (sensor.ldr_raw > th_ldr_day)   sensor.is_night = false;
  } else {
    if (sensor.ldr_raw < th_ldr_night) sensor.is_night = true;
  }
}

void pollFastSensors() {
  sensor.ldr_raw     = analogRead(PIN_LDR);
  sensor.mq2_raw     = analogRead(PIN_MQ2);
  sensor.temp_ntc    = readNtcCelsius();
  sensor.pir_out     = digitalRead(PIN_PIR_OUT) == HIGH;
  sensor.pir_in      = digitalRead(PIN_PIR_IN)  == HIGH;
  sensor.distance_cm = readDistanceCm();
  sensor.gate_open   = sensor.distance_cm < th_gate_open_cm;
  updateDayNight();
}

void pollSlowSensors() {
  ds.requestTemperatures();
  float t = ds.getTempCByIndex(0);
  sensor.temp_ds18 = (t == DEVICE_DISCONNECTED_C) ? NAN : t;
  if (bmpReady) sensor.press_hPa = bmp.readPressure() / 100.0f;
}

// -------------------------------------------- alarm engine
void triggerAlarm(AlarmCause cause) {
  if (arm_state == STATE_ALARM) return;
  arm_state   = STATE_ALARM;
  alarm_cause = cause;
  logEvent("ALARM", CAUSE_NAMES[cause]);
}

void checkAlarmTriggers() {
  if (sensor.mq2_raw > th_gas_alarm) { triggerAlarm(CAUSE_GAS); return; }
  if (!isnan(sensor.temp_ds18) && sensor.temp_ds18 > th_fire_temp) { triggerAlarm(CAUSE_FIRE); return; }
  if (arm_state == STATE_ARMED_HOME) {
    if (sensor.pir_out)                              triggerAlarm(CAUSE_INTRUSION);
    else if (sensor.gate_open && sensor.is_night)    triggerAlarm(CAUSE_INTRUSION);
  } else if (arm_state == STATE_ARMED_AWAY) {
    if (sensor.pir_out || sensor.pir_in || sensor.gate_open) triggerAlarm(CAUSE_INTRUSION);
  }
}

// Drive the buzzer via LEDC directly instead of tone()/noTone(). On ESP32 the
// Arduino tone() shim allocates a channel on first call; noTone() before that
// tries to stop an un-initialised channel and spams "LEDC not initialised"
// errors every tick. LEDC gives us direct control and no such race.
constexpr uint8_t BUZZER_LEDC_CH = 4;

void buzzerInit() {
  ledcSetup(BUZZER_LEDC_CH, 2000, 8);         // ch, freq, resolution bits
  ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
  ledcWrite(BUZZER_LEDC_CH, 0);               // start silent
}

void tickBuzzer() {
  if (arm_state != STATE_ALARM) { ledcWrite(BUZZER_LEDC_CH, 0); return; }
  uint16_t freq = 800;
  if (alarm_cause == CAUSE_FIRE) freq = 2000;
  else if (alarm_cause == CAUSE_GAS)  freq = 1200;
  if ((millis() / 250) & 1) ledcWriteTone(BUZZER_LEDC_CH, freq);
  else                      ledcWrite(BUZZER_LEDC_CH, 0);
}

// -------------------------------------------- lighting ticks
void tickInteriorLighting() {
  if (interior_mode == LIGHT_OFF) { interior_target = 0; }
  else if (interior_mode == LIGHT_ON) { interior_target = 255; }
  else {  // AUTO
    uint32_t since = millis() - last_pir_in_ms;
    if (arm_state == STATE_ALARM || !sensor.is_night) interior_target = 0;
    else if (since < INTERIOR_HOLD_MS) interior_target = 255;
    else if (since < INTERIOR_HOLD_MS + INTERIOR_FADE_MS) {
      uint32_t ft = since - INTERIOR_HOLD_MS;
      interior_target = 255 - (uint8_t)((255UL * ft) / INTERIOR_FADE_MS);
    } else interior_target = 0;
  }
  if (interior_brightness < interior_target) {
    int step = (int)((255UL * LIGHTING_TICK_MS) / INTERIOR_FADE_IN_MS);
    int nb   = (int)interior_brightness + step;
    interior_brightness = (uint8_t)min(nb, (int)interior_target);
  } else {
    interior_brightness = interior_target;
  }
}

void tickGateLighting() {
  if (gate_mode == LIGHT_OFF) { gate_led_state = false; return; }
  if (gate_mode == LIGHT_ON)  { gate_led_state = true;  return; }
  if (arm_state == STATE_ALARM || !sensor.is_night) { gate_led_state = false; return; }
  gate_led_state = (millis() - last_pir_out_ms < GATE_HOLD_MS);
}

void tickHeartbeat() {
  uint32_t now = millis();
  if (!heartbeat_active && now - last_heartbeat_ms >= HEARTBEAT_PERIOD_MS) {
    heartbeat_active = true;  last_heartbeat_ms = now;
  } else if (heartbeat_active && now - last_heartbeat_ms >= HEARTBEAT_FLASH_MS) {
    heartbeat_active = false;
  }
}

void tickAlarmZone() {
  if (arm_state != STATE_ALARM) { alarm_pulse_on = false; return; }
  uint32_t now = millis();
  if (now - last_alarm_pulse_ms >= ALARM_PULSE_MS) {
    alarm_pulse_on = !alarm_pulse_on;
    last_alarm_pulse_ms = now;
  }
}

void renderStrip() {
  {
    uint32_t c = (arm_state == STATE_ALARM) ? (alarm_pulse_on ? 0xFF0000 : 0x300000) : 0;
    for (uint16_t i = ALARM_START; i <= ALARM_END; i++) strip.setPixelColor(i, c);
  }
  {
    uint16_t sc = STATE_COLORS[arm_state];
    uint8_t r = ((sc >> 11) & 0x1F) << 3;
    uint8_t g = ((sc >>  5) & 0x3F) << 2;
    uint8_t b = ( sc        & 0x1F) << 3;
    uint8_t k = heartbeat_active ? 200 : 5;
    for (uint16_t i = PERIM_START; i <= PERIM_END; i++)
      strip.setPixelColor(i, strip.Color(r*k/255, g*k/255, b*k/255));
  }
  strip.setPixelColor(GATE_PIXEL, gate_led_state ? strip.Color(255, 200, 40) : 0);
  {
    uint8_t k = interior_brightness;
    for (uint16_t i = INTER_START; i <= INTER_END; i++)
      strip.setPixelColor(i, strip.Color(255*k/255, 180*k/255, 80*k/255));
  }
  strip.show();
}

// ===========================================================================
//  UI — screens, menus, buttons
// ===========================================================================

enum UIScreen : uint8_t {
  SCR_HOME, SCR_MENU_MAIN, SCR_MENU_ARM, SCR_MENU_LIGHTS,
  SCR_MENU_SETUP, SCR_EDIT_VALUE, SCR_MENU_LOGS, SCR_MENU_INFO
};
UIScreen ui = SCR_HOME;
uint8_t  menu_cursor = 0;
bool     needs_redraw = true;

// Value-edit context (used by SCR_EDIT_VALUE)
int32_t  edit_val = 0, edit_min = 0, edit_max = 4095, edit_step = 10;
const char *edit_label = "";
uint8_t  edit_target = 0;    // 0..4 -> which threshold

// ---------- button abstraction (short + long press) ----------
struct Button {
  uint8_t  pin;
  bool     prev;
  uint32_t press_start;
  bool     short_ready, long_ready, long_fired;
};
Button b_arm  = { PIN_ARM_BTN,    HIGH, 0, false, false, false };
Button b_menu = { PIN_MENU_BTN,   HIGH, 0, false, false, false };
Button b_sel  = { PIN_SELECT_BTN, HIGH, 0, false, false, false };

void updateBtn(Button &b, const char *name) {
  bool now = digitalRead(b.pin);
  uint32_t t = millis();
  if (b.prev == HIGH && now == LOW) {
    b.press_start = t; b.long_fired = false;
    Serial.printf("[BTN] %s down\n", name);
  } else if (b.prev == LOW && now == LOW) {
    if (!b.long_fired && t - b.press_start >= LONGPRESS_MS) {
      b.long_ready = true; b.long_fired = true;
      Serial.printf("[BTN] %s LONG\n", name);
    }
  } else if (b.prev == LOW && now == HIGH) {
    if (!b.long_fired && t - b.press_start > 30) {
      b.short_ready = true;
      Serial.printf("[BTN] %s SHORT (%lums)\n",
                    name, (unsigned long)(t - b.press_start));
    }
  }
  b.prev = now;
}
bool takeShort(Button &b) { if (b.short_ready) { b.short_ready = false; return true; } return false; }
bool takeLong(Button &b)  { if (b.long_ready)  { b.long_ready  = false; return true; } return false; }

// ---------- ARM button — always-on shortcut, works in any UI ----------
void handleArmButton() {
  if (takeLong(b_arm)) {
    if (arm_state == STATE_ALARM) {
      arm_state = STATE_DISARMED; alarm_cause = CAUSE_NONE;
      logEvent("STATE", "DISARMED (alarm reset)");
    } else if (arm_state != STATE_DISARMED) {
      arm_state = STATE_DISARMED;
      logEvent("STATE", "DISARMED (long-press)");
    }
    needs_redraw = true;
  }
  if (takeShort(b_arm)) {
    if (arm_state != STATE_ALARM) {
      arm_state = (ArmState)((arm_state + 1) % 3);
      logEvent("STATE", STATE_NAMES[arm_state]);
      needs_redraw = true;
    }
  }
}

// ---------- helpers to enter a screen ----------
const char *SCREEN_NAMES[8] = {
  "HOME", "MENU_MAIN", "MENU_ARM", "MENU_LIGHTS",
  "MENU_SETUP", "EDIT_VALUE", "MENU_LOGS", "MENU_INFO"
};
void goScreen(UIScreen s) {
  ui = s; menu_cursor = 0; needs_redraw = true;
  Serial.printf("[UI] -> %s\n", SCREEN_NAMES[s]);
}

// -------------------------------------------- menu labels & counts
constexpr uint8_t MAIN_ITEMS  = 6;
constexpr uint8_t ARM_ITEMS   = 4;
constexpr uint8_t LIGHT_ITEMS = 3;
constexpr uint8_t SETUP_ITEMS = 6;

const char *MAIN_LABEL[MAIN_ITEMS] = {
  "Arm Mode", "Lighting Control", "Thresholds",
  "Recent Events", "System Info", "Back to Dashboard"
};
const char *ARM_LABEL[ARM_ITEMS] = {
  "Disarm", "Arm Home", "Arm Away", "Reset Alarm"
};

// Threshold rows: label + accessor of current value string
struct ThreshRow { const char *label; };
const ThreshRow SETUP_ROW[SETUP_ITEMS] = {
  { "Gas alarm level" },
  { "Fire temperature" },
  { "Gate distance" },
  { "Night LDR thresh" },
  { "Day LDR thresh"   },
  { "Back"             }
};

// ---------- action dispatchers per screen ----------
void mainAction(uint8_t i) {
  switch (i) {
    case 0: goScreen(SCR_MENU_ARM);    break;
    case 1: goScreen(SCR_MENU_LIGHTS); break;
    case 2: goScreen(SCR_MENU_SETUP);  break;
    case 3: goScreen(SCR_MENU_LOGS);   break;
    case 4: goScreen(SCR_MENU_INFO);   break;
    case 5: goScreen(SCR_HOME);        break;
  }
}
void armAction(uint8_t i) {
  switch (i) {
    case 0:
      if (arm_state != STATE_DISARMED) {
        arm_state = STATE_DISARMED; alarm_cause = CAUSE_NONE;
        logEvent("STATE", "DISARMED (menu)");
      }
      break;
    case 1:
      if (arm_state != STATE_ALARM && arm_state != STATE_ARMED_HOME) {
        arm_state = STATE_ARMED_HOME;
        logEvent("STATE", "ARMED HOME (menu)");
      }
      break;
    case 2:
      if (arm_state != STATE_ALARM && arm_state != STATE_ARMED_AWAY) {
        arm_state = STATE_ARMED_AWAY;
        logEvent("STATE", "ARMED AWAY (menu)");
      }
      break;
    case 3:
      if (arm_state == STATE_ALARM) {
        arm_state = STATE_DISARMED; alarm_cause = CAUSE_NONE;
        logEvent("STATE", "ALARM RESET (menu)");
      }
      break;
  }
  goScreen(SCR_HOME);
}
void lightAction(uint8_t i) {
  switch (i) {
    case 0:
      interior_mode = (LightMode)((interior_mode + 1) % 3);
      logEvent("LIGHT", (String("interior=") + LIGHT_NAMES[interior_mode]).c_str());
      needs_redraw = true;
      break;
    case 1:
      gate_mode = (LightMode)((gate_mode + 1) % 3);
      logEvent("LIGHT", (String("gate=") + LIGHT_NAMES[gate_mode]).c_str());
      needs_redraw = true;
      break;
    case 2:
      goScreen(SCR_MENU_MAIN);
      break;
  }
}
void setupAction(uint8_t i) {
  if (i == 5) { goScreen(SCR_MENU_MAIN); return; }
  edit_target = i;
  switch (i) {
    case 0: edit_val = th_gas_alarm;    edit_min = 200;  edit_max = 4000; edit_step = 50;  edit_label = "GAS ALARM"; break;
    case 1: edit_val = (int32_t)(th_fire_temp * 10);
            edit_min = 300; edit_max = 800; edit_step = 5;  edit_label = "FIRE TEMP x0.1C"; break;
    case 2: edit_val = th_gate_open_cm; edit_min = 20;   edit_max = 300;  edit_step = 5;   edit_label = "GATE DIST cm"; break;
    case 3: edit_val = th_ldr_night;    edit_min = 100;  edit_max = 3500; edit_step = 100; edit_label = "NIGHT LDR"; break;
    case 4: edit_val = th_ldr_day;      edit_min = 500;  edit_max = 4000; edit_step = 100; edit_label = "DAY LDR"; break;
  }
  goScreen(SCR_EDIT_VALUE);
}
void editSave() {
  switch (edit_target) {
    case 0: th_gas_alarm    = edit_val;         break;
    case 1: th_fire_temp    = edit_val / 10.0f; break;
    case 2: th_gate_open_cm = edit_val;         break;
    case 3: th_ldr_night    = edit_val;         break;
    case 4: th_ldr_day      = edit_val;         break;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%s -> %ld", edit_label, (long)edit_val);
  logEvent("SETUP", buf);
  goScreen(SCR_MENU_SETUP);
}

// ---------- UI event dispatch ----------
void handleUIButtons() {
  handleArmButton();

  bool mn_short = takeShort(b_menu),  mn_long = takeLong(b_menu);
  bool sl_short = takeShort(b_sel),   sl_long = takeLong(b_sel);
  if (!mn_short && !mn_long && !sl_short && !sl_long) return;

  auto cursor = [&](uint8_t n) {
    menu_cursor = (menu_cursor + 1) % n;
    needs_redraw = true;
    Serial.printf("[UI] cursor=%u/%u\n", menu_cursor, n);
  };

  switch (ui) {
    case SCR_HOME:
      if (mn_short || sl_short) goScreen(SCR_MENU_MAIN);
      break;

    case SCR_MENU_MAIN:
      if (mn_short) cursor(MAIN_ITEMS);
      if (mn_long)  goScreen(SCR_HOME);
      if (sl_short) { Serial.printf("[UI] select main[%u]\n", menu_cursor); mainAction(menu_cursor); }
      if (sl_long)  goScreen(SCR_HOME);
      break;

    case SCR_MENU_ARM:
      if (mn_short) cursor(ARM_ITEMS);
      if (mn_long)  goScreen(SCR_MENU_MAIN);
      if (sl_short) { Serial.printf("[UI] select arm[%u]\n", menu_cursor); armAction(menu_cursor); }
      if (sl_long)  goScreen(SCR_MENU_MAIN);
      break;

    case SCR_MENU_LIGHTS:
      if (mn_short) cursor(LIGHT_ITEMS);
      if (mn_long)  goScreen(SCR_MENU_MAIN);
      if (sl_short) { Serial.printf("[UI] select light[%u]\n", menu_cursor); lightAction(menu_cursor); }
      if (sl_long)  goScreen(SCR_MENU_MAIN);
      break;

    case SCR_MENU_SETUP:
      if (mn_short) cursor(SETUP_ITEMS);
      if (mn_long)  goScreen(SCR_MENU_MAIN);
      if (sl_short) { Serial.printf("[UI] select setup[%u]\n", menu_cursor); setupAction(menu_cursor); }
      if (sl_long)  goScreen(SCR_MENU_MAIN);
      break;

    case SCR_EDIT_VALUE:
      if (mn_short) {
        edit_val = min((int32_t)edit_max, edit_val + edit_step);
        needs_redraw = true;
        Serial.printf("[UI] edit +=%ld -> %ld\n", (long)edit_step, (long)edit_val);
      }
      if (mn_long) {
        edit_val = max((int32_t)edit_min, edit_val - edit_step);
        needs_redraw = true;
        Serial.printf("[UI] edit -=%ld -> %ld\n", (long)edit_step, (long)edit_val);
      }
      if (sl_short) { Serial.println("[UI] edit SAVE"); editSave(); }
      if (sl_long)  { Serial.println("[UI] edit CANCEL"); goScreen(SCR_MENU_SETUP); }
      break;

    case SCR_MENU_LOGS:
    case SCR_MENU_INFO:
      if (mn_short || sl_short || mn_long || sl_long) goScreen(SCR_MENU_MAIN);
      break;
  }
}

// ===========================================================================
//  TFT rendering — one function per screen. needs_redraw drives full clears.
// ===========================================================================
constexpr int16_t W = 320, H = 240;

void drawTitleBar(const char *title) {
  tft.fillRect(0, 0, W, 30, EMEN_GREEN);
  tft.setTextSize(2);
  tft.setTextColor(EMEN_GOLD, EMEN_GREEN);
  tft.setCursor(10, 8);
  tft.print("EMEN GUARDIAN");
  tft.setTextColor(EMEN_WHITE, EMEN_GREEN);
  int16_t lx = W - (int16_t)(strlen(title) * 12) - 8;
  tft.setCursor(lx, 8);
  tft.print(title);
}

void drawHintBar(const char *nav, const char *sel, const char *back) {
  tft.fillRect(0, 218, W, 22, EMEN_BG);
  tft.drawFastHLine(0, 216, W, EMEN_GREY);
  tft.setTextSize(1);
  tft.setTextColor(EMEN_GREEN, EMEN_BG);
  tft.setCursor(10, 224);  tft.print("MENU:");
  tft.setTextColor(EMEN_WHITE, EMEN_BG); tft.print(nav);
  tft.setTextColor(EMEN_GOLD, EMEN_BG);  tft.setCursor(120, 224); tft.print("SEL:");
  tft.setTextColor(EMEN_WHITE, EMEN_BG); tft.print(sel);
  tft.setTextColor(EMEN_BLUE, EMEN_BG);  tft.setCursor(230, 224); tft.print("ARM:");
  tft.setTextColor(EMEN_WHITE, EMEN_BG); tft.print(back);
}

// ---------- HOME dashboard ----------
void drawHomeStatic() {
  tft.fillScreen(EMEN_BG);
  drawTitleBar("HOME");

  tft.setTextSize(2);
  tft.setTextColor(EMEN_WHITE, EMEN_BG);
  tft.setCursor(10,  48); tft.print("TIME");
  tft.setCursor(10,  72); tft.print("TEMP");
  tft.setCursor(10,  96); tft.print("AMB");
  tft.setCursor(10, 120); tft.print("GAS");
  tft.setCursor(160, 72); tft.print("PRESS");
  tft.setCursor(160, 96); tft.print("DIST");
  tft.setCursor(160,120); tft.print("LGT");
  tft.setTextColor(EMEN_GOLD, EMEN_BG);
  tft.setCursor(10, 155); tft.print("ZONES");
  drawHintBar(" open menu", " open menu", " arm cycle");
}
void drawHomeLive() {
  char buf[24];

  uint16_t bg = STATE_COLORS[arm_state];
  tft.fillRect(180, 4, W - 180, 22, bg);
  tft.setTextColor(EMEN_WHITE, bg);
  tft.setTextSize(2);
  int16_t lx = W - (int16_t)(strlen(STATE_NAMES[arm_state]) * 12) - 8;
  tft.setCursor(lx, 8); tft.print(STATE_NAMES[arm_state]);

  auto slot = [&](int16_t x, int16_t y, int16_t w, uint16_t c, const char *s) {
    tft.fillRect(x, y, w, 20, EMEN_BG);
    tft.setTextColor(c, EMEN_BG);
    tft.setTextSize(2); tft.setCursor(x, y); tft.print(s);
  };
  slot(90, 48, 70, sensor.is_night ? EMEN_BLUE : EMEN_GOLD,
       sensor.is_night ? "NIGHT" : "DAY");

  if (!isnan(sensor.temp_ds18)) snprintf(buf, sizeof(buf), "%4.1fC", sensor.temp_ds18); else strcpy(buf, " --- ");
  slot(90, 72, 60, EMEN_GREEN, buf);
  if (!isnan(sensor.temp_ntc))  snprintf(buf, sizeof(buf), "%4.1fC", sensor.temp_ntc);  else strcpy(buf, " --- ");
  slot(90, 96, 60, EMEN_GOLD, buf);
  {
    uint16_t c = (sensor.mq2_raw > th_gas_alarm) ? rgb565(255,40,20) : EMEN_WHITE;
    snprintf(buf, sizeof(buf), "%4u", sensor.mq2_raw); slot(90, 120, 60, c, buf);
  }
  if (!isnan(sensor.press_hPa)) snprintf(buf, sizeof(buf), "%4.0f", sensor.press_hPa); else strcpy(buf, " --- ");
  slot(240, 72, 70, EMEN_BLUE, buf);
  {
    uint16_t c = sensor.gate_open ? EMEN_GOLD : EMEN_WHITE;
    snprintf(buf, sizeof(buf), "%3ucm", sensor.distance_cm); slot(240, 96, 70, c, buf);
  }
  snprintf(buf, sizeof(buf), "%4u", sensor.ldr_raw); slot(240, 120, 70, EMEN_WHITE, buf);

  tft.setTextSize(1);
  int y = 178;
  auto row = [&](const char *name, bool active, uint16_t color, const char *msg) {
    tft.fillRect(10, y, 300, 12, EMEN_BG);
    tft.fillCircle(18, y + 5, 4, color);
    tft.setTextColor(EMEN_WHITE, EMEN_BG);
    tft.setCursor(30, y + 2); tft.print(name);
    tft.setCursor(120, y + 2); tft.print(msg);
    y += 12;
  };
  row("PERIMETER", sensor.pir_out, sensor.pir_out ? EMEN_GOLD : EMEN_GREEN,
      sensor.pir_out ? "MOTION" : (sensor.gate_open ? "GATE OPEN" : "QUIET"));
  row("INTERIOR",  sensor.pir_in,  sensor.pir_in  ? EMEN_GOLD : EMEN_GREEN,
      sensor.pir_in  ? "MOTION" : "QUIET");
  bool gas_bad  = sensor.mq2_raw > th_gas_alarm;
  bool fire_bad = !isnan(sensor.temp_ds18) && sensor.temp_ds18 > th_fire_temp;
  const char *sm = fire_bad ? "FIRE" : (gas_bad ? "GAS LEAK" : "CLEAR");
  row("SAFETY", gas_bad || fire_bad,
      (gas_bad || fire_bad) ? rgb565(255,40,20) : EMEN_GREEN, sm);
}

// ---------- menu list rendering ----------
void drawMenuList(const char *title, const char *const *items, uint8_t count,
                  bool show_light_values = false, bool show_setup_values = false) {
  tft.fillScreen(EMEN_BG);
  drawTitleBar(title);
  tft.setTextSize(2);
  int16_t y0 = 45, dy = 24;
  for (uint8_t i = 0; i < count; i++) {
    uint16_t bg = (i == menu_cursor) ? EMEN_GREEN : EMEN_BG;
    uint16_t fg = (i == menu_cursor) ? EMEN_BG    : EMEN_WHITE;
    int16_t y = y0 + i * dy;
    tft.fillRect(0, y - 2, W, dy - 2, bg);
    tft.setTextColor(fg, bg);
    tft.setCursor(12, y);
    tft.print(items[i]);
    // Right-aligned dynamic value column.
    const char *val = nullptr;
    if (show_light_values) {
      if (i == 0) val = LIGHT_NAMES[interior_mode];
      if (i == 1) val = LIGHT_NAMES[gate_mode];
    }
    if (show_setup_values) {
      static char vbuf[16];
      switch (i) {
        case 0: snprintf(vbuf, sizeof(vbuf), "%u",     th_gas_alarm);    val = vbuf; break;
        case 1: snprintf(vbuf, sizeof(vbuf), "%.1fC",  th_fire_temp);    val = vbuf; break;
        case 2: snprintf(vbuf, sizeof(vbuf), "%ucm",   th_gate_open_cm); val = vbuf; break;
        case 3: snprintf(vbuf, sizeof(vbuf), "%u",     th_ldr_night);    val = vbuf; break;
        case 4: snprintf(vbuf, sizeof(vbuf), "%u",     th_ldr_day);      val = vbuf; break;
      }
    }
    if (val) {
      int16_t vx = W - (int16_t)(strlen(val) * 12) - 12;
      tft.setCursor(vx, y);
      tft.print(val);
    }
  }
  drawHintBar(" next / -", " ok / +", " arm shortcut");
}

// ---------- edit value screen ----------
void drawEditScreen() {
  tft.fillScreen(EMEN_BG);
  drawTitleBar("EDIT");

  tft.setTextSize(2);
  tft.setTextColor(EMEN_GOLD, EMEN_BG);
  tft.setCursor(10, 50); tft.print(edit_label);

  char buf[16];
  if (edit_target == 1) snprintf(buf, sizeof(buf), "%.1f", edit_val / 10.0f);
  else                  snprintf(buf, sizeof(buf), "%ld", (long)edit_val);
  tft.setTextSize(6);
  tft.setTextColor(EMEN_WHITE, EMEN_BG);
  tft.fillRect(0, 90, W, 60, EMEN_BG);
  int16_t vx = (W - (int16_t)strlen(buf) * 36) / 2;
  tft.setCursor(vx, 100); tft.print(buf);

  tft.setTextSize(1);
  tft.setTextColor(EMEN_GREY, EMEN_BG);
  tft.setCursor(10, 170);
  snprintf(buf, sizeof(buf), "range %ld..%ld  step %ld",
           (long)edit_min, (long)edit_max, (long)edit_step);
  tft.print(buf);

  drawHintBar(" +/- (long)", " save (long=cancel)", " arm shortcut");
}

// ---------- logs viewer ----------
void drawLogsScreen() {
  tft.fillScreen(EMEN_BG);
  drawTitleBar("EVENTS");
  tft.setTextSize(1);
  tft.setTextColor(EMEN_GOLD, EMEN_BG);
  tft.setCursor(10, 40);
  if (log_count == 0) {
    tft.setTextColor(EMEN_GREY, EMEN_BG);
    tft.print("(no events yet)");
  } else {
    // Iterate newest-first
    int16_t y = 40;
    for (int8_t i = 0; i < log_count; i++) {
      uint8_t idx = (log_head + LOG_RING_LEN - 1 - i) % LOG_RING_LEN;
      LogRow &r = log_ring[idx];
      char ts[10];
      uint32_t s = r.ms / 1000;
      snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu",
               (unsigned long)(s / 3600),
               (unsigned long)((s / 60) % 60),
               (unsigned long)(s % 60));
      tft.setTextColor(EMEN_GOLD, EMEN_BG);
      tft.setCursor(10, y); tft.print(ts);
      tft.setTextColor(EMEN_WHITE, EMEN_BG);
      tft.setCursor(80, y); tft.print(r.text);
      y += 20;
      if (y > 200) break;
    }
  }
  drawHintBar(" back", " back", " arm shortcut");
}

// ---------- info screen ----------
void drawInfoScreen() {
  tft.fillScreen(EMEN_BG);
  drawTitleBar("SYSTEM INFO");
  tft.setTextSize(1);
  int16_t y = 45;
  char buf[48];

  auto ln = [&](const char *label, const char *val, uint16_t vc) {
    tft.setTextColor(EMEN_WHITE, EMEN_BG);
    tft.setCursor(10, y); tft.print(label);
    tft.setTextColor(vc, EMEN_BG);
    tft.setCursor(150, y); tft.print(val);
    y += 16;
  };

  uint32_t s = millis() / 1000;
  snprintf(buf, sizeof(buf), "%luh %02lum %02lus",
           (unsigned long)(s / 3600),
           (unsigned long)((s / 60) % 60),
           (unsigned long)(s % 60));
  ln("Uptime",        buf,                             EMEN_GOLD);
  ln("Arm state",     STATE_NAMES[arm_state],          STATE_COLORS[arm_state]);
  ln("Alarm cause",   CAUSE_NAMES[alarm_cause],
     (alarm_cause == CAUSE_NONE) ? EMEN_GREEN : rgb565(255,40,20));
  ln("Interior mode", LIGHT_NAMES[interior_mode],      EMEN_GOLD);
  ln("Gate mode",     LIGHT_NAMES[gate_mode],          EMEN_GOLD);
  ln("SD",            sdReady  ? "ready" : "absent",   sdReady  ? EMEN_GREEN : rgb565(255,40,20));
  ln("BMP180",        bmpReady ? "ready" : "absent",   bmpReady ? EMEN_GREEN : rgb565(255,40,20));
  snprintf(buf, sizeof(buf), "%u bytes", (unsigned)ESP.getFreeHeap());
  ln("Free heap",     buf,                             EMEN_BLUE);
  snprintf(buf, sizeof(buf), "%u / %u", log_count, LOG_RING_LEN);
  ln("Log entries",   buf,                             EMEN_WHITE);

  drawHintBar(" back", " back", " arm shortcut");
}

// ---------- top-level UI render (called on tick) ----------
void renderUI() {
  switch (ui) {
    case SCR_HOME:
      if (needs_redraw) { drawHomeStatic(); needs_redraw = false; }
      drawHomeLive();
      break;
    case SCR_MENU_MAIN:
      if (needs_redraw) { drawMenuList("MAIN MENU", MAIN_LABEL, MAIN_ITEMS); needs_redraw = false; }
      break;
    case SCR_MENU_ARM:
      if (needs_redraw) { drawMenuList("ARM MODE",  ARM_LABEL,  ARM_ITEMS);  needs_redraw = false; }
      break;
    case SCR_MENU_LIGHTS: {
      if (needs_redraw) {
        const char *L[LIGHT_ITEMS] = { "Interior", "Gate", "Back" };
        drawMenuList("LIGHTING", L, LIGHT_ITEMS, /*light*/true, false);
        needs_redraw = false;
      }
      break;
    }
    case SCR_MENU_SETUP: {
      if (needs_redraw) {
        const char *L[SETUP_ITEMS];
        for (uint8_t i = 0; i < SETUP_ITEMS; i++) L[i] = SETUP_ROW[i].label;
        drawMenuList("THRESHOLDS", L, SETUP_ITEMS, false, /*setup*/true);
        needs_redraw = false;
      }
      break;
    }
    case SCR_EDIT_VALUE:
      if (needs_redraw) { drawEditScreen(); needs_redraw = false; }
      break;
    case SCR_MENU_LOGS:
      if (needs_redraw) { drawLogsScreen(); needs_redraw = false; }
      break;
    case SCR_MENU_INFO:
      drawInfoScreen();     // always redraw — live uptime
      needs_redraw = false;
      break;
  }
}

// ===========================================================================
//  setup / loop
// ===========================================================================
void setup() {
  logBoot("10_home_guardian");

  pinMode(PIN_ARM_BTN,    INPUT_PULLUP);
  pinMode(PIN_MENU_BTN,   INPUT_PULLUP);
  pinMode(PIN_SELECT_BTN, INPUT_PULLUP);
  pinMode(PIN_HC_TRIG,    OUTPUT);
  pinMode(PIN_HC_ECHO,    INPUT);
  pinMode(PIN_PIR_OUT,    INPUT);
  pinMode(PIN_PIR_IN,     INPUT);
  buzzerInit();

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  Wire.begin(PIN_SDA, PIN_SCL);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(EMEN_BG);

  // Wokwi's microsd-card widget needs a FAT image loaded through the sim UI.
  // Without one, sdcard_mount fails and spams errors — silence the sd_diskio
  // logger so the console stays clean; sdReady stays false and the rest of
  // the code skips SD writes gracefully.
  esp_log_level_set("sd_diskio",       ESP_LOG_NONE);
  esp_log_level_set("sd_diskio_crc",   ESP_LOG_NONE);
  sdReady = SD.begin(PIN_SD_CS);
  if (sdReady) {
    if (!SD.exists("/events.csv")) {
      File f = SD.open("/events.csv", FILE_WRITE);
      if (f) { f.println("ms,category,detail"); f.close(); }
    }
    if (!SD.exists("/env.csv")) {
      File f = SD.open("/env.csv", FILE_WRITE);
      if (f) { f.println("ms,temp_ds18,temp_ntc,press_hPa,dist_cm,ldr,mq2"); f.close(); }
    }
  }

  ds.begin();
  bmpReady = bmp.begin();

  strip.begin();
  strip.setBrightness(150);
  strip.clear(); strip.show();

  logEvent("BOOT", "system online");
  goScreen(SCR_HOME);
}

void loop() {
  const uint32_t now = millis();

  updateBtn(b_arm,  "ARM");
  updateBtn(b_menu, "MENU");
  updateBtn(b_sel,  "SEL");
  handleUIButtons();

  if (now - last_sensor_ms >= SENSOR_POLL_MS) {
    pollFastSensors();
    if (sensor.pir_out) last_pir_out_ms = now;
    if (sensor.pir_in)  last_pir_in_ms  = now;
    checkAlarmTriggers();
    last_sensor_ms = now;
  }
  if (now - last_slow_ms >= SLOW_POLL_MS) {
    pollSlowSensors();
    last_slow_ms = now;
  }
  if (now - last_light_ms >= LIGHTING_TICK_MS) {
    tickInteriorLighting();
    tickGateLighting();
    tickHeartbeat();
    tickAlarmZone();
    renderStrip();
    last_light_ms = now;
  }
  tickBuzzer();

  // Two render triggers: (1) needs_redraw fires immediately after any button
  // event so the UI feels snappy — no waiting for the periodic tick;
  // (2) the periodic tick keeps HOME and INFO live-updated.
  if (needs_redraw || now - last_tft_ms >= TFT_UPDATE_MS) {
    renderUI();
    last_tft_ms = now;
  }

  if (now - last_env_ms >= ENV_LOG_MS) {
    logEnv();
    last_env_ms = now;
  }
}
