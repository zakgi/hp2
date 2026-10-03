#pragma once

// Minimal SHA-256 for test vectors only (FIPS 180-4). Not for use in the engine or tools.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace hp2::test {

inline std::string Sha256Hex(std::span<const std::uint8_t> message) {
  constexpr std::array<std::uint32_t, 64> kRoundConstants = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  const auto process_block = [&state, &kRoundConstants](std::span<const std::uint8_t, 64> block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
      const std::size_t byte = 4 * index;
      schedule[index] = std::uint32_t{block[byte]} << 24 | std::uint32_t{block[byte + 1]} << 16 |
                        std::uint32_t{block[byte + 2]} << 8 | std::uint32_t{block[byte + 3]};
    }
    for (std::size_t index = 16; index < 64; ++index) {
      const std::uint32_t word15 = schedule[index - 15];
      const std::uint32_t word2 = schedule[index - 2];
      const std::uint32_t sigma0 = std::rotr(word15, 7) ^ std::rotr(word15, 18) ^ (word15 >> 3);
      const std::uint32_t sigma1 = std::rotr(word2, 17) ^ std::rotr(word2, 19) ^ (word2 >> 10);
      schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
    }
    std::array<std::uint32_t, 8> work = state;
    for (std::size_t index = 0; index < 64; ++index) {
      const std::uint32_t big_sigma1 = std::rotr(work[4], 6) ^ std::rotr(work[4], 11) ^ std::rotr(work[4], 25);
      const std::uint32_t choice = (work[4] & work[5]) ^ (~work[4] & work[6]);
      const std::uint32_t temp1 = work[7] + big_sigma1 + choice + kRoundConstants[index] + schedule[index];
      const std::uint32_t big_sigma0 = std::rotr(work[0], 2) ^ std::rotr(work[0], 13) ^ std::rotr(work[0], 22);
      const std::uint32_t majority = (work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]);
      const std::uint32_t temp2 = big_sigma0 + majority;
      work[7] = work[6];
      work[6] = work[5];
      work[5] = work[4];
      work[4] = work[3] + temp1;
      work[3] = work[2];
      work[2] = work[1];
      work[1] = work[0];
      work[0] = temp1 + temp2;
    }
    for (std::size_t index = 0; index < 8; ++index) {
      state[index] += work[index];
    }
  };

  std::size_t offset = 0;
  while (message.size() - offset >= 64) {
    process_block(message.subspan(offset).first<64>());
    offset += 64;
  }
  std::array<std::uint8_t, 128> tail{};
  const std::size_t remaining = message.size() - offset;
  for (std::size_t index = 0; index < remaining; ++index) {
    tail[index] = message[offset + index];
  }
  tail[remaining] = 0x80;
  const std::size_t tail_length = remaining < 56 ? 64 : 128;
  const std::uint64_t bit_length = std::uint64_t{message.size()} * 8;
  for (std::size_t index = 0; index < 8; ++index) {
    tail[tail_length - 1 - index] = static_cast<std::uint8_t>(bit_length >> (8 * index));
  }
  process_block(std::span<const std::uint8_t, 64>{tail.data(), 64});
  if (tail_length == 128) {
    process_block(std::span<const std::uint8_t, 64>{tail.data() + 64, 64});
  }

  std::string hex;
  constexpr std::string_view kDigits = "0123456789abcdef";
  for (const std::uint32_t word : state) {
    for (std::size_t shift = 0; shift < 32; shift += 4) {
      hex.push_back(kDigits[(word >> (28 - shift)) & 0xf]);
    }
  }
  return hex;
}

}  // namespace hp2::test
