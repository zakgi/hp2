#pragma once

#include <cstdint>

namespace hp2 {

// A small deterministic generator (PCG32, XSH RR), seeded once per mission so a mission replays
// from its seed and keystrokes. The original's only random source is the display's beam position
// (ReadBeamPosition, 1:19a4).
class Random {
 public:
  explicit constexpr Random(std::uint64_t seed = 0) {
    Next();
    state_ += seed;
    Next();
  }

  constexpr std::uint32_t Next() {
    const auto previous = state_;
    state_ = (previous * kMultiplier) + kIncrement;
    const auto shuffled = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(previous >> 59U);
    return (shuffled >> rotation) | (shuffled << ((32U - rotation) & 31U));
  }
  // In [0, count), by multiply and shift: some values come up more often, by at most count / 2^32.
  constexpr std::uint32_t Below(std::uint32_t count) {
    return static_cast<std::uint32_t>((std::uint64_t{Next()} * count) >> 32U);
  }
  // Uniform in [0, 1).
  constexpr float GetUnit() { return static_cast<float>(Next() >> 8U) * (1.0F / 16777216.0F); }

 private:
  static constexpr auto kMultiplier = std::uint64_t{6364136223846793005U};
  static constexpr auto kIncrement = std::uint64_t{1442695040888963407U};

  std::uint64_t state_{};
};

}  // namespace hp2
