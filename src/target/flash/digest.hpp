#pragma once

// SHA-256 digest type and its hex literal form. The hashing itself is the target's business: the
// RP2350 accelerator on the board, a software implementation in tests.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hp2::flash {

inline constexpr std::size_t kDigestLength = 32;
using Digest = std::array<std::uint8_t, kDigestLength>;

// A 64-character hex string to a digest. An invalid character yields all 0xFF, which no image
// hashes to.
[[nodiscard]] consteval Digest Sha256FromHex(std::span<const char, 65> hex) {
  const auto nibble = [](char character) {
    auto value = std::uint8_t{0xff};
    if (character >= '0' and character <= '9') {
      value = static_cast<std::uint8_t>(character - '0');
    } else if (character >= 'a' and character <= 'f') {
      value = static_cast<std::uint8_t>(character - 'a' + 10);
    } else if (character >= 'A' and character <= 'F') {
      value = static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return value;
  };
  auto digest = Digest{};
  auto valid = true;
  for (auto index = std::size_t{0}; index < kDigestLength; ++index) {
    const auto high = nibble(hex[2 * index]);
    const auto low = nibble(hex[(2 * index) + 1]);
    valid = valid and high != 0xff and low != 0xff;
    digest[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (not valid) {
    digest.fill(0xff);
  }
  return digest;
}

}  // namespace hp2::flash
