#include "host/format/cpv.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "host/format/endian.hpp"
#include "host/format/planar.hpp"

namespace hp2::host {

namespace {

constexpr auto kMagic = std::uint16_t{0x1234};
constexpr auto kHeaderBytes = std::size_t{2 + (16 * 2)};
constexpr auto kPlaneCount = std::size_t{4};
constexpr auto kRowBytes = std::size_t{CpvPicture::kWidth / 8};
constexpr auto kPlaneBytes = kRowBytes * CpvPicture::kHeight;
constexpr auto kScreenBytes = kPlaneBytes * kPlaneCount;
constexpr auto kLiteralFlag = std::uint8_t{0x80};
constexpr auto kCountMask = std::uint8_t{0x7f};

// Where the n-th decoded byte goes: planes one after the other, each filled byte column by byte
// column, top to bottom (CpvNextByte, 0:0fd0).
std::size_t PlanarOffset(std::size_t index) {
  const auto plane = index / kPlaneBytes;
  const auto within = index % kPlaneBytes;
  const auto column = within / CpvPicture::kHeight;
  const auto row = within % CpvPicture::kHeight;
  return (plane * kPlaneBytes) + (row * kRowBytes) + column;
}

// Four 8000-byte planes (40 bytes a row) to one colour index per pixel.
std::vector<std::uint8_t> PlanesToPixels(std::span<const std::uint8_t> planes) {
  auto pixels = std::vector<std::uint8_t>(std::size_t{CpvPicture::kWidth} * CpvPicture::kHeight);
  for (auto plane = std::size_t{0}; plane < kPlaneCount; ++plane) {
    const auto plane_bytes = planes.subspan(plane * kPlaneBytes, kPlaneBytes);
    for (auto index = std::size_t{0}; index < kPlaneBytes; ++index) {
      const auto plane_bits = DeinterleaveShift(plane_bytes[index], static_cast<std::uint8_t>(plane));
      const auto out = std::span{pixels}.subspan(index * 8, 8);
      std::ranges::transform(plane_bits, out, out.begin(),
                             [](std::uint8_t bits, std::uint8_t pixel) { return static_cast<std::uint8_t>(bits | pixel); });
    }
  }
  return pixels;
}

// One control byte's worth of output: `count` bytes, either `source` (literal) or `source[0]`
// repeated.
struct Run {
  std::size_t count{};
  bool literal{};
  std::span<const std::uint8_t> source;
};

std::expected<Run, CpvError> ReadRun(std::span<const std::uint8_t> stream) {
  auto result = std::expected<Run, CpvError>{std::unexpected{CpvError::kTruncatedStream}};
  if (not stream.empty()) {
    const auto control = stream.front();
    const auto literal = (control & kLiteralFlag) != 0;
    const auto count = literal ? std::size_t{static_cast<std::uint8_t>(control & kCountMask)} : std::size_t{control};
    const auto source_bytes = literal ? count : std::size_t{1};
    if (count == 0) {
      // A zero count would make the original's dbf loop run 65536 times; no file has one.
      result = std::unexpected{CpvError::kZeroRun};
    } else if (stream.size() - 1 >= source_bytes) {
      result = Run{.count = count, .literal = literal, .source = stream.subspan(1, source_bytes)};
    }
  }
  return result;
}

// Decodes runs from `stream` until `planes` is full; returns the stream bytes read. The original
// stops as soon as the screen is full, even inside a run.
std::expected<std::size_t, CpvError> DecodeRuns(std::span<const std::uint8_t> stream, std::span<std::uint8_t> planes) {
  auto result = std::expected<std::size_t, CpvError>{std::size_t{0}};
  auto written = std::size_t{0};
  while (result and written < planes.size()) {
    const auto run = ReadRun(stream.subspan(*result));
    if (run) {
      for (auto index = std::size_t{0}; index < run->count and written < planes.size(); ++index) {
        planes[PlanarOffset(written++)] = run->source[run->literal ? index : 0];
      }
      *result += 1 + run->source.size();
    } else {
      result = std::unexpected{run.error()};
    }
  }
  return result;
}

}  // namespace

std::expected<CpvPicture, CpvError> DecodeCpv(std::span<const std::uint8_t> file) {
  const auto bytes = std::as_bytes(file);
  auto result = std::expected<CpvPicture, CpvError>{CpvPicture{}};
  if (file.size() < kHeaderBytes) {
    result = std::unexpected{CpvError::kTooShort};
  } else if (ReadBigEndian<std::uint16_t>(bytes) != kMagic) {
    result = std::unexpected{CpvError::kBadMagic};
  } else {
    for (auto index = std::size_t{0}; index < result->palette_st.size(); ++index) {
      result->palette_st[index] = *ReadBigEndian<std::uint16_t>(bytes.subspan(2 + (2 * index)));
    }
    auto planes = std::vector<std::uint8_t>(kScreenBytes);
    const auto stream_bytes = DecodeRuns(file.subspan(kHeaderBytes), planes);
    if (stream_bytes) {
      result->pixels = PlanesToPixels(planes);
      result->consumed = kHeaderBytes + *stream_bytes;
    } else {
      result = std::unexpected{stream_bytes.error()};
    }
  }
  return result;
}

}  // namespace hp2::host
