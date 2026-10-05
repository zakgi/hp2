#pragma once

// Keys over USB CDC, from a plain serial terminal. A terminal reports no releases, so every byte
// the port has buffered becomes a press of the key it maps to, and the release follows at the next
// poll. Letters and digits are their own keys, Enter and Space too; the ANSI arrow sequences are
// the arrows, and an ESC that starts no sequence is Escape. Byte values with bit 7 set are left for
// a packet protocol later.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "pico/stdio.h"

#include "core/key.hpp"
#include "core/key_events.hpp"

namespace hp2 {

class CdcInput {
 public:
  explicit CdcInput(KeyEvents& events) : events_(events) {}

  // Releases the previous poll's keys, then drains the port. Once per frame, before the engine
  // steps.
  void Poll() {
    for (auto index = std::size_t{0}; index < pressed_count_; ++index) {
      events_.Record(KeyEvent{.key = pressed_[index], .action = KeyAction::kRelease});
    }
    pressed_count_ = 0;
    auto draining = true;
    while (draining) {
      const auto byte = getchar_timeout_us(0);
      if (byte < 0) {
        draining = false;
      } else {
        Consume(static_cast<std::uint8_t>(byte));
      }
    }
    // An ESC with nothing after it in this poll is the Escape key: a terminal sends an arrow's
    // three bytes together.
    if (escape_ == Escape::kEscape) {
      Press(Key::kEscape);
      escape_ = Escape::kNone;
    }
  }

 private:
  static constexpr std::size_t kPressLimit = 8;
  enum class Escape : std::uint8_t { kNone, kEscape, kBracket };

  static constexpr Key GetKey(std::uint8_t byte) {
    auto key = Key::kNone;
    if (byte >= 'a' and byte <= 'z') {
      key = static_cast<Key>(std::to_underlying(Key::kA) + (byte - 'a'));
    } else if (byte >= 'A' and byte <= 'Z') {
      key = static_cast<Key>(std::to_underlying(Key::kA) + (byte - 'A'));
    } else if (byte >= '0' and byte <= '9') {
      key = static_cast<Key>(std::to_underlying(Key::kDigit0) + (byte - '0'));
    } else if (byte == '\r' or byte == '\n') {
      key = Key::kEnter;
    } else if (byte == ' ') {
      key = Key::kSpace;
    }
    return key;
  }

  static constexpr Key GetArrow(std::uint8_t byte) {
    auto key = Key::kNone;
    switch (byte) {
      case 'A':
        key = Key::kUp;
        break;
      case 'B':
        key = Key::kDown;
        break;
      case 'C':
        key = Key::kRight;
        break;
      case 'D':
        key = Key::kLeft;
        break;
      default:
        break;
    }
    return key;
  }

  // ESC [ A..D are the arrows; an ESC before anything else is Escape, and that byte counts too.
  void Consume(std::uint8_t byte) {
    if (escape_ == Escape::kBracket) {
      Press(GetArrow(byte));
      escape_ = Escape::kNone;
    } else if (escape_ == Escape::kEscape and byte == '[') {
      escape_ = Escape::kBracket;
    } else {
      if (escape_ == Escape::kEscape) {
        Press(Key::kEscape);
        escape_ = Escape::kNone;
      }
      if (byte == 0x1b) {
        escape_ = Escape::kEscape;
      } else if (byte < 0x80) {
        Press(GetKey(byte));
      }
    }
  }

  void Press(Key key) {
    if (key != Key::kNone and pressed_count_ < kPressLimit) {
      events_.Record(KeyEvent{.key = key, .action = KeyAction::kPress});
      pressed_[pressed_count_] = key;
      ++pressed_count_;
    }
  }

  KeyEvents& events_;
  std::array<Key, kPressLimit> pressed_{};
  std::size_t pressed_count_{0};
  Escape escape_{Escape::kNone};
};

}  // namespace hp2
