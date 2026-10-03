#include "host/format/dif.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "host/format/endian.hpp"

namespace hp2::host {

namespace {

constexpr auto kScreenBytes = std::uint32_t{32'000};
constexpr auto kGroupBytes = std::uint32_t{8};  // 4 plane words for 16 pixels
constexpr auto kGroupPixels = std::uint32_t{16};
// A list longer than this is taken as a misread offset rather than a play list (the game's has 27
// steps).
constexpr auto kMaxSteps = std::size_t{256};

// One run: `words` XORed into the ST screen from byte `offset` on, as pixel masks over the groups
// they touch.
DifRun ToPixelRun(std::uint32_t offset, ConstDataSpan words, std::uint32_t count) {
  const auto first_group = offset / kGroupBytes;
  const auto last_group = (offset + (2 * count) - 2) / kGroupBytes;
  auto run = DifRun{.offset = first_group * kGroupPixels,
                    .masks = std::vector<std::uint8_t>(std::size_t{last_group - first_group + 1} * kGroupPixels)};
  for (auto index = std::uint32_t{0}; index < count; ++index) {
    const auto byte = offset + (2 * index);
    const auto word = *ReadBigEndian<std::uint16_t>(words.subspan(2 * std::size_t{index}));
    const auto plane_bit = static_cast<std::uint8_t>(1U << ((byte % kGroupBytes) / 2));
    const auto first_pixel = ((byte / kGroupBytes) - first_group) * kGroupPixels;
    for (auto pixel = std::uint32_t{0}; pixel < kGroupPixels; ++pixel) {
      if ((word & (0x8000U >> pixel)) != 0) {
        run.masks[first_pixel + pixel] ^= plane_bit;
      }
    }
  }
  return run;
}

std::expected<DifFrame, DifError> DecodeFrame(ConstDataSpan file, std::uint32_t frame_offset) {
  auto result = std::expected<DifFrame, DifError>{DifFrame{}};
  auto data = frame_offset <= file.size() ? file.subspan(frame_offset) : ConstDataSpan{};
  auto finished = false;
  while (result and not finished) {
    const auto count = TakeBigEndian<std::uint16_t>(data);
    if (not count) {
      result = std::unexpected{DifError::kFrameOutOfFile};
    } else if (*count == 0) {
      finished = true;
    } else {
      const auto offset = TakeBigEndian<std::uint16_t>(data);
      const auto bytes = 2 * std::size_t{*count};
      if (not offset or data.size() < bytes) {
        result = std::unexpected{DifError::kFrameOutOfFile};
      } else if (*offset % 2 != 0) {
        // The 68000 cannot XOR a word at an odd address.
        result = std::unexpected{DifError::kOddOffset};
      } else if (std::uint32_t{*offset} + bytes > kScreenBytes) {
        result = std::unexpected{DifError::kRunPastScreen};
      } else {
        result->runs.push_back(ToPixelRun(*offset, data.first(bytes), *count));
        result->words += *count;
        data = data.subspan(bytes);
      }
    }
  }
  return result;
}

}  // namespace

std::expected<std::vector<DifFrame>, DifError> DecodeDif(std::span<const std::uint8_t> file) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<std::vector<DifFrame>, DifError>{std::vector<DifFrame>{}};
  const auto count = ReadBigEndian<std::uint32_t>(bytes);
  if (not count or (file.size() - 4) / 4 < *count) {
    result = std::unexpected{DifError::kTooShort};
  }
  for (auto frame = std::size_t{1}; result and frame <= count.value_or(0); ++frame) {
    auto decoded = DecodeFrame(bytes, *ReadBigEndian<std::uint32_t>(bytes.subspan(4 * frame)));
    if (decoded) {
      result->push_back(std::move(*decoded));
    } else {
      result = std::unexpected{decoded.error()};
    }
  }
  return result;
}

std::expected<std::vector<AnimationStep>, PlayListError> ReadPlayList(const LoadableHunk& hunk, std::uint32_t offset,
                                                                      std::size_t frame_count) {
  auto result = std::expected<std::vector<AnimationStep>, PlayListError>{std::vector<AnimationStep>{}};
  auto finished = false;
  while (result and not finished) {
    const auto frame = hunk.Read<std::uint16_t>(offset);
    // The end marker has no delay after it.
    const auto delay = frame == 0 ? std::optional<std::uint16_t>{0} : hunk.Read<std::uint16_t>(offset + 2);
    if (not frame or not delay) {
      result = std::unexpected{PlayListError::kOutOfHunk};
    } else if (*frame == 0) {
      finished = true;
    } else if (*frame > frame_count) {
      result = std::unexpected{PlayListError::kUnknownFrame};
    } else if (result->size() == kMaxSteps) {
      result = std::unexpected{PlayListError::kTooLong};
    } else {
      result->push_back(AnimationStep{.frame = static_cast<std::uint16_t>(*frame - 1), .delay = *delay});
      offset += 4;
    }
  }
  return result;
}

}  // namespace hp2::host
