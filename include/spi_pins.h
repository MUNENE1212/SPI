// spi_pins.h — Shared SPI pin map for every project in this repo.
// ---------------------------------------------------------------------------
// The ESP32 has two hardware SPI peripherals: HSPI and VSPI. The Arduino
// core exposes VSPI as the default `SPI` object with the pins below.
// Individual projects reassign the CS pin (chip-select is device-specific)
// but SCK/MOSI/MISO stay the same across the whole repo.

#pragma once                    // Include this file at most once per TU.

#define SPI_SCK_PIN    18       // Clock: master drives this line.
#define SPI_MOSI_PIN   23       // Master-Out Slave-In: data from ESP32.
#define SPI_MISO_PIN   19       // Master-In Slave-Out: data to ESP32.
