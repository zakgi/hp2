#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "core/engine_assets.hpp"
#include "core/image_view.hpp"
#include "core/palette.hpp"
#include "core/xor_animation.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/bob_bank.hpp"
#include "host/format/dif.hpp"

namespace hp2::host {

// Reads the game files once (hp.prg and DISK2_2/ in the game directory), decodes what the engine
// uses and owns the storage behind the EngineAssets views. Components receive EngineAssets by
// reference and never query the manager.
class AssetManager {
 public:
  // Loads everything; false (with a logged reason) when a file is missing or does not decode.
  [[nodiscard]] bool Load(const std::filesystem::path& game_dir);

  [[nodiscard]] const EngineAssets& Engine() const { return engine_; }

 private:
  [[nodiscard]] bool LoadExecutable(const std::filesystem::path& game_dir);
  [[nodiscard]] bool LoadLogo(const std::filesystem::path& game_dir);
  [[nodiscard]] bool LoadTitle(const std::filesystem::path& game_dir);
  [[nodiscard]] bool LoadNames(const std::filesystem::path& game_dir);
  // Points the EngineAssets views at the storage, once nothing more is added to it.
  void BuildViews();

  // hp.prg's bytes and its hunks, which are views into them.
  std::vector<std::uint8_t> executable_bytes_;
  std::optional<HunkFile> executable_;
  std::vector<std::uint8_t> logo_picture_;
  std::vector<PaletteSegment> logo_palette_;
  std::vector<std::uint8_t> title_picture_;
  std::vector<PaletteSegment> title_palette_;
  std::vector<DifFrame> title_frames_;
  std::vector<AnimationStep> title_steps_;
  std::vector<BobImage> names_;
  // The views' own storage: runs per frame, frames, images.
  std::vector<std::vector<XorRun>> title_run_views_;
  std::vector<XorFrame> title_frame_views_;
  std::vector<ImageView> name_views_;
  EngineAssets engine_;
};

// The file named `name` in `directory`, matched without regard to case: AmigaDOS names are case
// insensitive, and the disk has e.g. Sirene.snd where the program asks for SIRENE.SND.
[[nodiscard]] std::optional<std::filesystem::path> FindFile(const std::filesystem::path& directory,
                                                            std::string_view name);

// The whole file, or empty when it cannot be read.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path& path);

}  // namespace hp2::host
