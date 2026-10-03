#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>

#include "core/screen.hpp"

namespace hp2::host {

// Shows a Screen in the window: every viewport's pixels through that viewport's palette, scaled to
// the window at a 4:3 aspect (320x200 filling an NTSC display). Colours are resolved here and
// nowhere else; the core never sees RGBA.
class Renderer {
 public:
  explicit Renderer(sf::RenderWindow& window);

  // Draws the screen and displays the window. Clears the palettes' dirty flags.
  void Render(Screen& screen);

 private:
  static constexpr auto kChannels = std::size_t{4};

  sf::RenderWindow& window_;
  sf::Texture texture_;
  std::array<std::uint8_t, std::size_t{Screen::kWidth} * Screen::kHeight * kChannels> rgba_{};
};

}  // namespace hp2::host
