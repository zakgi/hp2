#pragma once

#include <cstdint>
#include <span>

namespace hp2 {

// A sound effect: signed 8-bit samples at `rate_hz`. A non-empty loop repeats
// [loop_start, loop_start + loop_length) after the samples before it.
struct SoundSample {
  std::span<const std::int8_t> samples;
  std::uint32_t rate_hz{};
  std::uint32_t loop_start{};
  std::uint32_t loop_length{};
};

}  // namespace hp2
