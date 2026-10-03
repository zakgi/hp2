#pragma once

#include <cstdint>
#include <span>

namespace hp2 {

// One run of a delta frame: the pixels from screen pixel `offset` (row * screen width + x) on,
// continuing onto the following rows, are XORed with masks[mask_first, mask_first + mask_count).
struct XorRun {
  std::uint32_t offset{};
  std::uint32_t mask_first{};
  std::uint32_t mask_count{};

  friend constexpr bool operator==(const XorRun&, const XorRun&) = default;
};

// One delta frame: runs[run_first, run_first + run_count). `source_words` and the number of runs
// are the size of the original's delta, which set how long the original took to apply it.
struct XorFrame {
  std::uint32_t run_first{};
  std::uint32_t run_count{};
  std::uint32_t source_words{};

  friend constexpr bool operator==(const XorFrame&, const XorFrame&) = default;
};

// One step of a play list: apply `frame` (an index into the frames), then wait. `delay` is the
// original's busy-wait count (PlayDIF, 0:106e).
struct AnimationStep {
  std::uint16_t frame{};
  std::uint16_t delay{};

  friend constexpr bool operator==(const AnimationStep&, const AnimationStep&) = default;
};

// A delta animation over a picture and the order its frames are shown in (frames may repeat).
struct XorAnimation {
  std::span<const std::uint8_t> masks;
  std::span<const XorRun> runs;
  std::span<const XorFrame> frames;
  std::span<const AnimationStep> steps;

  [[nodiscard]] constexpr std::span<const XorRun> GetRuns(const XorFrame& frame) const {
    return runs.subspan(frame.run_first, frame.run_count);
  }
  [[nodiscard]] constexpr std::span<const std::uint8_t> GetMasks(const XorRun& run) const {
    return masks.subspan(run.mask_first, run.mask_count);
  }
};

}  // namespace hp2
