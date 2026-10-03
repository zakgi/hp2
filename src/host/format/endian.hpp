#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>

namespace hp2::host {

using ConstDataSpan = std::span<const std::byte>;

// `value` as stored in big-endian memory, the 68000's byte order.
template <std::integral T>
[[nodiscard]] constexpr T FromBigEndian(T value) {
  auto result = value;
  if constexpr (std::endian::native == std::endian::little) {
    result = std::byteswap(value);
  }
  return result;
}

// The big-endian T at the start of `data`; empty when `data` is too short.
template <std::integral T>
[[nodiscard]] std::optional<T> ReadBigEndian(ConstDataSpan data) {
  auto result = std::optional<T>{};
  if (data.size() >= sizeof(T)) {
    auto local = std::array<std::byte, sizeof(T)>{};
    std::ranges::copy(data.first(sizeof(T)), local.begin());
    result = FromBigEndian(std::bit_cast<T>(local));
  }
  return result;
}

// Like ReadBigEndian, and advances `data` past the value when there was one.
template <std::integral T>
[[nodiscard]] std::optional<T> TakeBigEndian(ConstDataSpan& data) {
  const auto result = ReadBigEndian<T>(data);
  if (result) {
    data = data.subspan(sizeof(T));
  }
  return result;
}

}  // namespace hp2::host
