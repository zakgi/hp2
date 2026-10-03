#include "host/asset_manager.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/screen.hpp"
#include "host/adf.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/bob_bank.hpp"
#include "host/format/cpv.hpp"
#include "host/format/dif.hpp"
#include "host/format/font.hpp"
#include "host/format/iff_8svx.hpp"
#include "host/format/music_file.hpp"
#include "host/format/object_placement.hpp"
#include "host/format/palette_list.hpp"
#include "host/format/road_map.hpp"

namespace hp2::host {

namespace {

// AmigaDOS paths on the game disk.
constexpr auto kExecutableName = std::string_view{"hp.prg"};
constexpr auto kDataDirectory = std::string_view{"DISK2_2"};
constexpr auto kHunkCount = std::size_t{2};

// In EnginePicture order.
constexpr auto kPictureFiles =
    std::to_array<std::string_view>({"LOGO.CPV", "PRESENT.CPV", "BUREAU.CPV", "STATION.CPV", "PAGE_F1.CPV",
                                     "PAGE_F2.CPV", "PAGE_F3.CPV", "PAGE_F4.CPV", "PAGE_F5.CPV", "PAGE_F6.CPV"});
static_assert(kPictureFiles.size() == kEnginePictureCount);

// Where each EnginePalette comes from: a picture's header, or a list in the executable.
struct PaletteSource {
  std::optional<EnginePicture> picture;
  HunkOffset list;
  ColorFormat format;
};
constexpr PaletteSource FromPicture(EnginePicture picture) {
  return PaletteSource{.picture = picture, .list = HunkOffset{}, .format = ColorFormat::kAtariSt};
}
constexpr PaletteSource FromList(std::uint32_t hunk, std::uint32_t offset, ColorFormat format) {
  return PaletteSource{.picture = std::nullopt, .list = HunkOffset{.hunk = hunk, .offset = offset}, .format = format};
}
constexpr auto kPaletteSources = std::to_array<PaletteSource>({
    FromPicture(EnginePicture::kLogo),
    FromPicture(EnginePicture::kStation),
    FromPicture(EnginePicture::kSpinOut),
    FromPicture(EnginePicture::kTyresGone),
    FromPicture(EnginePicture::kOutOfFuel),
    FromPicture(EnginePicture::kOverheated),
    FromPicture(EnginePicture::kWrecked),
    FromPicture(EnginePicture::kArrest),
    FromList(1, 0x28e0, ColorFormat::kAtariSt),  // titlePalette
    FromList(0, 0xca94, ColorFormat::kAmiga),    // officePalette
    FromList(0, 0xca6e, ColorFormat::kAmiga),    // officePaletteDim
    FromList(0, 0x7766, ColorFormat::kAmiga),    // scorePalette
    FromList(0, 0xa520, ColorFormat::kAmiga),    // viewPalette
    FromList(0, 0xa734, ColorFormat::kAmiga),    // viewPaletteRed
});
static_assert(kPaletteSources.size() == kEnginePaletteCount);

// In EngineBank order.
constexpr auto kBankFiles = std::to_array<std::string_view>(
    {"NAME.IMG",     "BUREAU.IMG",   "STATION.IMG",  "GAME_SCO.IMG", "BALLE.IMG",    "CACTUS.IMG",  "BUISSON.IMG",
     "CAILLOUX.IMG", "DEC_FOND.IMG", "DES_TABB.IMG", "PAN_POT.IMG",  "PAN_GAU.IMG",  "PAN_CRO.IMG", "PAN_DRO.IMG",
     "PAN_ARR.IMG",  "PAN_PRO.IMG",  "PST_FLG.IMG",  "PST_FLD.IMG",  "PST_PRO.IMG",  "PST_STA.IMG", "VOITURE0.IMG",
     "VOITURE1.IMG", "VOITURE2.IMG", "VOITURE3.IMG", "VOITURE4.IMG", "VOITURE5.IMG", "VOITURE6.IMG"});
static_assert(kBankFiles.size() == kEngineBankCount);

// In EngineFont order.
constexpr auto kFontFiles = std::to_array<std::string_view>({"LETTRE1.BIN", "LETTRE2.BIN"});
static_assert(kFontFiles.size() == kEngineFontCount);

// The sound effects in EngineSound order. MOTEUR.SND and DERAP.SND are IFF 8SVX and carry their
// rate and loop; the other three are bare samples, played at the rate their period in the sound
// code gives on the NTSC clock (docs/formats.md, "Sounds: .SND"), the siren looped.
struct SoundSource {
  std::string_view file;
  std::uint16_t period;  // 0 for an 8SVX sound
  bool looped;
};
constexpr auto kNtscPaulaClockHz = std::uint32_t{3'579'545};
constexpr auto kSoundSources = std::to_array<SoundSource>({
    {.file = "MOTEUR.SND", .period = 0, .looped = false},
    {.file = "SIRENE.SND", .period = 864, .looped = true},
    {.file = "TIR.SND", .period = 427, .looped = false},
    {.file = "DERAP.SND", .period = 0, .looped = false},
    {.file = "CHOC.SND", .period = 640, .looped = false},
});
static_assert(kSoundSources.size() == kEngineSoundCount);

constexpr auto kTitleAnimationFile = std::string_view{"PRESENT.DIF"};
// presentPlayList (1:2870): PRESENT.DIF's play list.
constexpr auto kTitlePlayList = HunkOffset{.hunk = 1, .offset = 0x2870};
constexpr auto kTitleMusicFile = std::string_view{"HIGHWAY.MUS"};
constexpr auto kRoadMapFile = std::string_view{"CARTE.BIN"};
constexpr auto kSceneryFile = std::string_view{"COOR_OBJ.BIN"};

// The data file `name` from the disk's DISK2_2/.
std::optional<std::span<const std::uint8_t>> ReadDataFile(const AdfImageManager& disk, std::string_view name) {
  auto path = std::string{kDataDirectory};
  path += '/';
  path += name;
  const auto bytes = disk.GetFile(path);
  if (not bytes) {
    spdlog::error("The disk has no {}", path);
  }
  return bytes;
}

// The data file `name` through `decode`; empty, with the reason logged, when it is missing or does
// not decode.
template <typename Decode>
auto DecodeDataFile(const AdfImageManager& disk, std::string_view name, Decode decode) {
  using Decoded = std::invoke_result_t<Decode, std::span<const std::uint8_t>>::value_type;
  auto result = std::optional<Decoded>{};
  const auto file = ReadDataFile(disk, name);
  if (file) {
    auto decoded = decode(*file);
    if (decoded) {
      result = std::move(*decoded);
    } else {
      spdlog::error("Cannot decode {} (error {})", name, static_cast<int>(decoded.error()));
    }
  }
  return result;
}

}  // namespace

bool AssetManager::Load(const std::filesystem::path& disk_image) {
  auto disk = AdfImageManager{};
  const auto loaded = disk.AddDiskImage(disk_image) and LoadExecutable(disk) and LoadPictures(disk) and
                      LoadPalettes() and LoadBanks(disk) and LoadFonts(disk) and LoadSounds(disk) and
                      LoadTitleAnimation(disk) and LoadMusic(disk) and LoadRoadMap(disk) and LoadScenery(disk);
  if (loaded) {
    BuildViews();
  }
  return loaded;
}

bool AssetManager::LoadExecutable(const AdfImageManager& disk) {
  executable_.reset();
  const auto bytes = disk.GetFile(kExecutableName);
  executable_bytes_ = bytes ? std::vector<std::uint8_t>(bytes->begin(), bytes->end()) : std::vector<std::uint8_t>{};
  auto hunks = HunkFile::FromBytes(std::as_bytes(std::span{executable_bytes_}));
  if (not hunks) {
    spdlog::error("Cannot load {} (error {})", kExecutableName, static_cast<int>(hunks.error()));
  } else if (hunks->hunks.size() != kHunkCount) {
    spdlog::error("{} has {} hunks, expected {}", kExecutableName, hunks->hunks.size(), kHunkCount);
  } else {
    executable_ = std::move(*hunks);
  }
  return executable_.has_value();
}

bool AssetManager::LoadPictures(const AdfImageManager& disk) {
  auto loaded = true;
  for (auto index = std::size_t{0}; loaded and index < kEnginePictureCount; ++index) {
    auto picture = DecodeDataFile(disk, kPictureFiles[index], DecodeCpv);
    loaded = picture.has_value();
    if (loaded) {
      pictures_[index] = std::move(picture->pixels);
      picture_palettes_[index] = picture->palette_st;
    }
  }
  return loaded;
}

bool AssetManager::LoadPalettes() {
  auto loaded = true;
  for (auto index = std::size_t{0}; loaded and index < kEnginePaletteCount; ++index) {
    const auto& source = kPaletteSources[index];
    if (source.picture) {
      auto segment = PaletteSegment{.count = kColorRegisterCount, .format = ColorFormat::kAtariSt};
      segment.colors = picture_palettes_[std::to_underlying(*source.picture)];
      palettes_[index] = {segment};
    } else {
      auto list = ReadPaletteList(executable_->hunks[source.list.hunk], source.list.offset, source.format);
      loaded = list.has_value();
      if (loaded) {
        palettes_[index] = std::move(*list);
      } else {
        spdlog::error("Cannot read the palette list at {}:{:04x} (error {})", source.list.hunk, source.list.offset,
                      static_cast<int>(list.error()));
      }
    }
  }
  return loaded;
}

bool AssetManager::LoadBanks(const AdfImageManager& disk) {
  auto loaded = true;
  for (auto index = std::size_t{0}; loaded and index < kEngineBankCount; ++index) {
    const auto images = DecodeDataFile(disk, kBankFiles[index], DecodeBobBank);
    loaded = images.has_value();
    if (loaded) {
      auto& bank = banks_[index];
      for (const auto& image : *images) {
        bank.sprites.push_back(SpriteRange{.offset = static_cast<std::uint32_t>(bank.pixels.size()),
                                           .width = image.width,
                                           .height = image.height,
                                           .origin_x = image.origin_x,
                                           .origin_y = image.origin_y});
        bank.pixels.insert(bank.pixels.end(), image.pixels.begin(), image.pixels.end());
      }
    }
  }
  return loaded;
}

bool AssetManager::LoadFonts(const AdfImageManager& disk) {
  auto loaded = true;
  for (auto index = std::size_t{0}; loaded and index < kEngineFontCount; ++index) {
    auto font = DecodeDataFile(disk, kFontFiles[index], DecodeFont);
    loaded = font.has_value();
    if (loaded) {
      fonts_[index] = std::move(font->pixels);
    }
  }
  return loaded;
}

bool AssetManager::LoadSounds(const AdfImageManager& disk) {
  auto loaded = true;
  for (auto index = std::size_t{0}; loaded and index < kEngineSoundCount; ++index) {
    const auto& source = kSoundSources[index];
    const auto file = ReadDataFile(disk, source.file);
    const auto iff = file ? Iff8SvxAsset::FromRawData(std::as_bytes(*file)) : std::nullopt;
    auto& sound = sounds_[index];
    loaded = file.has_value() and (iff.has_value() or source.period != 0);
    if (iff) {
      const auto body = iff->GetSamples();
      sound.samples.resize(body.size());
      std::ranges::transform(body, sound.samples.begin(),
                             [](std::byte value) { return std::to_integer<std::int8_t>(value); });
      sound.rate_hz = iff->GetSampleRate();
      sound.loop_start = iff->GetRepeatSamples() == 0 ? 0 : iff->GetOneShotSamples();
      sound.loop_length = iff->GetRepeatSamples();
    } else if (loaded) {
      sound.samples.resize(file->size());
      std::ranges::transform(*file, sound.samples.begin(),
                             [](std::uint8_t value) { return static_cast<std::int8_t>(value); });
      sound.rate_hz = (kNtscPaulaClockHz + (source.period / 2U)) / source.period;  // to the nearest hertz
      sound.loop_length = source.looped ? static_cast<std::uint32_t>(file->size()) : 0;
    } else {
      spdlog::error("{} is neither an 8SVX sound nor a known bare one", source.file);
    }
  }
  return loaded;
}

bool AssetManager::LoadTitleAnimation(const AdfImageManager& disk) {
  auto loaded = false;
  const auto frames = DecodeDataFile(disk, kTitleAnimationFile, DecodeDif);
  if (frames) {
    auto steps = ReadPlayList(executable_->hunks[kTitlePlayList.hunk], kTitlePlayList.offset, frames->size());
    if (steps) {
      auto& animation = title_animation_;
      animation.steps = std::move(*steps);
      for (const auto& frame : *frames) {
        animation.frames.push_back(XorFrame{.run_first = static_cast<std::uint32_t>(animation.runs.size()),
                                            .run_count = static_cast<std::uint32_t>(frame.runs.size()),
                                            .source_words = frame.words});
        for (const auto& run : frame.runs) {
          animation.runs.push_back(XorRun{.offset = run.offset,
                                          .mask_first = static_cast<std::uint32_t>(animation.masks.size()),
                                          .mask_count = static_cast<std::uint32_t>(run.masks.size())});
          animation.masks.insert(animation.masks.end(), run.masks.begin(), run.masks.end());
        }
      }
      loaded = true;
    } else {
      spdlog::error("Cannot read the title play list at {}:{:04x} (error {})", kTitlePlayList.hunk,
                    kTitlePlayList.offset, static_cast<int>(steps.error()));
    }
  }
  return loaded;
}

bool AssetManager::LoadMusic(const AdfImageManager& disk) {
  auto music = DecodeDataFile(disk, kTitleMusicFile, DecodeMusicFile);
  if (music) {
    title_music_ = std::move(*music);
  }
  return music.has_value();
}

bool AssetManager::LoadRoadMap(const AdfImageManager& disk) {
  const auto map = DecodeDataFile(disk, kRoadMapFile, DecodeRoadMap);
  if (map) {
    road_map_ = *map;
  }
  return map.has_value();
}

bool AssetManager::LoadScenery(const AdfImageManager& disk) {
  const auto placement = DecodeDataFile(disk, kSceneryFile, DecodeObjectPlacement);
  if (placement) {
    for (auto type = std::size_t{0}; type < kRoadCellTypeCount; ++type) {
      const auto& objects = placement->cell_types[type];
      scenery_ranges_[type] = ObjectRange{.first = static_cast<std::uint32_t>(scenery_objects_.size()),
                                          .count = static_cast<std::uint32_t>(objects.size())};
      scenery_objects_.insert(scenery_objects_.end(), objects.begin(), objects.end());
    }
  }
  return placement.has_value();
}

void AssetManager::BuildViews() {
  for (auto index = std::size_t{0}; index < kEnginePictureCount; ++index) {
    engine_.pictures[index] = ImageView{.width = Screen::kWidth, .height = Screen::kHeight, .pixels = pictures_[index]};
  }
  for (auto index = std::size_t{0}; index < kEnginePaletteCount; ++index) {
    engine_.palettes[index] = palettes_[index];
  }
  for (auto index = std::size_t{0}; index < kEngineBankCount; ++index) {
    engine_.banks[index] = SpriteBank{.pixels = banks_[index].pixels, .sprites = banks_[index].sprites};
  }
  for (auto index = std::size_t{0}; index < kEngineFontCount; ++index) {
    engine_.fonts[index] = BitmapFont{.pixels = fonts_[index]};
  }
  for (auto index = std::size_t{0}; index < kEngineSoundCount; ++index) {
    const auto& sound = sounds_[index];
    engine_.sounds[index] = SoundSample{.samples = sound.samples,
                                        .rate_hz = sound.rate_hz,
                                        .loop_start = sound.loop_start,
                                        .loop_length = sound.loop_length};
  }
  engine_.title_animation = XorAnimation{.masks = title_animation_.masks,
                                         .runs = title_animation_.runs,
                                         .frames = title_animation_.frames,
                                         .steps = title_animation_.steps};
  engine_.title_music = MusicModule{.sample_data = title_music_.sample_data,
                                    .samples = title_music_.samples,
                                    .positions = title_music_.positions,
                                    .notes = title_music_.notes,
                                    .timer = title_music_.timer};
  engine_.road_map = RoadMapView{.cells = road_map_.cells};
  engine_.scenery = Scenery{.objects = scenery_objects_, .cell_types = scenery_ranges_};
}

}  // namespace hp2::host
