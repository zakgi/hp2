#pragma once

#include <cstdint>
#include <span>

namespace hp2 {

// One run of a delta frame: the pixels from screen pixel `offset` (row * screen width + x) on,
// continuing onto the following rows, are XORed with `masks`.
struct XorRun {
  std::uint32_t offset{};
  std::span<const std::uint8_t> masks;
};

// One delta frame. `source_words` and the number of runs are the size of the original's delta,
// which set how long the original took to apply it.
struct XorFrame {
  std::span<const XorRun> runs;
  std::uint32_t source_words{};
};

// One step of a play list: apply `frame` (an index into the frames), then wait. `delay` is the
// original's busy-wait count (PlayDIF, 0:106e).
struct AnimationStep {
  std::uint16_t frame{};
  std::uint16_t delay{};
};

// A delta animation over a picture and the order its frames are shown in (frames may repeat).
struct XorAnimation {
  std::span<const XorFrame> frames;
  std::span<const AnimationStep> steps;
};

}  // namespace hp2
