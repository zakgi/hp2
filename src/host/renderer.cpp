#include "host/renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Vector2.hpp>

namespace hp2::host {

namespace {

constexpr auto kDisplayAspect = 4.0F / 3.0F;
constexpr auto kOpaque = std::uint8_t{0xff};

}  // namespace

Renderer::Renderer(sf::RenderWindow& window)
    : window_(window), texture_(sf::Vector2u{Screen::kWidth, Screen::kHeight}) {}

void Renderer::Render(Screen& screen) {
  for (const auto viewport : {Viewport::kUpper, Viewport::kLower}) {
    auto& palette = screen.Palette(viewport);
    const auto colors = palette.Colors();
    const auto first = screen.FirstRow(viewport);
    for (auto row = first; row < first + screen.Rows(viewport); ++row) {
      const auto pixels = screen.ScreenRow(row);
      const auto out =
          std::span{rgba_}.subspan(std::size_t{row} * Screen::kWidth * kChannels, Screen::kWidth * kChannels);
      for (auto column = std::size_t{0}; column < pixels.size(); ++column) {
        const auto color = colors[pixels[column]];
        out[(column * kChannels) + 0] = color.red;
        out[(column * kChannels) + 1] = color.green;
        out[(column * kChannels) + 2] = color.blue;
        out[(column * kChannels) + 3] = kOpaque;
      }
    }
  }
  texture_.update(rgba_.data());

  const auto size = window_.getSize();
  const auto window_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);
  auto viewport = sf::FloatRect{{0.0F, 0.0F}, {1.0F, 1.0F}};
  if (window_aspect > kDisplayAspect) {
    viewport.size.x = kDisplayAspect / window_aspect;
    viewport.position.x = (1.0F - viewport.size.x) / 2.0F;
  } else {
    viewport.size.y = window_aspect / kDisplayAspect;
    viewport.position.y = (1.0F - viewport.size.y) / 2.0F;
  }
  auto view = sf::View{sf::FloatRect{{0.0F, 0.0F}, {float{Screen::kWidth}, float{Screen::kHeight}}}};
  view.setViewport(viewport);
  window_.setView(view);

  window_.clear(sf::Color::Black);
  window_.draw(sf::Sprite{texture_});
  window_.display();
}

}  // namespace hp2::host
