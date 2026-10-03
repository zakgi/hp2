#pragma once

// Boot-time check of the asset image in the "assets" partition against the digest the packer
// recorded. Erased flash reads 0xFF, so an unflashed partition is absent, not corrupt. The
// engine's view of the image is the generated asset_layout::FlashAssets(), valid once this passed.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "flash_partitions.hpp"
#include "target/flash/asset_image.hpp"
#include "target/flash/asset_layout.hpp"

namespace hp2::flash {

enum class AssetStatus : std::uint8_t { kPass, kFail, kAbsent };

static_assert(asset_layout::kImageSize <= kAssets.size, "the asset image does not fit its partition");

// The image as flashed.
[[nodiscard]] inline std::span<const std::uint8_t> ImageBytes() {
  return TableView<std::uint8_t>(kAssets.address, asset_layout::kImageSize);
}

// Hashes the image with Sha256Fn and compares it with the recorded digest. Scanning for erased
// flash stops at the first programmed byte, so a present image costs one read.
template <auto Sha256Fn>
[[nodiscard]] AssetStatus VerifyAssets() {
  const auto bytes = ImageBytes();
  auto status = AssetStatus::kFail;
  if (std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0xff; })) {
    status = AssetStatus::kAbsent;
  } else if (Sha256Fn(bytes) == asset_layout::kImageDigest) {
    status = AssetStatus::kPass;
  }
  return status;
}

}  // namespace hp2::flash
