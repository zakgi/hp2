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
  kLogo,        // LOGO.CPV, the publisher's logo
  kTitle,       // PRESENT.CPV
  kOffice,      // bureau.cpv, where the missions are chosen
  kStation,     // STATION.CPV, the gas station
  kSpinOut,     // PAGE_F1.CPV: spun out after leaving the road, the game goes on
  kTiresGone,   // PAGE_F2.CPV
  kOutOfFuel,   // PAGE_F3.CPV
  kOverheated,  // PAGE_F4.CPV
  kWrecked,     // PAGE_F5.CPV
  kArrest,      // PAGE_F6.CPV
  kCount,
};

// Palette lists: a picture's own (the header of its .CPV, one segment of Atari ST colors) or one
// of the executable's (docs/formats.md, "Palettes in the executable").
enum class EnginePalette : std::uint8_t {
  kLogo,
  kStation,
  kSpinOut,
  kTiresGone,
  kOutOfFuel,
  kOverheated,
  kWrecked,
  kArrest,
  kTitle,      // 1:28e0: sky gradient rows 0-35, PRESENT.CPV's colors from row 36
  kOffice,     // 0:ca94
  kOfficeDim,  // 0:ca6e
  kScore,      // 0:7766: end and score screens
  kView,       // 0:a520: the driving screen
  kViewRed,    // 0:a734: the driving screen's red flash
  kCount,
};

// The sprite banks (.IMG files), named by meaning; the file names are French.
enum class EngineBank : std::uint8_t {
  kNames,              // NAME.IMG, the title's lettering
  kOffice,             // BUREAU.IMG: wanted posters, drawer fronts, the pointer
  kStation,            // STATION.IMG: menu items and attendants
  kScore,              // GAME_SCO.IMG: GAME OVER, SCORE:, the digits
  kGun,                // BALLE.IMG: gun, bullet, muzzle flash
  kCactus,             // CACTUS.IMG
  kBush,               // BUISSON.IMG
  kStones,             // CAILLOUX.IMG
  kBackdrop,           // DEC_FOND.IMG: horizon strips
  kCockpit,            // DES_TABB.IMG: steering wheel, hands, dashboard, roof
  kSignPole,           // PAN_POT.IMG
  kSignLeft,           // PAN_GAU.IMG
  kSignJunction,       // PAN_CRO.IMG
  kSignRight,          // PAN_DRO.IMG
  kSignBack,           // PAN_ARR.IMG
  kSignEdge,           // PAN_PRO.IMG
  kStationArrowLeft,   // PST_FLG.IMG
  kStationArrowRight,  // PST_FLD.IMG
  kStationEdge,        // PST_PRO.IMG
  kStationBoard,       // PST_STA.IMG
  kCar0,               // VOITURE0.IMG .. VOITURE6.IMG: the cars at 10 sizes
  kCar1,
  kCar2,
  kCar3,
  kCar4,
  kCar5,
  kCar6,
  kCount,
};

// The two fonts differ in colors only.
enum class EngineFont : std::uint8_t {
  kLettre1,  // LETTRE1.BIN
  kLettre2,  // LETTRE2.BIN
  kCount,
};

enum class EngineSound : std::uint8_t {
  kEngine,  // MOTEUR.SND
  kSiren,   // SIRENE.SND
  kShot,    // TIR.SND
  kSkid,    // DERAP.SND
  kCrash,   // CHOC.SND
  kCount,
};

inline constexpr std::size_t kEnginePictureCount = std::to_underlying(EnginePicture::kCount);
inline constexpr std::size_t kEnginePaletteCount = std::to_underlying(EnginePalette::kCount);
inline constexpr std::size_t kEngineBankCount = std::to_underlying(EngineBank::kCount);
inline constexpr std::size_t kEngineFontCount = std::to_underlying(EngineFont::kCount);
inline constexpr std::size_t kEngineSoundCount = std::to_underlying(EngineSound::kCount);

// Everything the game disk supplies, decoded: views only, no file names or decoders. Filled once by
// the host asset manager, or bound to the flash image by the generated asset_layout::FlashAssets()
// on the target; components receive it by reference and pick what they need.
struct EngineAssets {
  std::array<ImageView, kEnginePictureCount> pictures;
  // Each a palette list: segments in row order, the first at row 0.
  std::array<std::span<const PaletteSegment>, kEnginePaletteCount> palettes;
  std::array<SpriteBank, kEngineBankCount> banks;
  std::array<BitmapFont, kEngineFontCount> fonts;
  std::array<SoundSample, kEngineSoundCount> sounds;
  // PRESENT.DIF over the title picture, with the play list at 1:2870.
  XorAnimation title_animation;
  // HIGHWAY.MUS.
  MusicModule title_music;
  // CARTE.BIN.
  RoadMapView road_map;
  // COOR_OBJ.BIN.
  Scenery scenery;
  // roadCellShapes in hp.prg (0:7812).
  RoadShapes road_shapes;

  [[nodiscard]] constexpr const ImageView& Picture(EnginePicture picture) const {
    return pictures[std::to_underlying(picture)];
  }
  [[nodiscard]] constexpr std::span<const PaletteSegment> Palette(EnginePalette palette) const {
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
