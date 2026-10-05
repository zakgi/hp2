#pragma once

// Per-board surface for the Adafruit Feather RP2350 with PSRAM. Init is split per core:
// InitCore0() runs the core voltage, the flash clock divider, the system clocks, the GPIO and the
// ST7796 panel from main() before core 1 starts; InitCore1() enables the cycle counter on the
// second core. Subsystem objects are namespace-scope members of board.cpp.

#include "board_constants.hpp"
#include "display/st7789v.hpp"

namespace hp2 {

class AdafruitFeatherRp2350Board {
 public:
  using Display = St7789v<St7789vPins{
                              .spi_instance = lcd::kSpiInstance,
                              .cs = lcd::kCsPin,
                              .sclk = lcd::kSclkPin,
                              .mosi = lcd::kMosiPin,
                              .dc = lcd::kDcPin,
                              .reset = lcd::kResetPin,
                              .backlight = lcd::kBacklightPin,
                          },
                          lcd::kWidth, lcd::kHeight, lcd::kSpiBaudRate>;

  // Core 0: voltage, flash clock divider, clocks, cycle counter, status LED, the panel and its DMA
  // completion IRQ. Core 1: cycle counter.
  static void InitCore0();
  static void InitCore1();
  static Display& DisplayInstance();

  // The color index to RGB565 table the panel's DMA chain reads, 512-byte aligned (board.cpp).
  using PaletteLut = St7789vPaletteTable;
  static PaletteLut& PaletteLutInstance();

  static void SetStatusLed(bool lit);
};

using Board = AdafruitFeatherRp2350Board;

}  // namespace hp2
