#include "host/format/font.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "assets.hpp"
#include "sha256.hpp"

namespace hp2::host {
namespace {

TEST(Font, BuildsPixelsFromTheFourPlanesOfEachRow) {
  auto file = std::vector<std::uint8_t>(32);
  file[0] = 0x80;   // row 0, plane 0: pixel 0
  file[3] = 0x81;   // row 0, plane 3: pixels 0 and 7
  file[30] = 0x01;  // row 7, plane 2: pixel 7
  const auto font = DecodeFont(file);
  ASSERT_TRUE(font.has_value());
  EXPECT_EQ(font->GlyphCount(), 1U);
  EXPECT_EQ(font->pixels[0], 0b1001);
  EXPECT_EQ(font->pixels[7], 0b1000);
  EXPECT_EQ(font->pixels[63], 0b0100);
  EXPECT_EQ(font->pixels[1], 0);
}

TEST(Font, RejectsPartialGlyphs) {
  EXPECT_EQ(DecodeFont(std::vector<std::uint8_t>(33)).error(), FontError::kBadSize);
  EXPECT_EQ(DecodeFont(std::vector<std::uint8_t>{}).error(), FontError::kBadSize);
}

// Digests from scripts/reference_digests.py ("font N pixels").
TEST(Font, MatchesTheReferenceDecoder) {
  const auto first = test::DataFile("LETTRE1.BIN");
  const auto second = test::DataFile("LETTRE2.BIN");
  if (not first or not second) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  const auto font1 = DecodeFont(*first);
  const auto font2 = DecodeFont(*second);
  ASSERT_TRUE(font1.has_value());
  ASSERT_TRUE(font2.has_value());
  EXPECT_EQ(font1->GlyphCount(), 94U);
  EXPECT_EQ(test::Sha256Hex(font1->pixels), "01fa882260e1dbdb69960fdad65fb65c54a777adbcdeafddf8a7d8f67ceec9d9");
  EXPECT_EQ(test::Sha256Hex(font2->pixels), "0fd828ca4316f298720193fc932e0179a8bdc232e46a5081628648c5e4de01b9");
}

}  // namespace
}  // namespace hp2::host
