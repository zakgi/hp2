#include "host/format/bob_bank.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "host/format/endian.hpp"
#include "host/format/planar.hpp"

namespace hp2::host {

namespace {

constexpr auto kImageHeaderBytes = std::size_t{10};
constexpr auto kScreenPlanes = std::uint8_t{4};
constexpr auto kStoredPlanesMask = std::uint16_t{0xf};
constexpr auto kTargetPlanesShift = 8;

// The screen plane each stored plane goes to, lowest chosen plane first; empty when the flags
// choose fewer planes than are stored.
std::vector<std::uint8_t> TargetPlanes(std::uint16_t flags) {
  const auto stored = static_cast<std::size_t>(flags & kStoredPlanesMask);
  auto targets = std::vector<std::uint8_t>{};
  for (auto plane = std::uint8_t{0}; plane < kScreenPlanes and targets.size() < stored; ++plane) {
    if (((flags >> kTargetPlanesShift) & (1U << plane)) != 0) {
      targets.push_back(plane);
    }
  }
  if (targets.size() < stored) {
    targets.clear();
  }
  return targets;
}

std::expected<BobImage, BobBankError> DecodeImage(std::span<const std::uint8_t> file, std::size_t offset) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<BobImage, BobBankError>{std::unexpected{BobBankError::kImageOutOfBank}};
  if (offset + kImageHeaderBytes <= file.size()) {
    const auto header = bytes.subspan(offset);
    const auto flags = *ReadBigEndian<std::uint16_t>(header);
    const auto width_words = std::size_t{*ReadBigEndian<std::uint16_t>(header.subspan(2))};
    const auto height = *ReadBigEndian<std::uint16_t>(header.subspan(4));
    const auto targets = TargetPlanes(flags);
    const auto row_bytes = width_words * 2;
    const auto plane_bytes = row_bytes * height;
    const auto stored = static_cast<std::size_t>(flags & kStoredPlanesMask);
    if (targets.size() != stored) {
      result = std::unexpected{BobBankError::kTooFewTargetPlanes};
    } else if (offset + kImageHeaderBytes + (stored * plane_bytes) <= file.size()) {
      auto image = BobImage{.width = static_cast<std::uint16_t>(width_words * 16),
                            .height = height,
                            .origin_x = *ReadBigEndian<std::int16_t>(header.subspan(6)),
                            .origin_y = *ReadBigEndian<std::int16_t>(header.subspan(8)),
                            .pixels = std::vector<std::uint8_t>(plane_bytes * 8)};
      const auto planes = file.subspan(offset + kImageHeaderBytes, stored * plane_bytes);
      for (auto index = std::size_t{0}; index < stored; ++index) {
        const auto plane = planes.subspan(index * plane_bytes, plane_bytes);
        for (auto byte = std::size_t{0}; byte < plane_bytes; ++byte) {
          const auto plane_bits = DeinterleaveShift(plane[byte], targets[index]);
          const auto out = std::span{image.pixels}.subspan(byte * 8, 8);
          std::ranges::transform(plane_bits, out, out.begin(), [](std::uint8_t bits, std::uint8_t pixel) {
            return static_cast<std::uint8_t>(bits | pixel);
          });
        }
      }
      result = std::move(image);
    }
  }
  return result;
}

}  // namespace

std::expected<std::vector<BobImage>, BobBankError> DecodeBobBank(std::span<const std::uint8_t> file) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<std::vector<BobImage>, BobBankError>{std::vector<BobImage>{}};
  const auto count = ReadBigEndian<std::uint16_t>(bytes);
  if (not count or file.size() < 2 + (2 * std::size_t{*count})) {
    result = std::unexpected{BobBankError::kTooShort};
  }
  for (auto index = std::size_t{0}; result and index < count.value_or(0); ++index) {
    const auto offset = *ReadBigEndian<std::uint16_t>(bytes.subspan(2 + (2 * index)));
    auto image = DecodeImage(file, offset);
    if (image) {
      result->push_back(std::move(*image));
    } else {
      result = std::unexpected{image.error()};
    }
  }
  return result;
}

}  // namespace hp2::host
