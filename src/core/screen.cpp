#include "core/screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

namespace hp2 {

void Screen::EnableSplit(std::uint16_t split_row) {
  split_row_ = split_row > 0 and split_row < kHeight ? split_row : std::uint16_t{0};
  selected_ = Viewport::kUpper;
}

void Screen::DisableSplit() {
  split_row_ = 0;
  selected_ = Viewport::kUpper;
}

void Screen::SetViewport(Viewport viewport) {
  if (split_row_ != 0) {
    selected_ = viewport;
  }
}

std::uint16_t Screen::FirstRow(Viewport viewport) const {
  return viewport == Viewport::kLower ? split_row_ : std::uint16_t{0};
}

std::uint16_t Screen::Rows(Viewport viewport) const {
  auto rows = kHeight;
  if (split_row_ != 0) {
    rows = viewport == Viewport::kUpper ? split_row_ : static_cast<std::uint16_t>(kHeight - split_row_);
  } else if (viewport == Viewport::kLower) {
    rows = 0;
  }
  return rows;
}

std::span<std::uint8_t, Screen::kWidth> Screen::Row(std::uint16_t row) {
  const auto screen_row = std::size_t{FirstRow(selected_)} + row;
  return std::span<std::uint8_t, kWidth>{pixels_.data() + (std::min<std::size_t>(screen_row, kHeight - 1) * kWidth),
                                         kWidth};
}

std::span<const std::uint8_t, Screen::kWidth> Screen::ScreenRow(std::uint16_t row) const {
  return std::span<const std::uint8_t, kWidth>{pixels_.data() + (std::min<std::size_t>(row, kHeight - 1) * kWidth),
                                               kWidth};
}

std::span<const std::uint8_t> Screen::Pixels(Viewport viewport) const {
  return std::span{pixels_}.subspan(std::size_t{FirstRow(viewport)} * kWidth, std::size_t{Rows(viewport)} * kWidth);
}

void Screen::Clear(std::uint8_t index) {
  const auto first = std::size_t{FirstRow(selected_)} * kWidth;
  std::fill_n(pixels_.begin() + static_cast<std::ptrdiff_t>(first), std::size_t{Rows(selected_)} * kWidth, index);
}

template <typename Combine>
void Screen::BlitWith(const ImageView& image, Point origin, bool mirrored, Combine combine) {
  // Clip in 32-bit arithmetic: origin plus image size may exceed int16.
  const auto left = std::max<std::int32_t>(origin.x, 0);
  const auto top = std::max<std::int32_t>(origin.y, 0);
  const auto right = std::min<std::int32_t>(origin.x + std::int32_t{image.width}, kWidth);
  const auto bottom = std::min<std::int32_t>(origin.y + std::int32_t{image.height}, Rows(selected_));
  const auto complete = image.pixels.size() >= std::size_t{image.width} * image.height;
  if (complete and right > left and bottom > top) {
    const auto skipped = static_cast<std::size_t>(left - origin.x);
    const auto count = static_cast<std::size_t>(right - left);
    for (auto row = top; row < bottom; ++row) {
      const auto pixels = image.Row(static_cast<std::uint16_t>(row - origin.y));
      const auto destination = Row(static_cast<std::uint16_t>(row)).subspan(static_cast<std::size_t>(left));
      if (mirrored) {
        // The image's last column lands on its first.
        const auto source = pixels.subspan(image.width - skipped - count, count);
        std::ranges::transform(std::views::reverse(source), destination, destination.begin(), combine);
      } else {
        std::ranges::transform(pixels.subspan(skipped, count), destination, destination.begin(), combine);
      }
    }
  }
}

void Screen::Blit(const ImageView& image, Point origin, std::uint8_t index_offset) {
  BlitWith(image, origin, false, [index_offset](std::uint8_t index, [[maybe_unused]] std::uint8_t pixel) {
    return static_cast<std::uint8_t>(index + index_offset);
  });
}

void Screen::BlitMasked(const ImageView& image, Point origin, std::uint8_t index_offset) {
  BlitWith(image, origin, false, [index_offset](std::uint8_t index, std::uint8_t pixel) {
    return index == 0 ? pixel : static_cast<std::uint8_t>(index + index_offset);
  });
}

void Screen::BlitMaskedMirrored(const ImageView& image, Point origin, std::uint8_t index_offset) {
  BlitWith(image, origin, true, [index_offset](std::uint8_t index, std::uint8_t pixel) {
    return index == 0 ? pixel : static_cast<std::uint8_t>(index + index_offset);
  });
}

void Screen::ApplyFrame(const XorAnimation& animation, const XorFrame& frame) {
  for (const auto& run : animation.GetRuns(frame)) {
    XorScreen(run.offset, animation.GetMasks(run));
  }
}

void Screen::DrawText(const BitmapFont& font, std::string_view text, Point origin, std::uint8_t index_offset) {
  for (const auto character : text) {
    const auto glyph = font.GetGlyph(character);
    if (not glyph.pixels.empty()) {
      Blit(glyph, origin, index_offset);
    }
    origin.x = static_cast<std::int16_t>(origin.x + BitmapFont::kGlyphSize);
  }
}

void Screen::Copy(Point source, Point destination, std::uint16_t width, std::uint16_t height) {
  // The columns and rows where both the source and the destination are inside the viewport.
  const auto rows = std::int32_t{Rows(selected_)};
  // Explicit std::int32_t: it is long on some targets, so int operands would not deduce one type.
  const auto first_column = std::max<std::int32_t>({0, -source.x, -destination.x});
  const auto last_column = std::min<std::int32_t>({width, kWidth - source.x, kWidth - destination.x});
  const auto first_row = std::max<std::int32_t>({0, -source.y, -destination.y});
  const auto last_row = std::min<std::int32_t>({height, rows - source.y, rows - destination.y});
  if (last_column > first_column and last_row > first_row) {
    // Rows go in the direction that reads every source row before it is overwritten; each row
    // goes through a buffer, so columns may overlap too.
    const auto downwards = destination.y <= source.y;
    const auto source_left = std::int32_t{source.x} + first_column;
    const auto destination_left = std::int32_t{destination.x} + first_column;
    const auto source_column = static_cast<std::size_t>(source_left);
    const auto destination_column = static_cast<std::size_t>(destination_left);
    const auto count = static_cast<std::size_t>(last_column - first_column);
    auto buffer = std::array<std::uint8_t, kWidth>{};
    for (auto step = first_row; step < last_row; ++step) {
      const auto row = downwards ? step : last_row - 1 - (step - first_row);
      std::ranges::copy(Row(static_cast<std::uint16_t>(source.y + row)).subspan(source_column, count), buffer.begin());
      std::ranges::copy(std::span{buffer}.first(count),
                        Row(static_cast<std::uint16_t>(destination.y + row)).subspan(destination_column).begin());
    }
  }
}

void Screen::DrawHorizontalLine(std::int16_t left, std::int16_t right, std::int16_t row, std::uint8_t index) {
  const auto first = std::max<std::int32_t>(left, 0);
  const auto last = std::min<std::int32_t>(right, kWidth - 1);
  if (row >= 0 and row < Rows(selected_) and last >= first) {
    const auto pixels = Row(static_cast<std::uint16_t>(row))
                            .subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(last - first + 1));
    std::ranges::fill(pixels, index);
  }
}

