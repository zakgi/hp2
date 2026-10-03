#pragma once

#include <array>

#include <SFML/Window/Event.hpp>

#include "core/key.hpp"
#include "core/key_events.hpp"

namespace hp2::host {

// Records SFML keyboard events as keystrokes for the engine. The window's key repeat must be off so
// that every press is a real keystroke. The host's event loop hands every window event here; the
// return value says whether the window asked to close.
class SfmlInput {
 public:
  explicit SfmlInput(KeyEvents& events) : events_(events) {}

  // Returns true when the event asks to quit.
  bool Handle(const sf::Event& event);

 private:
  void Record(Key key, KeyAction action);

  KeyEvents& events_;
  // Keys this backend reported pressed and not yet released: when the window loses focus, the
  // releases it will never receive are recorded so that no keystroke stays open in the engine.
  std::array<bool, kKeyCount> down_{};
};

}  // namespace hp2::host
