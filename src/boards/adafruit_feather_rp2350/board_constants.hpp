#pragma once

// Board constants for the Adafruit Feather RP2350 with PSRAM. Product page:
// https://www.adafruit.com/product/6222
//
//   SoC    : RP2350A, 520 KiB SRAM, 8 MB QSPI flash.
//   PSRAM  : 8 MB QSPI PSRAM on CS GP8 (unused so far).
//   Audio  : External I2S DAC on GP26..GP29 (not driven yet).

#include <cstdint>

namespace hp2 {

// 300 MHz, the 1.25 V tier: the flash runs at 100 MHz (/3). InitCore0 raises the core voltage and
// reprograms the QMI flash divider before the switch.
inline constexpr std::uint32_t kSysClockKhz{300000};

namespace i2s {

inline constexpr int kSckPin{29};  // driven low: the DAC derives MCLK from BCK

}  // namespace i2s

inline constexpr int kStatusLedPin{7};

}  // namespace hp2
