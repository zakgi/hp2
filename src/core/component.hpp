#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace hp2 {

struct EngineState;

// Top-level application states, after the original's program flow (docs/game.md): title, office
// (mission choice), the highway, station stop, mission end. kQuit ends the engine loop.
enum class ComponentType : std::uint8_t {
  Title,
  Office,
  Highway,
  Station,
  MissionEnd,
  Quit,
};

// A component controls one scene. Shared data lives in EngineState; private controller data lives
// only while that controller is active. Step returns its kType to stay, or another type to hand over.
template <typename T>
concept ComponentLike =
    std::default_initializable<T> and requires(T& component, EngineState& state, float delta_seconds) {
      { T::kType } -> std::convertible_to<ComponentType>;
      { component.OnEnter(state) } -> std::same_as<void>;
      { component.OnExit(state) } -> std::same_as<void>;
      { component.Step(state, delta_seconds) } -> std::same_as<ComponentType>;
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

// Constructs one controller at a time in a variant, against shared state that outlives the engine.
// A transition to a type that is not registered is refused and reported by Step().
template <ComponentLike... Components>
class Engine {
 public:
  // Dependent constexpr initializers cannot run dynamically.
  // NOLINTBEGIN(bugprone-dynamic-static-initializers)
  static constexpr std::size_t kComponentCount = sizeof...(Components);
  static constexpr std::array<ComponentType, kComponentCount> kTypes = {Components::kType...};
  // NOLINTEND(bugprone-dynamic-static-initializers)
  static_assert(kComponentCount > 0, "an engine needs at least one component");
  static_assert(AllUnique(kTypes), "each ComponentType may be registered once");
  static_assert(((Components::kType != ComponentType::Quit) and ...), "kQuit is a transition, not a component");

  explicit Engine(EngineState& state) : state_(state) {}

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
      Enter(type);
    }
    return registered;
  }

  // Steps the active component and performs any transition it requests. Returns false when the
  // engine is not running or the requested transition is unregistered; the component then stays.
  [[nodiscard]] bool Step(float delta_seconds) {
    bool accepted = Running();
    if (accepted) {
      const ComponentType next =
          Visit([this, delta_seconds](auto& component) { return component.Step(state_, delta_seconds); });
      if (next == ComponentType::Quit) {
        Visit([this](auto& component) { component.OnExit(state_); });
        active_.template emplace<std::monostate>();
      } else if (next != Active()) {
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
      Visit([this](auto& component) { component.OnExit(state_); });
      Enter(type);
    }
    return accepted;
  }

  [[nodiscard]] bool Running() const { return not std::holds_alternative<std::monostate>(active_); }
  [[nodiscard]] ComponentType Active() const {
    return std::visit(
        [](const auto& component) {
          using Current = std::remove_cvref_t<decltype(component)>;
          if constexpr (std::same_as<Current, std::monostate>) {
            return ComponentType::Quit;
          } else {
            return Current::kType;
          }
        },
        active_);
  }

 private:
  using Variant = std::variant<std::monostate, Components...>;

  // The caller has checked that type is registered. Construct directly in its final storage.
  void Enter(ComponentType type) {
    ((type == Components::kType ? static_cast<void>(active_.template emplace<Components>()) : void()), ...);
    Visit([this](auto& component) { component.OnEnter(state_); });
  }

  template <typename Visitor>
  auto Visit(Visitor&& visitor) {
    using First = std::variant_alternative_t<1, Variant>;
    using Result = std::invoke_result_t<Visitor, First&>;
    return std::visit(
        [&visitor](auto& component) -> Result {
          using Current = std::remove_cvref_t<decltype(component)>;
          if constexpr (not std::same_as<Current, std::monostate>) {
            return std::forward<Visitor>(visitor)(component);
          } else if constexpr (not std::is_void_v<Result>) {
            return Result{};
          }
        },
        active_);
  }

  EngineState& state_;
  Variant active_;
};

}  // namespace hp2
