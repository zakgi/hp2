#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/component.hpp"
#include "core/screen.hpp"

namespace hp2 {

enum class OfficePhase : std::uint8_t { LightsOn, Pointing, Closing, Opening, LightsOff };

// The office, before every mission (OfficeSelectMission, 0:c3aa; docs/game.md, "Program flow"): a
// pointer over the desk. Clicking a drawer slides one of its two wanted posters up, clicking the
// same drawer again the other one; clicking the open poster takes its mission, which then cannot
// be taken again. The arrow keys move the pointer, Space or Enter click; Escape quits.
class Office {
 public:
  static constexpr ComponentType kType = ComponentType::Office;

  // A rectangle of screen pixels, edges included.
  struct Area {
    std::int16_t left;
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;

    [[nodiscard]] constexpr bool Contains(Point point) const {
      return point.x >= left and point.x <= right and point.y >= top and point.y <= bottom;
    }
  };

  // Where a poster slides: its column, its row open and closed, and the drawer's bottom, below
  // which nothing of it shows (posterSlideTable, 0:cd30).
  struct PosterSlide {
    std::int16_t x;
    std::int16_t open_y;
    std::int16_t closed_y;
    std::int16_t clip_bottom;
  };

  static constexpr std::size_t kDrawerCount = 3;
  // Clicks count only over the desk.
  static constexpr Area kDesk{.left = 20, .top = 0, .right = 130, .bottom = 140};
  // A drawer is found by the color of the picture under the pointer.
  static constexpr auto kDrawerColors = std::to_array<std::uint8_t>({11, 12, 4});
  static constexpr auto kPosterSlides = std::to_array<PosterSlide>({
      {.x = 48, .open_y = 48, .closed_y = 138, .clip_bottom = 138},
      {.x = 48, .open_y = 48, .closed_y = 138, .clip_bottom = 138},
      {.x = 32, .open_y = 41, .closed_y = 134, .clip_bottom = 134},
      {.x = 32, .open_y = 41, .closed_y = 134, .clip_bottom = 134},
      {.x = 32, .open_y = 32, .closed_y = 128, .clip_bottom = 128},
      {.x = 32, .open_y = 32, .closed_y = 128, .clip_bottom = 128},
  });
  // The drawer fronts drawn over a sliding poster (0:cd60), and the part of an open poster a click
  // takes (0:cd6c).
  static constexpr auto kDrawerFronts =
      std::to_array<Point>({{.x = 48, .y = 110}, {.x = 32, .y = 106}, {.x = 32, .y = 100}});
  static constexpr auto kPosterAreas = std::to_array<Area>({{.left = 61, .top = 60, .right = 91, .bottom = 94},
                                                            {.left = 50, .top = 53, .right = 80, .bottom = 87},
                                                            {.left = 35, .top = 44, .right = 65, .bottom = 78}});
  // BUREAU.IMG: posters 0-5, drawer fronts 6-8, the pointer 9.
  static constexpr std::size_t kFirstDrawerFront = 6;
  static constexpr std::size_t kPointerImage = 9;
  static constexpr Point kPointerStart{.x = 160, .y = 100};
  static constexpr Area kPointerRange{.left = 7, .top = 7, .right = 312, .bottom = 192};

  // The pointer moves 2 pixels a frame in the original; posters slide 4.
  static constexpr float kPointerSpeed = 120.0F;  // pixels per second
  static constexpr float kSlideSpeed = 240.0F;    // pixels per second
  // The lights come on after a moment, and go down before the mission starts.
  static constexpr float kLightsSeconds = 0.5F;

  void OnEnter(EngineState& state);
  void OnExit(EngineState& state);
  ComponentType Step(EngineState& state, float delta_seconds);

  // For tests: the poster open or sliding, the pointer.
  [[nodiscard]] static std::optional<std::size_t> Poster(const EngineState& state);
  [[nodiscard]] static Point Pointer(const EngineState& state);

 private:
  static void ReadKeys(EngineState& state, bool& clicked, bool& quit);
  static void MovePointer(EngineState& state, float delta_seconds);
  static void Click(EngineState& state);
  // The poster a click on `drawer` brings out: the drawer's first one, or its other one when the
  // drawer's poster is open; a poster already taken gives way to its pair.
  [[nodiscard]] static std::optional<std::size_t> PosterFor(const EngineState& state, std::size_t drawer);
  static void Slide(EngineState& state, float delta_seconds);
  // The desk with the poster and its drawer front, without the pointer.
  static void ComposeDesk(EngineState& state);
  static void Compose(EngineState& state);
};

// Persistent scene data, owned by EngineState.
struct OfficeState {
  OfficePhase phase{OfficePhase::LightsOn};
  float phase_seconds{};
  float pointer_x{};
  float pointer_y{};
  // Arrow keys held: left, right, up, down.
  std::array<bool, 4> held{};
  std::optional<std::size_t> poster;
  std::optional<std::size_t> next_poster;
  float poster_y{};
};

static_assert(ComponentLike<Office>);

}  // namespace hp2