void Screen::DrawVerticalLine(std::int16_t top, std::int16_t bottom, std::int16_t column, std::uint8_t index) {
  const auto first = std::max<std::int32_t>(top, 0);
  const auto last = std::min<std::int32_t>(bottom, Rows(selected_) - 1);
  if (column >= 0 and column < kWidth) {
    for (auto row = first; row <= last; ++row) {
      Row(static_cast<std::uint16_t>(row))[static_cast<std::size_t>(column)] = index;
    }
  }
}

void Screen::DrawLine(Point start, Point end, std::uint8_t index) {
  // The coordinate that changes more is counted pixel by pixel, upward; the other one follows in
  // 16.16 steps from the middle of its first pixel.
  constexpr auto kOnePixel = std::int64_t{1} << 16U;
  constexpr auto kHalfPixel = kOnePixel / 2;
  const auto wide = std::abs(end.x - start.x) >= std::abs(end.y - start.y);
  if (wide ? end.x < start.x : end.y < start.y) {
    std::swap(start, end);
  }
  const auto steps = std::int64_t{wide ? end.x - start.x : end.y - start.y};
  const auto column_step = wide ? kOnePixel : (end.x - start.x) * kOnePixel / std::max<std::int64_t>(steps, 1);
  const auto row_step = wide ? (end.y - start.y) * kOnePixel / std::max<std::int64_t>(steps, 1) : kOnePixel;
  auto column = (start.x * kOnePixel) + kHalfPixel;
  auto row = (start.y * kOnePixel) + kHalfPixel;
  const auto rows = std::int64_t{Rows(selected_)};
  for (auto step = std::int64_t{0}; step <= steps; ++step) {
    const auto pixel_column = column >> 16U;
    const auto pixel_row = row >> 16U;
    if (pixel_column >= 0 and pixel_column < kWidth and pixel_row >= 0 and pixel_row < rows) {
      Row(static_cast<std::uint16_t>(pixel_row))[static_cast<std::size_t>(pixel_column)] = index;
    }
    column += column_step;
    row += row_step;
  }
}

void Screen::XorScreen(std::uint32_t offset, std::span<const std::uint8_t> masks) {
  if (offset < pixels_.size()) {
    const auto count = std::min<std::size_t>(masks.size(), pixels_.size() - offset);
    const auto pixels = std::span{pixels_}.subspan(offset, count);
    std::ranges::transform(pixels, masks.first(count), pixels.begin(), [](std::uint8_t pixel, std::uint8_t mask) {
      return static_cast<std::uint8_t>(pixel ^ mask);
    });
  }
}

}  // namespace hp2
