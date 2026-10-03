#pragma once

// DWT cycle counter of the RP2350's Cortex-M33 cores. Each core has its own DWT at the
// architecturally fixed System Control Space addresses (ARMv8-M ARM, D2.4); CYCCNT counts CPU
// cycles and wraps every 2^32 cycles. DEMCR.TRCENA gates the DWT, so Enable programs it first. Per
// core: each core's init calls EnableCycleCounter() itself. Nothing here logs, allocates or blocks.

#include <cstdint>

namespace hp2 {

inline constexpr std::uintptr_t kDemcrAddress{0xE000EDFC};
inline constexpr std::uintptr_t kDwtCtrlAddress{0xE0001000};
inline constexpr std::uintptr_t kDwtCyccntAddress{0xE0001004};

inline constexpr std::uint32_t kDemcrTrcenaBit{1U << 24};
inline constexpr std::uint32_t kDwtCtrlCyccntenaBit{1U << 0};
inline constexpr std::uint32_t kDwtCtrlNocyccntBit{1U << 25};

// Memory-mapped registers: an integer address is the only way to name them.
[[nodiscard]] inline volatile std::uint32_t& Demcr() {
  return *reinterpret_cast<volatile std::uint32_t*>(kDemcrAddress);
}
[[nodiscard]] inline volatile std::uint32_t& DwtCtrl() {
  return *reinterpret_cast<volatile std::uint32_t*>(kDwtCtrlAddress);
}
[[nodiscard]] inline volatile std::uint32_t& DwtCyccnt() {
  return *reinterpret_cast<volatile std::uint32_t*>(kDwtCyccntAddress);
}

// Meaningful after EnableCycleCounter() has set TRCENA.
[[nodiscard]] inline bool CycleCounterAvailable() {
  return (DwtCtrl() & kDwtCtrlNocyccntBit) == 0;
}

// Enables the calling core's counter and resets it. Idempotent.
inline void EnableCycleCounter() {
  Demcr() |= kDemcrTrcenaBit;
  DwtCyccnt() = 0;
  DwtCtrl() |= kDwtCtrlCyccntenaBit;
}

// Deltas are unsigned subtractions, exact across one wrap.
[[nodiscard]] inline std::uint32_t ReadCycleCounter() {
  return DwtCyccnt();
}

class CycleTimer {
 public:
  CycleTimer() : start_{ReadCycleCounter()} {}

  void Reset() { start_ = ReadCycleCounter(); }
  [[nodiscard]] std::uint32_t Elapsed() const { return ReadCycleCounter() - start_; }

  // Elapsed cycles, then a new window from the same read, so contiguous spans lose nothing.
  std::uint32_t ElapsedAndReset() {
    const auto now = ReadCycleCounter();
    const auto delta = now - start_;
    start_ = now;
    return delta;
  }

 private:
  std::uint32_t start_{0};
};

}  // namespace hp2
