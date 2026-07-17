# SPI — ESP32 Learning Bench

A PlatformIO monorepo of ESP32 mini-projects focused on the **SPI** bus —
sibling to the [I2C repo](https://github.com/MUNENE1212/I2C). One folder
per mini-project under `src/`, each self-contained with `main.cpp`,
`diagram.json`, and `wokwi.toml`; each wired into `platformio.ini` as its
own env so builds and simulations stay isolated.

## Why a whole repo for SPI?

I²C addresses devices by a 7-bit number on a shared 2-wire bus (SDA/SCL).
SPI selects the target with a dedicated **CS** (chip-select) line and
streams bits synchronously over **MOSI + MISO + SCK**. That gives you:

- **Higher clock rates** than I²C (tens of MHz on ESP32) — good for
  displays, SD cards, and dense pixel data.
- **No arbitration** — one master talks to whichever peripheral has its CS
  pulled low. Chained devices (like MAX7219 modules) share the same
  MOSI/CLK and pass data down the chain.
- **Fewer library conventions** — every peripheral defines its own byte
  protocol on top of the raw SPI transfer.

## Repo layout

```
.
├── platformio.ini
├── include/                       # shared header-only helpers
│   ├── spi_pins.h                 # SPI_SCK_PIN, SPI_MOSI_PIN, SPI_MISO_PIN
│   ├── emen_serial.h              # logBoot() — consistent Serial banner
│   └── emen_brand.h               # RGB565 brand palette (EMEN_GREEN, ...)
├── src/
│   ├── 01_max7219_hello/          # MAX7219 8x32 scrolling marquee
│   ├── 02_hello_tft/              # ST7735 128x160 color TFT splash
│   ├── 03_sd_card/                # MicroSD write/read/list
│   ├── 04_shift_register/         # 74HC595 WALK + CYLON via raw SPI
│   └── 05_rfid_mfrc522/           # RFID tag reader (UID -> Serial)
└── .gitignore
```

## The mini-projects

| # | Folder | Hardware | What it teaches |
|---|--------|----------|-----------------|
| 1 | `01_max7219_hello`   | 4× MAX7219 (8×32) | First SPI. Uses `MD_MAX72XX` for framing and `MD_Parola` for the scroll effect. |
| 2 | `02_hello_tft`       | ST7735 128×160 SPI TFT | Adafruit_GFX primitives on a color surface. Draws the EMEN wordmark, a brand ribbon, and a palette legend. |
| 3 | `03_sd_card`         | MicroSD slot | ESP32 Arduino core's `SD.h` + `FS.h`. Mounts a card, reports type/size, writes/reads `/boot.log`, lists root. |
| 4 | `04_shift_register`  | 74HC595 + 8 LEDs | Raw SPI without a peripheral library. Two non-blocking animations (WALK, CYLON) drive Q0..Q7 via `SPI.transfer()`. |
| 5 | `05_rfid_mfrc522`    | MFRC522 RFID reader + tags | miguelbalboa/MFRC522. Polls for tags, prints the UID over Serial, blinks a confirmation LED. |

Every `setup()` starts with `logBoot("<project>")` from
[`include/emen_serial.h`](include/emen_serial.h), so serial logs across
the repo look the same. TFT projects use the RGB565 palette in
[`include/emen_brand.h`](include/emen_brand.h) so brand colors stay
consistent. Shared SPI pins live in
[`include/spi_pins.h`](include/spi_pins.h).

## Pin map (ESP32 dev kit, hardware SPI on VSPI)

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK    | 18   | Shared by every SPI project. |
| MOSI   | 23   | Shared by every SPI project. |
| MISO   | 19   | Only bidirectional projects (SD, RFID) use this. |
| CS     | project-specific | 5 (TFT, MAX7219, RFID), 15 (SD), 5 (74HC595 latch). |
| DC / A0 (TFT only) | 16 | Data/command line for the ST7735. |
| RST    | 17 | Reset line for TFT and RFID. |

## Building & simulating

```bash
pio run -e 02_hello_tft              # build one env
pio run                              # build every env at once
pio run -e 03_sd_card -t upload      # flash a real board
```

Wokwi: open `src/<project>/` in VS Code, run **Wokwi: Start Simulator**
(it picks up the local `wokwi.toml` and `diagram.json`).

## Design conventions used across all projects

- **Shared helpers live in `include/`** — never copy-paste boot banners or
  brand constants across projects.
- **`millis()`, not `delay()`** — every loop is non-blocking. See the
  `WALK` / `CYLON` swap in `04_shift_register/main.cpp` for the pattern.
- **Explicit stdint types** (`uint8_t`, `uint32_t`) instead of Arduino's
  `byte` / `unsigned long`, so intent is unambiguous.
- **Line-by-line comments** explain *why*, not just *what* — these
  projects are meant to be read like tutorials.
- **One responsibility per helper** — `writeSR()`, `formatUid()`,
  `dumpFile()`, `drawCentered()` etc. are small and focused.

## Adding a new mini-project

1. Create `src/NN_name/` with `main.cpp`, `diagram.json`, `wokwi.toml`.
2. Point `wokwi.toml` at `../../.pio/build/NN_name/firmware.{bin,elf}`.
3. Append to `platformio.ini`:
   ```ini
   [env:NN_name]
   build_src_filter = +<NN_name/>
   lib_deps =
       ; libraries this project uses
   ```
4. In `main.cpp`, `#include "emen_serial.h"` and call `logBoot("NN_name")`
   in `setup()` for a consistent boot log.

## License

MIT — see `LICENSE`.
