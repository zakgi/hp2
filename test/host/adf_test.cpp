#include "host/adf.hpp"

#include <gtest/gtest.h>

#include "assets.hpp"

namespace hp2::host {
namespace {

TEST(Adf, FindsFilesWhateverTheCase) {
  auto disk = AdfImageManager{};
  if (not disk.AddDiskImage(test::DiskImage())) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  // The disk has Sirene.snd and bureau.cpv; the program asks for SIRENE.SND and BUREAU.CPV.
  EXPECT_TRUE(disk.GetFile("DISK2_2/SIRENE.SND").has_value());
  EXPECT_TRUE(disk.GetFile("DISK2_2/BUREAU.CPV").has_value());
  EXPECT_FALSE(disk.GetFile("DISK2_2/MISSING.BIN").has_value());
  EXPECT_EQ(disk.GetFile("disk2_2/logo.cpv")->size(), 7'835U);
  EXPECT_EQ(disk.GetFile("DISK2_2/PRESENT.DIF")->size(), 71'532U);
  EXPECT_EQ(disk.GetFile("DISK2_2/HIGHWAY.MUS")->size(), 66'380U);
}

TEST(Adf, RefusesAMissingImage) {
  auto disk = AdfImageManager{};
  EXPECT_FALSE(disk.AddDiskImage(test::DiskImage().parent_path() / "missing.adf"));
  EXPECT_FALSE(disk.GetFile("hp.prg").has_value());
}

}  // namespace
}  // namespace hp2::host
