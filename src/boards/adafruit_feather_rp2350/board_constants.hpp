#pragma once

// Board constants for the Adafruit Feather RP2350 with PSRAM. Product page:
// https://www.adafruit.com/product/6222
//
//   SoC     : RP2350A, 520 KiB SRAM, 8 MB QSPI flash.
//   PSRAM   : 8 MB QSPI PSRAM on CS GP8 (unused so far).
//   Display : External 480x320 ST7796 module on SPI0, native landscape, RGB565 on the wire, driven
//             by the ST7789V driver with the ST7796 init sequence.
//   Audio   : External I2S DAC on GP26..GP29 (not driven yet).

#include <cstdint>

namespace hp2 {

// 300 MHz, the 1.25 V tier: the PL022's even divider lands the panel SPI on 75 MHz (/4) and the
// flash on 100 MHz (/3). InitCore0 raises the core voltage and reprograms the QMI flash divider
// before the switch.
inline constexpr std::uint32_t kSysClockKhz{300000};

namespace lcd {

inline constexpr int kSpiInstance{0};
inline constexpr int kCsPin{25};
inline constexpr int kSclkPin{22};  // SPI0 SCK
inline constexpr int kMosiPin{23};  // SPI0 TX
inline constexpr int kDcPin{24};
inline constexpr int kResetPin{5};
inline constexpr int kBacklightPin{1};  // PWM-capable

// Native addressing is 480 columns x 320 rows: landscape needs no row/column exchange, MADCTL keeps
// MV clear (st7789v_init.hpp).
inline constexpr std::uint16_t kWidth{480};
inline constexpr std::uint16_t kHeight{320};

// clk_peri tracks clk_sys; the PL022 takes the largest even divisor at or under the request: /4
// gives 75 MHz SCK at 300 MHz.
inline constexpr std::uint32_t kSpiBaudRate{kSysClockKhz * 1000 / 4};

}  // namespace lcd

namespace i2s {

inline constexpr int kDataInPin{26};
inline constexpr int kBckPin{27};
inline constexpr int kLRClockPin{28};
inline constexpr int kSckPin{29};  // driven low: the DAC derives MCLK from BCK

}  // namespace i2s

inline constexpr int kStatusLedPin{7};
inline constexpr int kNeoPixelPin{21};  // unused

}  // namespace hp2
