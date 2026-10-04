#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "core/engine_assets.hpp"
#include "core/palette.hpp"
#include "core/road_map.hpp"
#include "core/sprite_bank.hpp"
#include "core/xor_animation.hpp"
#include "host/adf.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/music_file.hpp"
#include "host/format/road_map.hpp"

namespace hp2::host {

// Reads the game files once from the game's disk image (hp.prg and DISK2_2/), decodes everything
// the engine uses and owns the storage behind the EngineAssets views. Components receive
// EngineAssets by reference and never query the manager.
class AssetManager {
 public:
  // Loads everything from the ADF at `disk_image`; false (with a logged reason) when the image or
  // a file is missing or does not decode.
  [[nodiscard]] bool Load(const std::filesystem::path& disk_image);

  [[nodiscard]] const EngineAssets& Engine() const { return engine_; }

 private:
  struct BankStorage {
    std::vector<std::uint8_t> pixels;
    std::vector<SpriteRange> sprites;
  };
  struct SoundStorage {
    std::vector<std::int8_t> samples;
    std::uint32_t rate_hz{};
    std::uint32_t loop_start{};
    std::uint32_t loop_length{};
  };
  struct AnimationStorage {
    std::vector<std::uint8_t> masks;
    std::vector<XorRun> runs;
    std::vector<XorFrame> frames;
    std::vector<AnimationStep> steps;
  };

  [[nodiscard]] bool LoadExecutable(const AdfImageManager& disk);
  [[nodiscard]] bool LoadPictures(const AdfImageManager& disk);
  [[nodiscard]] bool LoadPalettes();
  [[nodiscard]] bool LoadBanks(const AdfImageManager& disk);
  [[nodiscard]] bool LoadFonts(const AdfImageManager& disk);
  [[nodiscard]] bool LoadSounds(const AdfImageManager& disk);
  [[nodiscard]] bool LoadTitleAnimation(const AdfImageManager& disk);
  [[nodiscard]] bool LoadMusic(const AdfImageManager& disk);
  [[nodiscard]] bool LoadRoadMap(const AdfImageManager& disk);
  [[nodiscard]] bool LoadScenery(const AdfImageManager& disk);
  [[nodiscard]] bool LoadRoadShapes();
  // Points the EngineAssets views at the storage, once nothing more is added to it.
  void BuildViews();

  // hp.prg's bytes and its hunks, which are views into them.
  std::vector<std::uint8_t> executable_bytes_;
  std::optional<HunkFile> executable_;
  std::array<std::vector<std::uint8_t>, kEnginePictureCount> pictures_;
  // The pictures' header palettes, Atari ST colours.
  std::array<std::array<std::uint16_t, kColorRegisterCount>, kEnginePictureCount> picture_palettes_{};
  std::array<std::vector<PaletteSegment>, kEnginePaletteCount> palettes_;
  std::array<BankStorage, kEngineBankCount> banks_;
  std::array<std::vector<std::uint8_t>, kEngineFontCount> fonts_;
  std::array<SoundStorage, kEngineSoundCount> sounds_;
  AnimationStorage title_animation_;
  MusicFile title_music_;
  RoadMap road_map_;
  std::vector<PlacedObject> scenery_objects_;
  std::array<IndexRange, kRoadCellTypeCount> scenery_ranges_{};
  std::vector<ShapePoint> road_shape_points_;
  std::array<IndexRange, kRoadCellTypeCount> road_shape_ranges_{};
  EngineAssets engine_{};
};

}  // namespace hp2::host
