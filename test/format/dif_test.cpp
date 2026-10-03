#include "host/format/dif.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "assets.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/cpv.hpp"
#include "sha256.hpp"

namespace hp2::host {
namespace {

// A one-frame file: long count 1, long offset 8, then `frame`.
std::vector<std::uint8_t> OneFrame(const std::vector<std::uint8_t>& frame) {
  auto file = std::vector<std::uint8_t>{0, 0, 0, 1, 0, 0, 0, 8};
  file.insert(file.end(), frame.begin(), frame.end());
  return file;
}

TEST(Dif, ConvertsPlaneWordsToPenMasks) {
  // clang-format off
  const auto file = OneFrame({
      0x00, 0x02, 0x00, 0x06, 0x80, 0x00, 0x00, 0x01,  // plane 3 of group 0, plane 0 of group 1
      0x00, 0x01, 0x00, 0xa0, 0xff, 0xff,              // plane 0 of row 1's first group
      0x00, 0x00});
  // clang-format on
  const auto frames = DecodeDif(file);
  ASSERT_TRUE(frames.has_value());
  ASSERT_EQ(frames->size(), 1U);
  const auto& frame = frames->front();
  EXPECT_EQ(frame.words, 3U);
  ASSERT_EQ(frame.runs.size(), 2U);
  EXPECT_EQ(frame.runs[0].offset, 0U);
  ASSERT_EQ(frame.runs[0].masks.size(), 32U);
  EXPECT_EQ(frame.runs[0].masks[0], 8);
  EXPECT_EQ(frame.runs[0].masks[1], 0);
  EXPECT_EQ(frame.runs[0].masks[31], 1);
  EXPECT_EQ(frame.runs[1].offset, 320U);
  EXPECT_EQ(frame.runs[1].masks, std::vector<std::uint8_t>(16, 1));
}

TEST(Dif, RejectsBadFiles) {
  EXPECT_EQ(DecodeDif(std::vector<std::uint8_t>{0, 0, 0, 2, 0, 0, 0, 8}).error(), DifError::kTooShort);
  EXPECT_EQ(DecodeDif(OneFrame({0x00, 0x01, 0x00, 0x02})).error(), DifError::kFrameOutOfFile);
  EXPECT_EQ(DecodeDif(OneFrame({0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00})).error(), DifError::kOddOffset);
  EXPECT_EQ(DecodeDif(OneFrame({0x00, 0x01, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00})).error(), DifError::kRunPastScreen);
}

// PRESENT.CPV after every step of the play list, from scripts/reference_digests.py ("animated
// pixels").
TEST(Dif, MatchesTheReferencePlayer) {
  const auto executable = test::Executable();
  const auto animation = test::DataFile("PRESENT.DIF");
  const auto picture = test::DataFile("PRESENT.CPV");
  if (not executable or not animation or not picture) {
    GTEST_SKIP() << "game files not present in " << test::GameDir();
  }
  const auto hunks = HunkFile::FromBytes(std::as_bytes(std::span{*executable}));
  ASSERT_TRUE(hunks.has_value());
  const auto frames = DecodeDif(*animation);
  ASSERT_TRUE(frames.has_value());
  ASSERT_EQ(frames->size(), 19U);

  // presentPlayList (1:2870).
  const auto steps = ReadPlayList(hunks->hunks[1], 0x2870, frames->size());
  ASSERT_TRUE(steps.has_value());
  ASSERT_EQ(steps->size(), 27U);
  EXPECT_EQ(steps->front().frame, 0);
  EXPECT_EQ(steps->front().delay, 0x9c40);
  EXPECT_EQ(steps->back().frame, 18);
  EXPECT_EQ(steps->back().delay, 0);
  EXPECT_EQ(ReadPlayList(hunks->hunks[1], 0x2870, 18).error(), PlayListError::kUnknownFrame);

  auto pixels = DecodeCpv(*picture).value().pixels;
  for (const auto& step : *steps) {
    for (const auto& run : (*frames)[step.frame].runs) {
      for (auto index = std::size_t{0}; index < run.masks.size(); ++index) {
        pixels[run.offset + index] ^= run.masks[index];
      }
    }
  }
  EXPECT_EQ(test::Sha256Hex(pixels), "d3c24e325172ab28c43631b326e531981a6c1319e4e88fdf2b0977e4414c24b2");
}

}  // namespace
}  // namespace hp2::host
