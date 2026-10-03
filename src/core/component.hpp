#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

namespace hp2 {

// Top-level application states, after the original's program flow (docs/game.md): title, office
// (mission choice), the highway, station stop, mission end. kQuit ends the engine loop.
enum class ComponentType : std::uint8_t {
  kTitle,
  kOffice,
  kHighway,
  kStation,
  kMissionEnd,
  kQuit,
};

// A component owns one application state. It is constructed with references to what it uses and
// returns its own kType from Step() to stay active, or another type to hand over.
template <typename T>
concept ComponentLike = requires(T& component, float delta_seconds) {
  { T::kType } -> std::convertible_to<ComponentType>;
  { component.OnEnter() } -> std::same_as<void>;
  { component.OnExit() } -> std::same_as<void>;
  { component.Step(delta_seconds) } -> std::same_as<ComponentType>;
};

template <std::size_t Count>
constexpr bool AllUnique(const std::array<ComponentType, Count>& types) {
  bool unique = true;
  for (std::size_t first = 0; first < Count; ++first) {
    for (std::size_t second = first + 1; second < Count; ++second) {
      unique = unique and types[first] != types[second];
    }
  }
  return unique;
}

// Runs one component at a time and switches on the type its Step() returns. Components are moved in
// already bound to their dependencies. A transition to a type that is not registered is refused and
// reported by Step().
template <ComponentLike... Components>
class Engine {
 public:
  static constexpr std::size_t kComponentCount = sizeof...(Components);
  static constexpr std::array<ComponentType, kComponentCount> kTypes = {Components::kType...};
  static_assert(kComponentCount > 0, "an engine needs at least one component");
  static_assert(AllUnique(kTypes), "each ComponentType may be registered once");
  static_assert(((Components::kType != ComponentType::kQuit) and ...), "kQuit is a transition, not a component");

  explicit Engine(Components&&... components) : components_(std::move(components)...) {}

  [[nodiscard]] static constexpr bool IsRegistered(ComponentType type) {
    bool registered = false;
    for (const ComponentType candidate : kTypes) {
      registered = registered or candidate == type;
    }
    return registered;
  }

  // Enters `type` as the first active component. Returns false, and stays idle, if unregistered.
  [[nodiscard]] bool Start(ComponentType type) {
    const bool registered = IsRegistered(type);
    if (registered) {
      active_ = type;
      Visit(active_, [](auto& component) { component.OnEnter(); });
    }
    return registered;
  }

  // Steps the active component and performs any transition it requests. Returns false when the
  // engine is not running or the requested transition is unregistered; the component then stays.
  [[nodiscard]] bool Step(float delta_seconds) {
    bool accepted = Running();
    if (accepted) {
      const ComponentType next =
          Visit(active_, [delta_seconds](auto& component) { return component.Step(delta_seconds); });
      if (next == ComponentType::kQuit) {
        Visit(active_, [](auto& component) { component.OnExit(); });
        active_ = ComponentType::kQuit;
      } else if (next != active_) {
        accepted = Switch(next);
      }
    }
    return accepted;
  }

  // Leaves the active component for `type`, from outside the components: a platform's own keys,
  // or a stand-in for a transition nothing registered handles. Returns false, and stays, when the
  // engine is not running or `type` is unregistered. Switching to the active type re-enters it.
  [[nodiscard]] bool Switch(ComponentType type) {
    const bool accepted = Running() and IsRegistered(type);
    if (accepted) {
      Visit(active_, [](auto& component) { component.OnExit(); });
      active_ = type;
      Visit(active_, [](auto& component) { component.OnEnter(); });
    }
    return accepted;
  }

  [[nodiscard]] bool Running() const { return active_ != ComponentType::kQuit; }
  [[nodiscard]] ComponentType Active() const { return active_; }

 private:
  template <typename Visitor>
  auto Visit(ComponentType type, Visitor&& visitor) {
    return VisitComponent(type, std::forward<Visitor>(visitor), std::index_sequence_for<Components...>{});
  }

  // Calls `visitor` on the component whose kType is `type`. `type` must be registered.
  template <typename Visitor, std::size_t... Indices>
  auto VisitComponent(ComponentType type, Visitor&& visitor, std::index_sequence<Indices...> /*indices*/) {
    using Result = decltype(visitor(std::get<0>(components_)));
    if constexpr (std::is_void_v<Result>) {
      ((kTypes[Indices] == type ? (visitor(std::get<Indices>(components_)), void()) : void()), ...);
    } else {
      Result result{};
      ((kTypes[Indices] == type ? (result = visitor(std::get<Indices>(components_)), void()) : void()), ...);
      return result;
    }
  }

  std::tuple<Components...> components_;
  ComponentType active_{ComponentType::kQuit};
};

}  // namespace hp2
