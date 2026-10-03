#include "core/title.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "assets.hpp"
#include "core/audio_engine.hpp"
#include "core/key_events.hpp"
#include "core/screen.hpp"
#include "host/asset_manager.hpp"
#include "sha256.hpp"

namespace hp2 {
namespace {

// Digests from scripts/reference_digests.py.
constexpr auto kLogoRgb = "e2acdef5860ac75cd114acf5b93c82f486e7b20a0d08370b6c883e92107f4458";
constexpr auto kFinishedRgb = "7e62575de0716ec678eba686048e066dc65efe2626ef16417f946ff1a2b274fd";

// The screen's pixels through each viewport's palette, as RGB rows.
std::vector<std::uint8_t> ResolveRgb(const Screen& screen) {
  auto rgb = std::vector<std::uint8_t>{};
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    const auto& palette = screen.Palette(viewport);
    const auto first = screen.FirstRow(viewport);
    for (auto row = first; row < first + screen.Rows(viewport); ++row) {
      for (const auto pixel : screen.ScreenRow(row)) {
        const auto color = palette.Color(pixel);
        rgb.insert(rgb.end(), {color.red, color.green, color.blue});
      }
    }
  }
  return rgb;
}

bool AllBlack(const Screen& screen) {
  const auto rgb = ResolveRgb(screen);
  return std::ranges::all_of(rgb, [](std::uint8_t component) { return component == 0; });
}

class TitleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game files not present in " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }

  void Press(Key key) { keys_.Record({.key = key, .action = KeyAction::kPress}); }

  host::AssetManager manager_;
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
};

TEST_F(TitleTest, FadesTheLogoIn) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_EQ(screen_.SplitRow(), 0);
  EXPECT_TRUE(AllBlack(screen_));  // fade level 7: every ST colour is black
  // One long tick runs the whole fade: levels are never skipped, only shown late.
  EXPECT_EQ(title.Step(static_cast<float>(kFadeLevels) * original_timing::kFadeStepSeconds), ComponentType::kTitle);
  EXPECT_EQ(test::Sha256Hex(ResolveRgb(screen_)), kLogoRgb);
}

TEST_F(TitleTest, PlaysThroughToTheFinishedTitle) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  // About 11 seconds of presentation; the finished title then waits for a keystroke.
  for (auto tick = 0; tick < 600; ++tick) {
    ASSERT_EQ(title.Step(1.0F / 30.0F), ComponentType::kTitle);
  }
  EXPECT_EQ(screen_.SplitRow(), 36);
  EXPECT_EQ(test::Sha256Hex(ResolveRgb(screen_)), kFinishedRgb);
}

TEST_F(TitleTest, SkipsToTheFinishedTitleThenFadesOutToTheOffice) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  EXPECT_EQ(title.Step(0.5F), ComponentType::kTitle);
  keys_.Record({.key = Key::kSpace, .action = KeyAction::kRelease});
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_EQ(screen_.SplitRow(), 0);  // a release alone is not a keystroke to act on
  Press(Key::kEnter);
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_EQ(test::Sha256Hex(ResolveRgb(screen_)), kFinishedRgb);

  Press(Key::kSpace);
  EXPECT_EQ(title.Step(static_cast<float>(kFadeLevels) * original_timing::kFadeStepSeconds), ComponentType::kTitle);
  EXPECT_TRUE(AllBlack(screen_));
  EXPECT_EQ(title.Step(0.0F), ComponentType::kOffice);
}

TEST_F(TitleTest, PlaysTheMusicFromTheTitleToTheFadeOut) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  // The logo fades in, stays and goes; the music starts with the title picture.
  for (auto tick = 0; tick < 150; ++tick) {
    ASSERT_EQ(title.Step(1.0F / 30.0F), ComponentType::kTitle);
    if (screen_.SplitRow() == 0) {
      EXPECT_FALSE(audio_.MusicPlaying());
    }
  }
  EXPECT_EQ(screen_.SplitRow(), 36);
  EXPECT_TRUE(audio_.MusicPlaying());
  Press(Key::kSpace);  // to the finished title
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_TRUE(audio_.MusicPlaying());
  Press(Key::kSpace);  // fade out
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_FALSE(audio_.MusicPlaying());
}

TEST_F(TitleTest, SkippingStartsTheMusic) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  Press(Key::kEnter);
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  EXPECT_TRUE(audio_.MusicPlaying());
  Press(Key::kEscape);
  EXPECT_EQ(title.Step(0.0F), ComponentType::kQuit);
  title.OnExit();
  EXPECT_FALSE(audio_.MusicPlaying());
}

TEST_F(TitleTest, QuitsOnEscapeAtAnyTime) {
  auto title = Title{manager_.Engine(), screen_, keys_, audio_};
  title.OnEnter();
  Press(Key::kA);
  EXPECT_EQ(title.Step(0.0F), ComponentType::kTitle);
  Press(Key::kEscape);
  EXPECT_EQ(title.Step(0.0F), ComponentType::kQuit);
}

TEST(OriginalTiming, CountsThePresentationWaits) {
  // Two vertical blanks and 65536 dbf: about an eighth of a second per fade level.
  EXPECT_NEAR(original_timing::kFadeStepSeconds, 0.125F, 0.001F);
  // 16 x 65536 + 7 x 61441 dbf and three screen passes.
  EXPECT_NEAR(original_timing::kAnimationLeadSeconds, 2.154F, 0.001F);
  // A frame of 206 words (runs left out), the conversion, then twice 40001 dbf.
  const auto frame = XorFrame{.run_first = 0, .run_count = 0, .source_words = 206};
  EXPECT_NEAR(original_timing::AnimationStepSeconds(frame, 0x9c40), 0.1462F, 0.0001F);
}

}  // namespace
}  // namespace hp2
