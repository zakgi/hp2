#include "host/format/bob_bank.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "assets.hpp"
#include "sha256.hpp"

namespace hp2::host {
namespace {

TEST(BobBank, MapsStoredPlanesToTheChosenScreenPlanes) {
  // One 16x1 image, 2 stored planes going to screen planes 1 and 3 (flags 0x0a02).
  // clang-format off
  const auto file = std::vector<std::uint8_t>{
      0x00, 0x01, 0x00, 0x04,                                      // count, offset
      0x0a, 0x02, 0x00, 0x01, 0x00, 0x01, 0xff, 0xfe, 0x00, 0x03,  // flags, width, height, origin
      0x80, 0x00,                                                  // stored plane 0 -> screen plane 1
      0xc0, 0x00};                                                 // stored plane 1 -> screen plane 3
  // clang-format on
  const auto bank = DecodeBobBank(file);
  ASSERT_TRUE(bank.has_value());
  ASSERT_EQ(bank->size(), 1U);
  const auto& image = bank->front();
  EXPECT_EQ(image.width, 16);
  EXPECT_EQ(image.height, 1);
  EXPECT_EQ(image.origin_x, -2);
  EXPECT_EQ(image.origin_y, 3);
  EXPECT_EQ(image.pixels[0], 0b1010);
  EXPECT_EQ(image.pixels[1], 0b1000);
  EXPECT_EQ(image.pixels[2], 0);
}

TEST(BobBank, RejectsBadBanks) {
  EXPECT_EQ(DecodeBobBank(std::vector<std::uint8_t>{0x00}).error(), BobBankError::kTooShort);
  EXPECT_EQ(DecodeBobBank(std::vector<std::uint8_t>{0x00, 0x02, 0x00, 0x06}).error(), BobBankError::kTooShort);
  // The image's planes run past the end.
  EXPECT_EQ(DecodeBobBank(std::vector<std::uint8_t>{0x00, 0x01, 0x00, 0x04, 0x0f, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00,
                                                    0x00, 0x00, 0x00, 0x12})
                .error(),
            BobBankError::kImageOutOfBank);
  // Three stored planes, two chosen.
  EXPECT_EQ(DecodeBobBank(std::vector<std::uint8_t>{0x00, 0x01, 0x00, 0x04, 0x03, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00,
                                                    0x00, 0x00, 0x00})
                .error(),
            BobBankError::kTooFewTargetPlanes);
}

// Digests from scripts/reference_digests.py ("name N pixels").
TEST(BobBank, MatchesTheReferenceDecoder) {
  const auto file = test::DataFile("NAME.IMG");
  if (not file) {
    GTEST_SKIP() << "game files not present in " << test::GameDir();
  }
  const auto bank = DecodeBobBank(*file);
  ASSERT_TRUE(bank.has_value());
  ASSERT_EQ(bank->size(), 3U);
  EXPECT_EQ((*bank)[1].width, 240);
  EXPECT_EQ((*bank)[1].height, 10);
  EXPECT_EQ(test::Sha256Hex((*bank)[0].pixels), "008e27796cef956b3599402dca75d01e3bee21ad52dbe294eff6c51c95eb9d51");
  EXPECT_EQ(test::Sha256Hex((*bank)[1].pixels), "d6413f613b466bd31a3209dcd3ef4401315cf501b06c45e30f27749bbb125d2b");
  EXPECT_EQ(test::Sha256Hex((*bank)[2].pixels), "b5b077364c56f9dce0cdb163e6bdc9560a3ead30be11fdb8ae7b8336220906cf");
}

}  // namespace
}  // namespace hp2::host
