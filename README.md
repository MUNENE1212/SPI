# SPI — ESP32 Learning Bench

A PlatformIO monorepo of ESP32 mini-projects focused on the **SPI** bus —
sibling to the [I2C repo](https://github.com/MUNENE1212/I2C). Same layout:
one folder per mini-project under `src/`, each with `main.cpp`,
`diagram.json` and `wokwi.toml`; each wired into `platformio.ini` as its
own env so builds and simulations stay isolated.

## Why a whole repo for SPI?

I²C addresses devices by a 7-bit number on a shared 2-wire bus (SDA/SCL).
SPI selects the target with a dedicated **CS** (chip-select) line and
streams bits synchronously over **MOSI + MISO + SCK**. That gives you:

- **Higher clock rates** than I²C (tens of MHz on ESP32) — good for
  displays, SD cards, and dense pixel data.
- **No arbitration** — one master talks to whichever peripheral has its CS
  pulled low. Chained devices (like MAX7219 modules) share the same
  MOSI/CLK/CS and pass data down the chain.
- **Fewer library conventions** — every peripheral defines its own byte
  protocol on top of the raw SPI transfer.

## Repo layout

```
.
├── platformio.ini
├── src/
│   └── 01_max7219_hello/       # first project — MAX7219 scroll
├── include/  lib/  test/
└── .gitignore
```

## The mini-projects

| # | Folder | Hardware | What it teaches |
|---|--------|----------|-----------------|
| 1 | `01_max7219_hello` | ESP32 + 4× MAX7219 (8×32) | First SPI: hardware SPI init on ESP32 (SCK=18, MOSI=23, CS=5), `MD_MAX72XX` for the framing, `MD_Parola` for the scroll effect. Scrolls "SPI // hello" across the chain. |

More coming — SD card read, ST7735 TFT splash, chained shift-register
demo. Each follows the same self-contained pattern.

## Pin map (ESP32 dev kit, hardware SPI on VSPI)

| Signal | GPIO |
|--------|------|
| SCK    | 18   |
| MOSI   | 23   |
| MISO   | 19   |
| CS     | 5 (default; can be reassigned per project) |

## Building & simulating

```bash
pio run -e 01_max7219_hello              # build
pio run -e 01_max7219_hello -t upload    # flash a real board
```

Wokwi: open `src/<project>/` in VS Code, run **Wokwi: Start Simulator**.

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

## License

MIT — see `LICENSE`.
