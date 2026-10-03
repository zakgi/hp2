#include "host/format/iff_8svx.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "host/format/endian.hpp"
#include "host/format/iff.hpp"

namespace hp2::host {

namespace {

constexpr auto kVoiceHeaderBytes = std::size_t{20};

std::optional<VoiceHeader> ReadVoiceHeader(ConstDataSpan payload) {
  auto result = std::optional<VoiceHeader>{};
  if (payload.size() >= kVoiceHeaderBytes) {
    result = VoiceHeader{.one_shot_hi_samples = *TakeBigEndian<std::uint32_t>(payload),
                         .repeat_hi_samples = *TakeBigEndian<std::uint32_t>(payload),
                         .samples_per_hi_cycle = *TakeBigEndian<std::uint32_t>(payload),
                         .samples_per_sec = *TakeBigEndian<std::uint16_t>(payload),
                         .ct_octave = *TakeBigEndian<std::uint8_t>(payload),
                         .s_compression = *TakeBigEndian<std::uint8_t>(payload),
                         .volume = *TakeBigEndian<std::uint32_t>(payload)};
  }
  return result;
}

}  // namespace

std::optional<Iff8SvxAsset> Iff8SvxAsset::FromRawData(ConstDataSpan raw_data) {
  auto result = std::optional<Iff8SvxAsset>{};
  if (IsIffForm(raw_data, k8SvxSignature)) {
    // The FORM's own size bounds its chunks.
    const auto form_size = std::size_t{*ReadBigEndian<std::uint32_t>(raw_data.subspan(4))};
    const auto body = raw_data.subspan(kIffFormHeaderBytes);
    auto voice = std::optional<VoiceHeader>{};
    auto samples = std::optional<ConstDataSpan>{};
    const auto chunks_size = form_size >= 4 ? form_size - 4 : 0;  // the size counts the type
    for (const auto& chunk : GetIffChunks(body.first(std::min(body.size(), chunks_size)))) {
      if (chunk.chunk.id == kVoiceHeaderSignature) {
        voice = ReadVoiceHeader(chunk.payload);
      } else if (chunk.chunk.id == kBodySignature) {
        samples = chunk.payload;
      }
    }
    if (voice and samples and voice->s_compression == kCompressionNone) {
      auto asset = Iff8SvxAsset{};
      asset.voice_ = *voice;
      asset.samples_ = *samples;
      result = asset;
    }
  }
  return result;
}

}  // namespace hp2::host
