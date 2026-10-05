// SPDX-License-Identifier: MIT

#pragma once

// ST7796 boot-time register sequence for the Adafruit Feather RP2350 (external 480x320 ST7796
// module, wired like an ST7789V). Values from the vendor demo's st7796_init(), order and delays
// preserved: SLPOUT first (registers are written awake, unlike the ST7789V flow), then
// MADCTL/COLMOD, then the extended registers bracketed by the Command Set Control unlock (0xF0 =
// 0xC3, 0xF0 = 0x96) and lock (0xF0 = 0x3C, 0xF0 = 0x69), then INVON and DISPON.
//
// COLMOD is 0x05: RGB565, 16-bit frames on the wire; the ST7796 serial interface does not take the
// ST7789V's 12-bit RGB444 format. The SPI frame size on this board follows suit
// (SetPixelFormat(16) before pixel data). MADCTL 0x38 = MV clear (native 480x320 addressing), BGR
// subpixel order per the vendor demo.

#include <array>

#include "display/display_init.hpp"

namespace hp2 {

constexpr std::array kSt7789vInitSequence{

    // SLPOUT: exit sleep; 120 ms stabilization before any register writes.
    SelectPanel(),
    Command(0x11),
    DeselectPanel(),
    WaitMs(120),

    // MADCTL = 0x38. This module's native addressing is 480 columns x 320 rows, so landscape needs
    // no row/column exchange (MV stays clear). If the image comes up mirrored, set MX (0x40) and/or
    // MY (0x80) here.
    SelectPanel(),
    Command(0x36),
    Data(0x38),
    DeselectPanel(),

    // COLMOD = 0x05 (16 bits/pixel, RGB565).
    SelectPanel(),
    Command(0x3a),
    Data(0x05),
    DeselectPanel(),

    // Command Set Control: unlock part I then part II so the extended registers below take effect.
    SelectPanel(),
    Command(0xf0),
    Data(0xc3),
    DeselectPanel(),
    SelectPanel(),
    Command(0xf0),
    Data(0x96),
    DeselectPanel(),

    // DIC: display inversion control, 1-dot inversion.
    SelectPanel(),
    Command(0xb4),
    Data(0x01),
    DeselectPanel(),

    // EM: entry mode set.
    SelectPanel(),
    Command(0xb7),
    Data(0xc6),
    DeselectPanel(),

    // PWR1 / PWR2 / PWR3: power controls.
    SelectPanel(),
    Command(0xc0),
    Data(0x80),
    Data(0x45),
    DeselectPanel(),
    SelectPanel(),
    Command(0xc1),
    Data(0x13),
    DeselectPanel(),
    SelectPanel(),
    Command(0xc2),
    Data(0xa7),
    DeselectPanel(),

    // VCMPCTL: VCOM control.
    SelectPanel(),
    Command(0xc5),
    Data(0x0a),
    DeselectPanel(),

    // DOCA: display output ctrl adjust.
    SelectPanel(),
    Command(0xe8),
    Data(0x40),
    Data(0x8a),
    Data(0x00),
    Data(0x00),
    Data(0x29),
    Data(0x19),
    Data(0xa5),
    Data(0x33),
    DeselectPanel(),

    // PGC: positive gamma correction (14 coefficients).
    SelectPanel(),
    Command(0xe0),
    Data(0xd0),
    Data(0x08),
    Data(0x0f),
    Data(0x06),
    Data(0x06),
    Data(0x33),
    Data(0x30),
    Data(0x33),
    Data(0x47),
    Data(0x17),
    Data(0x13),
    Data(0x13),
    Data(0x2b),
    Data(0x31),
    DeselectPanel(),

    // NGC: negative gamma correction (14 coefficients).
    SelectPanel(),
    Command(0xe1),
    Data(0xd0),
    Data(0x0a),
    Data(0x11),
    Data(0x0b),
    Data(0x09),
    Data(0x07),
    Data(0x2f),
    Data(0x33),
    Data(0x47),
    Data(0x38),
    Data(0x15),
    Data(0x16),
    Data(0x2c),
    Data(0x32),
    DeselectPanel(),

    // Command Set Control: lock part I then part II.
    SelectPanel(),
    Command(0xf0),
    Data(0x3c),
    DeselectPanel(),
    SelectPanel(),
    Command(0xf0),
    Data(0x69),
    DeselectPanel(),
    WaitMs(120),

    // INVON: display inversion on (vendor module ships reversed).
    SelectPanel(),
    Command(0x21),
    DeselectPanel(),

    // DISPON: display on.
    SelectPanel(),
    Command(0x29),
    DeselectPanel(),
};

}  // namespace hp2
