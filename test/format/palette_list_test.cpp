#include "host/format/palette_list.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>

#include "assets.hpp"
#include "core/palette.hpp"
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

  // titlePalette (1:28e0): ST colors, the sky's set, then PRESENT.CPV's.
  const auto title = ReadPaletteList(data, 0x28e0, ColorFormat::kAtariSt);
  ASSERT_TRUE(title.has_value());
  ASSERT_EQ(title->size(), 2 * kColorRegisterCount);
  // The stored ST word 0x257, decoded.
  EXPECT_EQ((*title)[0], FromAtariSt(0x257));

  // viewPalette (0:a520): 39 segments. The third writes register 8 only; the other registers keep
  // the second set's colors.
  const auto view = ReadPaletteList(code, 0xa520, ColorFormat::kAmiga);
  ASSERT_TRUE(view.has_value());
  ASSERT_EQ(view->size(), 39 * kColorRegisterCount);
  const auto second = std::span{*view}.subspan(kColorRegisterCount, kColorRegisterCount);
  const auto third = std::span{*view}.subspan(2 * kColorRegisterCount, kColorRegisterCount);
  EXPECT_EQ(third[8], FromAmiga(0x06f));
  EXPECT_TRUE(std::ranges::equal(third.first(8), second.first(8)));
  EXPECT_TRUE(std::ranges::equal(third.subspan(9), second.subspan(9)));

  // A header that runs past the end of the hunk.
  EXPECT_EQ(ReadPaletteList(data, 0x3f98, ColorFormat::kAmiga).error(), PaletteListError::kOutOfHunk);
}

}  // namespace
}  // namespace hp2::host
