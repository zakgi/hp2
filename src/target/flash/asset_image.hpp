#pragma once

// The asset image scripts/pack_assets.py writes into the "assets" partition: the engine's own
// record types laid out as they are in memory, at the section offsets of the generated
// asset_layout.hpp. The static_asserts below are the contract with the packer's struct formats; a
// layout change on either side fails the build here.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/music_module.hpp"
#include "core/palette.hpp"
#include "core/road_map.hpp"
#include "core/sprite_bank.hpp"
#include "core/xor_animation.hpp"

namespace hp2::flash {

static_assert(sizeof(ColorFormat) == 1);
static_assert(sizeof(PaletteSegment) == 38 and offsetof(PaletteSegment, first_register) == 2 and
              offsetof(PaletteSegment, count) == 3 and offsetof(PaletteSegment, format) == 4 and
              offsetof(PaletteSegment, colors) == 6);
static_assert(sizeof(SpriteRange) == 12 and offsetof(SpriteRange, width) == 4 and offsetof(SpriteRange, height) == 6 and
              offsetof(SpriteRange, origin_x) == 8 and offsetof(SpriteRange, origin_y) == 10);
static_assert(sizeof(XorRun) == 12 and offsetof(XorRun, mask_first) == 4 and offsetof(XorRun, mask_count) == 8);
static_assert(sizeof(XorFrame) == 12 and offsetof(XorFrame, run_count) == 4 and offsetof(XorFrame, source_words) == 8);
static_assert(sizeof(AnimationStep) == 4 and offsetof(AnimationStep, delay) == 2);
static_assert(sizeof(ModuleSample) == 24 and offsetof(ModuleSample, size) == 4 and
              offsetof(ModuleSample, length) == 8 and offsetof(ModuleSample, loop_start) == 12 and
              offsetof(ModuleSample, loop_length) == 16 and offsetof(ModuleSample, volume) == 20);
static_assert(sizeof(ModuleNote) == 6 and offsetof(ModuleNote, sample) == 2 and offsetof(ModuleNote, effect) == 3 and
              offsetof(ModuleNote, parameter) == 4);
static_assert(sizeof(PlacedObject) == 10 and offsetof(PlacedObject, y) == 2 and offsetof(PlacedObject, z) == 4 and
              offsetof(PlacedObject, type) == 6 and offsetof(PlacedObject, extra) == 8);

// `count` records of T at `address`, which must be aligned for T. Runtime only: forming a pointer
// from an integer is not a constant expression.
template <typename T>
[[nodiscard]] std::span<const T> TableView(std::uintptr_t address, std::size_t count) {
  return std::span<const T>{std::bit_cast<const T*>(address), count};
}

}  // namespace hp2::flash
