#pragma once

// Board interface. Pulls in the chosen board's board_definitions.hpp (the board directory is first
// on the include path, set by HP2_BOARD in CMake) and pins the per-board surface with a concept.
// The Board class is a static-method entry point.

#include "board_definitions.hpp"

namespace hp2 {

template <typename T>
concept BoardLike = requires(bool lit) {
  T::InitCore0();
  T::InitCore1();
  T::SetStatusLed(lit);
};

static_assert(BoardLike<Board>, "board_definitions.hpp must define a Board with InitCore0, InitCore1 and SetStatusLed");

}  // namespace hp2
