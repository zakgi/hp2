#pragma once

#include <span>

#include "core/image_view.hpp"
#include "core/palette.hpp"
#include "core/xor_animation.hpp"

namespace hp2 {

// The decoded game data the components draw from: views only, no file names or decoders. The
// host asset manager (or, on the target, the flash image) owns the storage.
struct EngineAssets {
  // LOGO.CPV, the publisher's logo: 320x200 pixels, and its header palette as a one-segment list.
  ImageView logo_picture;
  PaletteProgram logo_palette;
  // PRESENT.CPV, the title picture: 320x200 pixels.
  ImageView title_picture;
  // The title palette (1:28e0): sky gradient rows 0-35, PRESENT.CPV's colours from row 36.
  PaletteProgram title_palette;
  // PRESENT.DIF played over the title picture with the play list at 1:2870.
  XorAnimation title_animation;
  // NAME.IMG, the title's lettering: three masked images.
  std::span<const ImageView> name_images;
};

}  // namespace hp2
