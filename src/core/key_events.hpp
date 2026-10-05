#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/key.hpp"

namespace hp2 {

enum class KeyAction : std::uint8_t { kPress, kRelease };

// One keystroke edge as the platform saw it. The engine keeps no key levels: whatever needs to know
// that a key is down follows the presses and releases it consumes.
struct KeyEvent {
  Key key{Key::kNone};
  KeyAction action{KeyAction::kPress};

  constexpr bool operator==(const KeyEvent&) const = default;
};

// Keystrokes recorded by the platform backend, in arrival order, until the engine consumes them.
// Bounded: when kCapacity events are pending, new ones are dropped and counted, so a stalled
// consumer loses the newest keystrokes rather than reordering the queue. Single producer and single
// consumer on one thread; a target feeding it from an interrupt or the other core needs a lock-free
// variant with the same interface.
class KeyEvents {
 public:
  static constexpr std::size_t kCapacity = 64;

  // Records a keystroke; returns false, and counts the loss, when the queue is full.
  bool Record(KeyEvent event) {
    const bool accepted = count_ < kCapacity;
    if (accepted) {
      events_[(head_ + count_) % kCapacity] = event;
      ++count_;
    } else {
      ++dropped_;
    }
    return accepted;
  }

  // The oldest pending keystroke, removed from the queue; empty when none is pending.
  [[nodiscard]] std::optional<KeyEvent> Next() {
    std::optional<KeyEvent> event;
    if (count_ > 0) {
      event = events_[head_];
      head_ = (head_ + 1) % kCapacity;
      --count_;
    }
    return event;
  }

  // Forgets every pending keystroke, as when a component hands over and its keys must not leak
  // into the next one.
  void Clear() {
    head_ = 0;
    count_ = 0;
  }

  [[nodiscard]] std::size_t Pending() const { return count_; }
  [[nodiscard]] std::size_t Dropped() const { return dropped_; }

 private:
  std::array<KeyEvent, kCapacity> events_{};
  std::size_t head_{};
  std::size_t count_{};
  std::size_t dropped_{};
};

}  // namespace hp2
