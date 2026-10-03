#include "host/format/cpv.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "assets.hpp"
#include "sha256.hpp"

namespace hp2::host {
namespace {

TEST(Cpv, RejectsBadInput) {
  EXPECT_EQ(DecodeCpv(std::vector<std::uint8_t>(10)).error(), CpvError::kTooShort);
  auto file = std::vector<std::uint8_t>(34);
  EXPECT_EQ(DecodeCpv(file).error(), CpvError::kBadMagic);
  file[0] = 0x12;
  file[1] = 0x34;
  EXPECT_EQ(DecodeCpv(file).error(), CpvError::kTruncatedStream);
  file.push_back(0x00);
  EXPECT_EQ(DecodeCpv(file).error(), CpvError::kZeroRun);
}

TEST(Cpv, FillsColumnsThenPlanes) {
  // One run of 32000 bytes would need 250 control bytes; use 0xff repeated 125 times per run
  // (count 0x7d) for plane 0 only, then zeros: plane 0 all set gives index 1 everywhere.
  auto file = std::vector<std::uint8_t>{0x12, 0x34};
  file.resize(34);
  for (auto run = 0; run < 64; ++run) {
    file.push_back(125);
    file.push_back(0xff);
  }
  for (auto run = 0; run < 192; ++run) {
    file.push_back(125);
    file.push_back(0x00);
  }
  const auto picture = DecodeCpv(file);
  ASSERT_TRUE(picture.has_value());
  EXPECT_EQ(picture->pixels.size(), 64000U);
  EXPECT_EQ(picture->pixels.front(), 1);
  EXPECT_EQ(picture->pixels.back(), 1);
  EXPECT_EQ(picture->consumed, file.size());
}

// Digests of the pixels decoded by scripts/hp2lib/images.py (decode_cpv), row-major.
TEST(Cpv, MatchesTheReferenceDecoder) {
  const auto present = test::DataFile("PRESENT.CPV");
  const auto logo = test::DataFile("LOGO.CPV");
  if (not present or not logo) {
    GTEST_SKIP() << "game files not present in " << test::DiskImage();
  }
  const auto present_picture = DecodeCpv(*present);
  ASSERT_TRUE(present_picture.has_value());
  EXPECT_EQ(present_picture->consumed, 18465U);
  EXPECT_EQ(present_picture->palette_st[0], 0x257);
  EXPECT_EQ(test::Sha256Hex(present_picture->pixels),
            "c0eac0ea98a6a362337370b8219865c1dc9000952babcd368619365faa8dce64");

  const auto logo_picture = DecodeCpv(*logo);
  ASSERT_TRUE(logo_picture.has_value());
  EXPECT_EQ(logo_picture->consumed, 7835U);
  EXPECT_EQ(test::Sha256Hex(logo_picture->pixels), "cb7c731478865448b31898d1253cfa70a9c7ef0ba6c04c39eb69e927ab56207b");
}

}  // namespace
}  // namespace hp2::host
