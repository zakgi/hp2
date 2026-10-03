#include "target/flash/asset_image.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets.hpp"
#include "core/engine_assets.hpp"
#include "host/asset_manager.hpp"
#include "sha256.hpp"
#include "target/flash/asset_layout.hpp"
#include "target/flash/digest.hpp"

namespace hp2::flash {
namespace {

std::string Hex(const Digest& digest) {
  constexpr auto kDigits = std::string_view{"0123456789abcdef"};
  auto text = std::string{};
  for (const auto byte : digest) {
    text.push_back(kDigits[byte >> 4]);
    text.push_back(kDigits[byte & 0xf]);
  }
  return text;
}

template <typename T>
bool Same(std::span<const T> first, std::span<const T> second) {
  return std::ranges::equal(first, second);
}

// The image the host build packed (target hp2_assets), read through the generated layout and
// checked against what the asset manager decodes from the disk.
TEST(AssetImage, MatchesTheAssetManager) {
  auto file = std::ifstream{HP2_ASSET_IMAGE, std::ios::binary};
  if (not file or not test::Executable()) {
    GTEST_SKIP() << "packed image or game disk not present";
  }
  const auto image = std::vector<std::uint8_t>{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  ASSERT_EQ(image.size(), asset_layout::kImageSize);
  EXPECT_EQ(test::Sha256Hex(image), Hex(asset_layout::kImageDigest));

  auto manager = host::AssetManager{};
  ASSERT_TRUE(manager.Load(test::DiskImage()));
  const auto& expected = manager.Engine();
  const auto actual = asset_layout::FlashAssets(std::bit_cast<std::uintptr_t>(image.data()));

  for (auto index = std::size_t{0}; index < kEnginePictureCount; ++index) {
    EXPECT_EQ(actual.pictures[index].width, expected.pictures[index].width);
    EXPECT_EQ(actual.pictures[index].height, expected.pictures[index].height);
    EXPECT_TRUE(Same(actual.pictures[index].pixels, expected.pictures[index].pixels)) << "picture " << index;
  }
  for (auto index = std::size_t{0}; index < kEnginePaletteCount; ++index) {
    EXPECT_TRUE(Same(actual.palettes[index], expected.palettes[index])) << "palette " << index;
  }
  for (auto index = std::size_t{0}; index < kEngineBankCount; ++index) {
    EXPECT_TRUE(Same(actual.banks[index].pixels, expected.banks[index].pixels)) << "bank " << index;
    EXPECT_TRUE(Same(actual.banks[index].sprites, expected.banks[index].sprites)) << "bank " << index;
  }
  for (auto index = std::size_t{0}; index < kEngineFontCount; ++index) {
    EXPECT_TRUE(Same(actual.fonts[index].pixels, expected.fonts[index].pixels)) << "font " << index;
  }
  for (auto index = std::size_t{0}; index < kEngineSoundCount; ++index) {
    const auto& sound = actual.sounds[index];
    const auto& reference = expected.sounds[index];
    EXPECT_TRUE(Same(sound.samples, reference.samples)) << "sound " << index;
    EXPECT_EQ(sound.rate_hz, reference.rate_hz) << "sound " << index;
    EXPECT_EQ(sound.loop_start, reference.loop_start) << "sound " << index;
    EXPECT_EQ(sound.loop_length, reference.loop_length) << "sound " << index;
  }

  EXPECT_TRUE(Same(actual.title_animation.masks, expected.title_animation.masks));
  EXPECT_TRUE(Same(actual.title_animation.runs, expected.title_animation.runs));
  EXPECT_TRUE(Same(actual.title_animation.frames, expected.title_animation.frames));
  EXPECT_TRUE(Same(actual.title_animation.steps, expected.title_animation.steps));

  EXPECT_TRUE(Same(actual.title_music.sample_data, expected.title_music.sample_data));
  EXPECT_TRUE(Same(actual.title_music.samples, expected.title_music.samples));
  EXPECT_TRUE(Same(actual.title_music.positions, expected.title_music.positions));
  EXPECT_TRUE(Same(actual.title_music.notes, expected.title_music.notes));
  EXPECT_EQ(actual.title_music.timer, expected.title_music.timer);

  EXPECT_TRUE(Same(actual.road_map.cells, expected.road_map.cells));
  EXPECT_TRUE(Same(actual.scenery.objects, expected.scenery.objects));
  EXPECT_EQ(actual.scenery.cell_types, expected.scenery.cell_types);
}

}  // namespace
}  // namespace hp2::flash
