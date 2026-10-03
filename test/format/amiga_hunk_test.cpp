#include "host/format/amiga_hunk.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "assets.hpp"

namespace hp2::host {
namespace {

TEST(AmigaHunk, RejectsNonExecutables) {
  const auto bytes = std::vector<std::uint8_t>{0x00, 0x00, 0x03, 0xf2};
  EXPECT_EQ(HunkFile::FromBytes(std::as_bytes(std::span{bytes})).error(), HunkError::kNotExecutable);
  const auto header_only = std::vector<std::uint8_t>{0x00, 0x00, 0x03, 0xf3, 0x00, 0x00};
  EXPECT_EQ(HunkFile::FromBytes(std::as_bytes(std::span{header_only})).error(), HunkError::kTruncated);
}

TEST(AmigaHunk, ReadsTheGameUnrelocated) {
  const auto file = test::Executable();
  if (not file) {
    GTEST_SKIP() << "game files not present in " << test::DiskImage();
  }
  const auto hunks = HunkFile::FromBytes(std::as_bytes(std::span{*file}));
  ASSERT_TRUE(hunks.has_value());
  ASSERT_EQ(hunks->hunks.size(), 2U);
  const auto& code = hunks->hunks[0];
  EXPECT_EQ(code.size, 0xe8a8U);
  EXPECT_EQ(hunks->hunks[1].size, 0x3f9cU);
  EXPECT_EQ(code.relocations.at(0).size(), 1259U);
  EXPECT_EQ(code.relocations.at(1).size(), 1339U);

  // start (0:0000): jmp main, whose operand is relocated against hunk 0 and holds main's
  // offset in it (0:a908).
  EXPECT_EQ(code.Read<std::uint16_t>(0), 0x4ef9);
  EXPECT_EQ(code.Read<std::uint32_t>(2), 0xa908U);
  EXPECT_TRUE(std::ranges::contains(code.relocations.at(0), 2U));
  // roadCellShapes[0] (0:7812) points at 0:7846.
  EXPECT_EQ(code.Read<std::uint32_t>(0x7812), 0x7846U);
  EXPECT_FALSE(code.Read<std::uint32_t>(0xe8a8 - 2).has_value());
}

}  // namespace
}  // namespace hp2::host
