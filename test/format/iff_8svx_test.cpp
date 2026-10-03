#include "host/format/iff_8svx.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "assets.hpp"
#include "host/format/iff.hpp"

namespace hp2::host {
namespace {

void AppendLong(std::vector<std::uint8_t>& data, std::uint32_t value) {
  data.insert(data.end(), {static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
                           static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)});
}

// A FORM 8SVX: VHDR (one-shot 2, repeat 4, 8000 Hz), an odd-sized ANNO, BODY of 6 samples.
std::vector<std::uint8_t> Sound(std::uint8_t compression = 0) {
  auto chunks = std::vector<std::uint8_t>{};
  AppendLong(chunks, FourCc("VHDR"));
  AppendLong(chunks, 20);
  AppendLong(chunks, 2);
  AppendLong(chunks, 4);
  AppendLong(chunks, 0);
  chunks.insert(chunks.end(), {0x1f, 0x40, 1, compression});
  AppendLong(chunks, 0x10000);
  AppendLong(chunks, FourCc("ANNO"));
  AppendLong(chunks, 3);
  chunks.insert(chunks.end(), {'a', 'b', 'c', 0});
  AppendLong(chunks, FourCc("BODY"));
  AppendLong(chunks, 6);
  chunks.insert(chunks.end(), {1, 2, 3, 0xfd, 0xfe, 0xff});
  auto file = std::vector<std::uint8_t>{};
  AppendLong(file, FourCc("FORM"));
  AppendLong(file, static_cast<std::uint32_t>(4 + chunks.size()));
  AppendLong(file, FourCc("8SVX"));
  file.insert(file.end(), chunks.begin(), chunks.end());
  return file;
}

TEST(Iff8Svx, ReadsTheVoiceHeaderAndBody) {
  const auto file = Sound();
  const auto sound = Iff8SvxAsset::FromRawData(std::as_bytes(std::span{file}));
  ASSERT_TRUE(sound.has_value());
  EXPECT_EQ(sound->GetSampleRate(), 8000U);
  EXPECT_EQ(sound->GetOneShotSamples(), 2U);
  EXPECT_EQ(sound->GetRepeatSamples(), 4U);
  ASSERT_EQ(sound->GetSamples().size(), 6U);
  EXPECT_EQ(std::to_integer<std::int8_t>(sound->GetSamples()[5]), -1);
}

TEST(Iff8Svx, RefusesOtherFilesAndCompression) {
  const auto compressed = Sound(kCompressionFibonacciDelta);
  EXPECT_FALSE(Iff8SvxAsset::FromRawData(std::as_bytes(std::span{compressed})).has_value());
  const auto raw = std::vector<std::uint8_t>(64, 0x10);
  EXPECT_FALSE(Iff8SvxAsset::FromRawData(std::as_bytes(std::span{raw})).has_value());
}

// MOTEUR.SND and DERAP.SND are 8SVX; the other three sounds are raw samples.
TEST(Iff8Svx, DecodesTheGamesSounds) {
  const auto engine = test::DataFile("MOTEUR.SND");
  const auto skid = test::DataFile("DERAP.SND");
  const auto shot = test::DataFile("TIR.SND");
  if (not engine or not skid or not shot) {
    GTEST_SKIP() << "game disk not present at " << test::DiskImage();
  }
  const auto engine_sound = Iff8SvxAsset::FromRawData(std::as_bytes(std::span{*engine}));
  ASSERT_TRUE(engine_sound.has_value());
  EXPECT_EQ(engine_sound->GetSampleRate(), 6'628U);
  EXPECT_EQ(engine_sound->GetOneShotSamples(), 0U);
  EXPECT_EQ(engine_sound->GetRepeatSamples(), 0x254U);
  EXPECT_EQ(engine_sound->GetSamples().size(), 0x254U);
  const auto skid_sound = Iff8SvxAsset::FromRawData(std::as_bytes(std::span{*skid}));
  ASSERT_TRUE(skid_sound.has_value());
  EXPECT_EQ(skid_sound->GetSampleRate(), 3'977U);
  EXPECT_EQ(skid_sound->GetOneShotSamples(), 0x1c1cU);
  EXPECT_EQ(skid_sound->GetSamples().size(), 0x1c1cU);
  EXPECT_FALSE(Iff8SvxAsset::FromRawData(std::as_bytes(std::span{*shot})).has_value());
}

}  // namespace
}  // namespace hp2::host
