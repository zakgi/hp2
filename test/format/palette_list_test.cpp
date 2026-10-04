#include "host/format/palette_list.hpp"

#include <gtest/gtest.h>

#include <span>

#include "assets.hpp"
#include "host/format/amiga_hunk.hpp"

namespace hp2::host {
namespace {

TEST(PaletteList, ReadsTheGamesLists) {
  const auto file = test::Executable();
  if (not file) {
    GTEST_SKIP() << "game files not present in " << test::DiskImage();
  }
  const auto hunks = HunkFile::FromBytes(std::as_bytes(std::span{*file}));
  ASSERT_TRUE(hunks.has_value());
  ASSERT_EQ(hunks->hunks.size(), 2U);
  const auto& code = hunks->hunks[0];
  const auto& data = hunks->hunks[1];

  // titlePalette (1:28e0): ST colors, sky rows 0-35, PRESENT.CPV's colors from row 36.
  const auto title = ReadPaletteList(data, 0x28e0, ColorFormat::kAtariSt);
  ASSERT_TRUE(title.has_value());
  ASSERT_EQ(title->size(), 2U);
  EXPECT_EQ((*title)[1].first_row, 36);
  EXPECT_EQ((*title)[0].count, 16);
  // The stored ST word 0x257, decoded.
  EXPECT_EQ((*title)[0].colors[0], FromAtariSt(0x257));

  // viewPalette (0:a520): 39 segments, the dashboard palette at row 132.
  const auto view = ReadPaletteList(code, 0xa520, ColorFormat::kAmiga);
  ASSERT_TRUE(view.has_value());
  ASSERT_EQ(view->size(), 39U);
  EXPECT_EQ(view->back().first_row, 132);
  EXPECT_EQ((*view)[2].first_row, 20);
  EXPECT_EQ((*view)[2].first_register, 8);
  EXPECT_EQ((*view)[2].Color(0), FromAmiga(0x06f));

  // A header that runs past the end of the hunk.
  EXPECT_EQ(ReadPaletteList(data, 0x3f98, ColorFormat::kAmiga).error(), PaletteListError::kOutOfHunk);
}

}  // namespace
}  // namespace hp2::host
