#pragma once

// Turns the screen's pixels and palettes into panel contents through the driver's palette-lookup
// path: each viewport's palette into the RGB565 table the DMA chain reads, then its pixels by DMA,
// one push per viewport. The target's counterpart of host::Renderer; colors are resolved here and
// nowhere else.

#include <cstddef>
#include <cstdint>

#include "hardware/sync.h"

#include "core/palette.hpp"
#include "core/screen.hpp"
#include "display/st7789v.hpp"

namespace hp2 {

template <typename Display>
class PanelPresenter {
 public:
  PanelPresenter(Display& display, St7789vPaletteTable& lut) : display_(display), lut_(lut) {}

  // Sets the panel's window to the screen's size, centered, and starts the lookup path. Once,
  // after the board's panel init; the window stays for every push.
  void Init() {
    static_assert(Screen::kWidth <= Display::kPanelWidth and Screen::kHeight <= Display::kPanelHeight);
    constexpr std::uint16_t kOriginX = (Display::kPanelWidth - Screen::kWidth) / 2;
    constexpr std::uint16_t kOriginY = (Display::kPanelHeight - Screen::kHeight) / 2;
    display_.Deselect();
    display_.SetCommandFormat();
    display_.SetWindow(kOriginX, kOriginY, kOriginX + Screen::kWidth - 1, kOriginY + Screen::kHeight - 1);
    display_.SetPixelFormat(16);
    display_.InitPaletteLut(lut_);
  }

  // Waits for the previous push to leave the screen, then sends each viewport: its palette into
  // the table, its pixels after it. The upper viewport's push is waited for, since the table
  // changes for the lower one; the last push is left running.
  void Present(const Screen& screen) {
    for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
      const auto pixels = screen.Pixels(viewport);
      if (not pixels.empty()) {
        WaitDrained();
        const auto colors = screen.Palette(viewport).Colors();
        for (auto index = std::size_t{0}; index < colors.size(); ++index) {
          lut_.entries[index] = ToRgb565(colors[index].red, colors[index].green, colors[index].blue);
        }
        if (not display_.PushIndexed(pixels)) {
          ++dropped_;
        }
      }
    }
  }

  // The DMA completion interrupt sets this core's event register, so the wait sleeps in WFE and
  // rechecks on each wake instead of polling the DMA.
  void WaitIdle() {
    while (display_.Busy()) {
      __wfe();
    }
  }

  [[nodiscard]] std::uint32_t DroppedPushes() const { return dropped_; }

 private:
  // Waits until the table is free to change: the previous push done and its last indices looked
  // up, seen on two readings in a row (St7789v::Drained).
  void WaitDrained() {
    WaitIdle();
    auto readings = 0;
    while (readings < 2) {
      readings = display_.Drained() ? readings + 1 : 0;
    }
  }

  Display& display_;
  St7789vPaletteTable& lut_;
  std::uint32_t dropped_{0};
};

}  // namespace hp2
