#include "host/format/music_file.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "assets.hpp"

namespace hp2::host {
namespace {

TEST(MusicFile, RejectsBadFiles) {
  EXPECT_EQ(DecodeMusicFile(std::vector<std::uint8_t>(0x100)).error(), MusicFileError::kTooShort);
  auto file = std::vector<std::uint8_t>(0x3c + 0x258);
  EXPECT_EQ(DecodeMusicFile(file).error(), MusicFileError::kBadSongLength);
  file[0x3c + 0x1d6] = 1;  // one position, pattern 0, which is missing
  EXPECT_EQ(DecodeMusicFile(file).error(), MusicFileError::kPatternsOutOfFile);
  file.resize(file.size() + 1024);
  file[3] = 2;  // instrument 1 has two bytes, which are missing
  EXPECT_EQ(DecodeMusicFile(file).error(), MusicFileError::kSamplesOutOfFile);
  file.resize(file.size() + 2);
  EXPECT_TRUE(DecodeMusicFile(file).has_value());
}

TEST(MusicFile, DecodesTheTitleMusic) {
  const auto file = test::DataFile("HIGHWAY.MUS");
  if (not file) {
    GTEST_SKIP() << "game files not present in " << test::DiskImage();
  }
  const auto music = DecodeMusicFile(*file);
  ASSERT_TRUE(music.has_value());
  EXPECT_EQ(music->timer, 0x3199);
  EXPECT_EQ(music->positions, (std::vector<std::uint8_t>{0, 1, 2, 3, 4, 3, 4, 5, 6}));
  EXPECT_EQ(music->notes.size(), 7U * 64U * 4U);
  // The instruments fill the file to its end.
  EXPECT_EQ(file->size(), 0x3cU + 0x258U + (7U * 1024U) + music->sample_data.size());

  // "nappes": a head, then a loop over the rest.
  const auto& nappes = music->samples[2];
  EXPECT_EQ(nappes.size, 20'828U);
  EXPECT_EQ(nappes.length, 6'306U);
  EXPECT_EQ(nappes.loop_start, 6'307U);
  EXPECT_EQ(nappes.loop_length, 14'520U);
  EXPECT_EQ(nappes.volume, 63);
  // "shaker": a one-shot at half volume.
  const auto& shaker = music->samples[12];
  EXPECT_EQ(shaker.length, 1'096U);
  EXPECT_EQ(shaker.loop_length, 2U);
  EXPECT_EQ(shaker.volume, 31);
  EXPECT_EQ(shaker.offset, 5'884U + 26'608U + 20'828U);

  // The first row: the "strat", "nappes" and "Pied", and a release.
  EXPECT_EQ(music->notes[0].period, 604);
  EXPECT_EQ(music->notes[0].sample, 1);
  EXPECT_EQ(music->notes[1].sample, 3);
  EXPECT_EQ(music->notes[2].sample, 15);
  EXPECT_EQ(music->notes[3].period, ModuleNote::kRelease);
}

}  // namespace
}  // namespace hp2::host
