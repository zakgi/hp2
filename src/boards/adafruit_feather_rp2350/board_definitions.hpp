#pragma once

// Per-board surface for the Adafruit Feather RP2350 with PSRAM. Init is split per core:
// InitCore0() sets the core voltage, the flash clock divider, the system clocks and the GPIO from
// main() before core 1 starts; InitCore1() enables the cycle counter on the second core.

#include "board_constants.hpp"

namespace hp2 {

class AdafruitFeatherRp2350Board {
 public:
  static void InitCore0();
  static void InitCore1();
  static void SetStatusLed(bool lit);
};

using Board = AdafruitFeatherRp2350Board;

}  // namespace hp2
