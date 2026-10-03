#pragma once

// SHA-256 over the RP2350 hardware accelerator (pico_sha256), fed by DMA.

#include <algorithm>
#include <cstdint>
#include <span>

#include "pico/sha256.h"
#include "pico/stdlib.h"

#include "target/flash/digest.hpp"

namespace hp2::flash {

static_assert(sizeof(sha256_result_t) == sizeof(Digest), "Digest does not match the SDK result layout");

// Single-shot digest in the byte order a software SHA-256 produces. The accelerator is shared
// hardware, so start_blocking waits for it.
[[nodiscard]] inline Digest Sha256Hw(std::span<const std::uint8_t> data) {
  auto state = pico_sha256_state_t{};
  const auto retcode = pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, true);
  hard_assert(retcode == PICO_OK);
  pico_sha256_update_blocking(&state, data.data(), data.size());

  auto result = sha256_result_t{};
  pico_sha256_finish(&state, &result);

  auto digest = Digest{};
  std::ranges::copy(result.bytes, digest.begin());
  return digest;
}

}  // namespace hp2::flash
