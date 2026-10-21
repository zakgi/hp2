#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "core/bitmap_font.hpp"
#include "core/image_view.hpp"
#include "core/music_module.hpp"
#include "core/palette.hpp"
#include "core/road_map.hpp"
#include "core/sound_sample.hpp"
#include "core/sprite_bank.hpp"
#include "core/xor_animation.hpp"

namespace hp2 {

// The full-screen pictures (.CPV files), 320 x 200 each, named by meaning.
enum class EnginePicture : std::uint8_t {
  Logo,        // LOGO.CPV, the publisher's logo
  Title,       // PRESENT.CPV
  Office,      // bureau.cpv, where the missions are chosen
  Station,     // STATION.CPV, the gas station
  SpinOut,     // PAGE_F1.CPV: spun out after leaving the road, the game goes on
  TiresGone,   // PAGE_F2.CPV
  OutOfFuel,   // PAGE_F3.CPV
  Overheated,  // PAGE_F4.CPV
  Wrecked,     // PAGE_F5.CPV
  Arrest,      // PAGE_F6.CPV
  Count,
};

// Palettes: a picture's own (the header of its .CPV) or one of the executable's lists
// (docs/formats.md, "Palettes in the executable").
enum class EnginePalette : std::uint8_t {
  Logo,
  Station,
  SpinOut,
  TiresGone,
  OutOfFuel,
  Overheated,
  Wrecked,
  Arrest,
  Title,      // 1:28e0: the sky's colors, then PRESENT.CPV's
  Office,     // 0:ca94
  OfficeDim,  // 0:ca6e
  Score,      // 0:7766: end and score screens
  View,       // 0:a520: the driving screen
  ViewRed,    // 0:a734: the driving screen's red flash
  Count,
};

// The sprite banks (.IMG files), named by meaning; the file names are French.
enum class EngineBank : std::uint8_t {
  Names,              // NAME.IMG, the title's lettering
  Office,             // BUREAU.IMG: wanted posters, drawer fronts, the pointer
  Station,            // STATION.IMG: menu items and attendants
  Score,              // GAME_SCO.IMG: GAME OVER, SCORE:, the digits
  Gun,                // BALLE.IMG: gun, bullet, muzzle flash
  Cactus,             // CACTUS.IMG
  Bush,               // BUISSON.IMG
  Stones,             // CAILLOUX.IMG
  Backdrop,           // DEC_FOND.IMG: horizon strips
  Cockpit,            // DES_TABB.IMG: steering wheel, hands, dashboard, roof
  SignPole,           // PAN_POT.IMG
  SignLeft,           // PAN_GAU.IMG
  SignJunction,       // PAN_CRO.IMG
  SignRight,          // PAN_DRO.IMG
  SignBack,           // PAN_ARR.IMG
  SignEdge,           // PAN_PRO.IMG
  StationArrowLeft,   // PST_FLG.IMG
  StationArrowRight,  // PST_FLD.IMG
  StationEdge,        // PST_PRO.IMG
  StationBoard,       // PST_STA.IMG
  Car0,               // VOITURE0.IMG .. VOITURE6.IMG: the cars at 10 sizes
  Car1,
  Car2,
  Car3,
  Car4,
  Car5,
  Car6,
  Count,
};

// The two fonts differ in colors only.
enum class EngineFont : std::uint8_t {
  Lettre1,  // LETTRE1.BIN
  Lettre2,  // LETTRE2.BIN
  Count,
};

enum class EngineSound : std::uint8_t {
  Engine,  // MOTEUR.SND
  Siren,   // SIRENE.SND
  Shot,    // TIR.SND
  Skid,    // DERAP.SND
  Crash,   // CHOC.SND
  Count,
};

inline constexpr std::size_t kEnginePictureCount = std::to_underlying(EnginePicture::Count);
inline constexpr std::size_t kEnginePaletteCount = std::to_underlying(EnginePalette::Count);
inline constexpr std::size_t kEngineBankCount = std::to_underlying(EngineBank::Count);
inline constexpr std::size_t kEngineFontCount = std::to_underlying(EngineFont::Count);
inline constexpr std::size_t kEngineSoundCount = std::to_underlying(EngineSound::Count);

// Everything the game disk supplies, decoded: views only, no file names or decoders. Filled once by
// the host asset manager, or bound to the flash image by the generated asset_layout::FlashAssets()
// on the target; components receive it by reference and pick what they need.
struct EngineAssets {
  std::array<ImageView, kEnginePictureCount> pictures{};
  // Each palette as sets of kColorRegisterCount colors, one after the other: a list's segments in
  // order, each set the colors in effect from its segment on.
  std::array<std::span<const Rgb>, kEnginePaletteCount> palettes{};
  std::array<SpriteBank, kEngineBankCount> banks{};
  std::array<BitmapFont, kEngineFontCount> fonts{};
  std::array<SoundSample, kEngineSoundCount> sounds{};
  // PRESENT.DIF over the title picture, with the play list at 1:2870.
  XorAnimation title_animation{};
  // HIGHWAY.MUS.
  MusicModule title_music{};
  // CARTE.BIN.
  RoadMapView road_map{};
  // COOR_OBJ.BIN.
  Scenery scenery{};
  // roadCellShapes in hp.prg (0:7812).
  RoadShapes road_shapes{};

  [[nodiscard]] constexpr const ImageView& Picture(EnginePicture picture) const {
    return pictures[std::to_underlying(picture)];
  }
  [[nodiscard]] constexpr std::span<const Rgb> Palette(EnginePalette palette) const {
    return palettes[std::to_underlying(palette)];
  }
  [[nodiscard]] constexpr const SpriteBank& Bank(EngineBank bank) const { return banks[std::to_underlying(bank)]; }
  [[nodiscard]] constexpr const BitmapFont& Font(EngineFont font) const { return fonts[std::to_underlying(font)]; }
  [[nodiscard]] constexpr const SoundSample& Sound(EngineSound sound) const {
    return sounds[std::to_underlying(sound)];
  }
};

static_assert(std::is_trivially_copyable_v<EngineAssets>);

}  // namespace hp2
