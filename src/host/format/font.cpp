#include "host/format/font.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "host/format/planar.hpp"

namespace hp2::host {

namespace {

constexpr auto kPlaneCount = std::size_t{4};
constexpr auto kGlyphBytes = std::size_t{Font::kGlyphSize} * kPlaneCount;

}  // namespace

std::expected<Font, FontError> DecodeFont(std::span<const std::uint8_t> file) {
  auto result = std::expected<Font, FontError>{Font{}};
  if (file.empty() or file.size() % kGlyphBytes != 0) {
    result = std::unexpected{FontError::kBadSize};
  } else {
    result->pixels.resize(file.size() / kGlyphBytes * Font::kGlyphPixels);
    for (auto row = std::size_t{0}; row < file.size() / kPlaneCount; ++row) {
      const auto out = std::span{result->pixels}.subspan(row * Font::kGlyphSize, Font::kGlyphSize);
      for (auto plane = std::size_t{0}; plane < kPlaneCount; ++plane) {
        const auto plane_bits = DeinterleaveShift(file[(row * kPlaneCount) + plane], static_cast<std::uint8_t>(plane));
        std::ranges::transform(plane_bits, out, out.begin(), [](std::uint8_t bits, std::uint8_t pixel) {
          return static_cast<std::uint8_t>(bits | pixel);
        });
      }
    }
  }
  return result;
}

}  // namespace hp2::host
