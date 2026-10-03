#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/key.hpp"

namespace hp2 {

struct KeyMapping {
  Key key;
  std::int32_t native;
};

// A platform backend's table from neutral keys to its own key codes. Construction validates both
// directions at compile time: every Key mapped once, no native code used twice.
class KeyMap {
 public:
  consteval explicit KeyMap(std::array<KeyMapping, kKeyCount> mappings) : reverse_{mappings} {
    std::array<bool, kKeyCount> seen{};
    for (const KeyMapping& mapping : mappings) {
      const std::size_t index = std::to_underlying(mapping.key);
      if (index >= seen.size() or seen[index]) {
        Invalid("invalid or duplicate neutral key");
      }
      seen[index] = true;
      forward_[index] = mapping.native;
    }
    std::ranges::sort(reverse_, {}, &KeyMapping::native);
    for (std::size_t index = 1; index < reverse_.size(); ++index) {
      if (reverse_[index - 1].native == reverse_[index].native) {
        Invalid("duplicate native key");
      }
    }
  }

  [[nodiscard]] constexpr std::int32_t ToNative(Key key) const {
    const std::size_t index = std::to_underlying(key);
    return forward_[index < forward_.size() ? index : 0];
  }

  [[nodiscard]] constexpr Key FromNative(std::int32_t native) const {
    const auto* found = std::ranges::lower_bound(reverse_, native, {}, &KeyMapping::native);
    return found != reverse_.end() and found->native == native ? found->key : Key::kNone;
  }

 private:
  // Declared, never defined: calling it inside the consteval constructor makes the constant
  // evaluation ill-formed, which is how a table error becomes a compile error without exceptions.
  static void Invalid(const char* reason);

  std::array<std::int32_t, kKeyCount> forward_{};
  std::array<KeyMapping, kKeyCount> reverse_{};
};

}  // namespace hp2
