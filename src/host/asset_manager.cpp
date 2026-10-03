#include "host/asset_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/screen.hpp"
#include "host/format/amiga_hunk.hpp"
#include "host/format/bob_bank.hpp"
#include "host/format/cpv.hpp"
#include "host/format/dif.hpp"
#include "host/format/palette_list.hpp"

namespace hp2::host {

namespace {

constexpr auto kExecutableName = std::string_view{"hp.prg"};
constexpr auto kDataDirectory = std::string_view{"DISK2_2"};
constexpr auto kLogoPictureName = std::string_view{"LOGO.CPV"};
constexpr auto kTitlePictureName = std::string_view{"PRESENT.CPV"};
constexpr auto kTitleAnimationName = std::string_view{"PRESENT.DIF"};
constexpr auto kNamesName = std::string_view{"NAME.IMG"};
// titlePalette (1:28e0): ST colours, sky gradient rows 0-35, PRESENT.CPV's palette from
// row 36.
constexpr auto kTitlePalette = HunkOffset{.hunk = 1, .offset = 0x28e0};
// presentPlayList (1:2870): PRESENT.DIF's play list.
constexpr auto kTitlePlayList = HunkOffset{.hunk = 1, .offset = 0x2870};
constexpr auto kHunkCount = std::size_t{2};

bool SameName(std::string_view first, std::string_view second) {
  return std::ranges::equal(first, second, [](char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
  });
}

// The data file `name` from the game directory's DISK2_2/, read whole.
std::optional<std::vector<std::uint8_t>> ReadDataFile(const std::filesystem::path& game_dir, std::string_view name) {
  auto bytes = std::optional<std::vector<std::uint8_t>>{};
  const auto directory = FindFile(game_dir, kDataDirectory);
  const auto path = directory ? FindFile(*directory, name) : std::nullopt;
  if (path) {
    bytes = ReadFile(*path);
  }
  if (not bytes) {
    spdlog::error("Cannot read {}/{} in {}", kDataDirectory, name, game_dir.string());
  }
  return bytes;
}

// The data file `name` through `decode`; empty, with the reason logged, when it cannot be read or
// does not decode.
template <typename Decode>
auto DecodeDataFile(const std::filesystem::path& game_dir, std::string_view name, Decode decode) {
  using Decoded = std::invoke_result_t<Decode, std::span<const std::uint8_t>>::value_type;
  auto result = std::optional<Decoded>{};
  const auto file = ReadDataFile(game_dir, name);
  if (file) {
    auto decoded = decode(std::span<const std::uint8_t>{*file});
    if (decoded) {
      result = std::move(*decoded);
    } else {
      spdlog::error("Cannot decode {} (error {})", name, static_cast<int>(decoded.error()));
    }
  }
  return result;
}

}  // namespace

std::optional<std::filesystem::path> FindFile(const std::filesystem::path& directory, std::string_view name) {
  auto found = std::optional<std::filesystem::path>{};
  auto error = std::error_code{};
  for (const auto& entry : std::filesystem::directory_iterator{directory, error}) {
    if (SameName(entry.path().filename().string(), name)) {
      found = entry.path();
      break;
    }
  }
  return found;
}

std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path& path) {
  auto bytes = std::optional<std::vector<std::uint8_t>>{};
  auto file = std::ifstream{path, std::ios::binary};
  if (file) {
    bytes = std::vector<std::uint8_t>{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  }
  return bytes;
}

bool AssetManager::Load(const std::filesystem::path& game_dir) {
  const auto loaded = LoadExecutable(game_dir) and LoadLogo(game_dir) and LoadTitle(game_dir) and LoadNames(game_dir);
  if (loaded) {
    BuildViews();
  }
  return loaded;
}

bool AssetManager::LoadExecutable(const std::filesystem::path& game_dir) {
  executable_.reset();
  const auto path = FindFile(game_dir, kExecutableName);
  executable_bytes_ = path ? ReadFile(*path).value_or(std::vector<std::uint8_t>{}) : std::vector<std::uint8_t>{};
  auto hunks = HunkFile::FromBytes(std::as_bytes(std::span{executable_bytes_}));
  if (not hunks) {
    spdlog::error("Cannot load {} from {} (error {})", kExecutableName, game_dir.string(),
                  static_cast<int>(hunks.error()));
  } else if (hunks->hunks.size() != kHunkCount) {
    spdlog::error("{} has {} hunks, expected {}", kExecutableName, hunks->hunks.size(), kHunkCount);
  } else {
    executable_ = std::move(*hunks);
  }
  return executable_.has_value();
}

bool AssetManager::LoadLogo(const std::filesystem::path& game_dir) {
  const auto logo = DecodeDataFile(game_dir, kLogoPictureName, DecodeCpv);
  if (logo) {
    logo_picture_ = logo->pixels;
    auto segment =
        PaletteSegment{.count = static_cast<std::uint8_t>(logo->palette_st.size()), .format = ColorFormat::kAtariSt};
    std::ranges::copy(logo->palette_st, segment.colors.begin());
    logo_palette_ = {segment};
  }
  return logo.has_value();
}

bool AssetManager::LoadTitle(const std::filesystem::path& game_dir) {
  auto loaded = false;
  const auto title = DecodeDataFile(game_dir, kTitlePictureName, DecodeCpv);
  auto frames = DecodeDataFile(game_dir, kTitleAnimationName, DecodeDif);
  auto palette = ReadPaletteList(executable_->hunks[kTitlePalette.hunk], kTitlePalette.offset, ColorFormat::kAtariSt);
  auto steps =
      ReadPlayList(executable_->hunks[kTitlePlayList.hunk], kTitlePlayList.offset, frames ? frames->size() : 0);
  if (not palette) {
    spdlog::error("Cannot read the title palette at hunk {} offset {:04x} (error {})", kTitlePalette.hunk,
                  kTitlePalette.offset, static_cast<int>(palette.error()));
  } else if (not steps) {
    spdlog::error("Cannot read the title play list at hunk {} offset {:04x} (error {})", kTitlePlayList.hunk,
                  kTitlePlayList.offset, static_cast<int>(steps.error()));
  } else if (title and frames) {
    title_picture_ = title->pixels;
    title_palette_ = std::move(*palette);
    title_frames_ = std::move(*frames);
    title_steps_ = std::move(*steps);
    loaded = true;
  }
  return loaded;
}

bool AssetManager::LoadNames(const std::filesystem::path& game_dir) {
  auto names = DecodeDataFile(game_dir, kNamesName, DecodeBobBank);
  if (names) {
    names_ = std::move(*names);
  }
  return names.has_value();
}

void AssetManager::BuildViews() {
  title_run_views_.clear();
  title_frame_views_.clear();
  for (const auto& frame : title_frames_) {
    auto& runs = title_run_views_.emplace_back();
    for (const auto& run : frame.runs) {
      runs.push_back(XorRun{.offset = run.offset, .masks = run.masks});
    }
  }
  for (auto index = std::size_t{0}; index < title_frames_.size(); ++index) {
    title_frame_views_.push_back(XorFrame{.runs = title_run_views_[index], .source_words = title_frames_[index].words});
  }
  name_views_.clear();
  for (const auto& image : names_) {
    name_views_.push_back(ImageView{.width = image.width, .height = image.height, .pixels = image.pixels});
  }
  engine_ = EngineAssets{
      .logo_picture = ImageView{.width = Screen::kWidth, .height = Screen::kHeight, .pixels = logo_picture_},
      .logo_palette = logo_palette_,
      .title_picture = ImageView{.width = Screen::kWidth, .height = Screen::kHeight, .pixels = title_picture_},
      .title_palette = title_palette_,
      .title_animation = XorAnimation{.frames = title_frame_views_, .steps = title_steps_},
      .name_images = name_views_};
}

}  // namespace hp2::host
