#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

namespace hp2 {

// An action is one step of a presentation: it is initialised once when it first becomes the top of
// the stack, then ticked with the elapsed time in seconds until it reports completion. Deltas are
// fractions of a second; anything long-lived counts fixed steps instead of summing them.
template <typename T>
concept ActionLike = requires(T& action, float delta_seconds) {
  { action.Init() } -> std::same_as<void>;
  { action.Tick(delta_seconds) } -> std::same_as<bool>;
};

// Fixed-capacity stack of actions, ticked from the top. A sequence is pushed in reverse order at
// entry; pushing beyond Capacity is refused and reported to the caller. Clear() abandons every
// pending action, which is how a key press cuts a presentation short. Storage is fixed; each slot is
// initialised once.
template <std::size_t Capacity, ActionLike... Actions>
class ActionStack {
 public:
  using Variant = std::variant<Actions...>;

  template <typename T, typename... Args>
    requires(std::same_as<T, Actions> or ...)
  [[nodiscard]] bool Push(Args&&... args) {
    const bool has_room = size_ < Capacity;
    if (has_room) {
      // Constructed in place: actions may hold references and need not be assignable.
      slots_[size_].emplace(Variant{std::in_place_type<T>, std::forward<Args>(args)...});
      ++size_;
    }
    return has_room;
  }

  // Initialises the top action if needed, then ticks it; a completed action is popped.
  void Tick(float delta_seconds) {
    if (size_ > 0) {
      Slot& top = *slots_[size_ - 1];
      if (not top.initialized) {
        std::visit([](auto& action) { action.Init(); }, top.action);
        top.initialized = true;
      }
      const bool done = std::visit([delta_seconds](auto& action) { return action.Tick(delta_seconds); }, top.action);
      if (done) {
        Pop();
      }
    }
  }

  void Pop() {
    if (size_ > 0) {
      --size_;
      slots_[size_].reset();
    }
  }

  void Clear() {
    while (size_ > 0) {
      Pop();
    }
  }

  [[nodiscard]] bool Empty() const { return size_ == 0; }
  [[nodiscard]] std::size_t Size() const { return size_; }
  [[nodiscard]] static constexpr std::size_t MaxSize() { return Capacity; }

 private:
  struct Slot {
    explicit Slot(Variant&& variant) : action(std::move(variant)) {}
    Variant action;
    bool initialized{false};
  };

  std::array<std::optional<Slot>, Capacity> slots_{};
  std::size_t size_{};
};

}  // namespace hp2
