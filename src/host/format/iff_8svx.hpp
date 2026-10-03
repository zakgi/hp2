#pragma once

#include <cstdint>
#include <optional>

#include "host/format/endian.hpp"

namespace hp2::host {

// Voice Header (VHDR) chunk content.
struct VoiceHeader {
  std::uint32_t one_shot_hi_samples{};   // samples before the looping portion
  std::uint32_t repeat_hi_samples{};     // samples in the looping portion
  std::uint32_t samples_per_hi_cycle{};  // samples per synthetic cycle
  std::uint16_t samples_per_sec{};       // sample rate
  std::uint8_t ct_octave{};              // octaves of data
  std::uint8_t s_compression{};          // 0 = none, 1 = Fibonacci delta
  std::uint32_t volume{};                // fixed point, 0x10000 = unity
};

inline constexpr std::uint8_t kCompressionNone = 0;
inline constexpr std::uint8_t kCompressionFibonacciDelta = 1;

// An IFF 8SVX sound: its voice header and a view of its samples (signed 8-bit) in the file.
class Iff8SvxAsset {
 public:
  // The sound in `raw_data`; empty unless it is a FORM 8SVX with a VHDR and an uncompressed BODY.
  [[nodiscard]] static std::optional<Iff8SvxAsset> FromRawData(ConstDataSpan raw_data);

  [[nodiscard]] ConstDataSpan GetSamples() const { return samples_; }
  [[nodiscard]] std::uint32_t GetSampleRate() const { return voice_.samples_per_sec; }
  [[nodiscard]] std::uint32_t GetSamplesPerCycle() const { return voice_.samples_per_hi_cycle; }
  [[nodiscard]] std::uint32_t GetOneShotSamples() const { return voice_.one_shot_hi_samples; }
  [[nodiscard]] std::uint32_t GetRepeatSamples() const { return voice_.repeat_hi_samples; }

 private:
  VoiceHeader voice_{};
  ConstDataSpan samples_;
};

}  // namespace hp2::host
