#include "host/sfml_input.hpp"

#include <cstddef>
#include <utility>

#include <SFML/Window/Event.hpp>

#include "host/sfml_keys.hpp"

namespace hp2::host {

bool SfmlInput::Handle(const sf::Event& event) {
  auto quit = false;
  if (event.is<sf::Event::Closed>()) {
    quit = true;
  } else if (const auto* pressed = event.getIf<sf::Event::KeyPressed>()) {
    Record(kSfmlKeys.FromNative(std::to_underlying(pressed->scancode)), KeyAction::kPress);
  } else if (const auto* released = event.getIf<sf::Event::KeyReleased>()) {
    Record(kSfmlKeys.FromNative(std::to_underlying(released->scancode)), KeyAction::kRelease);
  } else if (event.is<sf::Event::FocusLost>()) {
    for (auto index = std::size_t{1}; index < down_.size(); ++index) {
      if (down_[index]) {
        Record(static_cast<Key>(index), KeyAction::kRelease);
      }
    }
  }
  return quit;
}

void SfmlInput::Record(Key key, KeyAction action) {
  const auto index = std::size_t{std::to_underlying(key)};
  const auto down = action == KeyAction::kPress;
  // A press of a key already down, or a release of one already up, is not a keystroke.
  if (key != Key::kNone and index < down_.size() and down_[index] != down) {
    down_[index] = down;
    events_.Record(KeyEvent{.key = key, .action = action});
  }
}

}  // namespace hp2::host
