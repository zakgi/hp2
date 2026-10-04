#include "host/format/color.hpp"

#include <gtest/gtest.h>

#include "core/palette.hpp"

namespace hp2::host {
namespace {

TEST(Color, DecodesAmigaWords) {
  EXPECT_EQ(FromAmiga(0x4af), (Rgb{.red = 0x44, .green = 0xaa, .blue = 0xff}));
  EXPECT_EQ(FromAmiga(0x000), Rgb{});
}

TEST(Color, DecodesAtariStWords) {
  EXPECT_EQ(FromAtariSt(0x777), (Rgb{.red = 0xff, .green = 0xff, .blue = 0xff}));
  EXPECT_EQ(FromAtariSt(0x257), (Rgb{.red = 73, .green = 182, .blue = 255}));
  EXPECT_EQ(FromColorWord(0x257, ColorFormat::kAtariSt), FromAtariSt(0x257));
  EXPECT_EQ(FromColorWord(0x257, ColorFormat::kAmiga), FromAmiga(0x257));
}

}  // namespace
}  // namespace hp2::host
