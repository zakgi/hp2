#pragma once

// Display boot-sequence building blocks shared by the SPI panel drivers. A board describes its
// panel's register bring-up as a flat constexpr std::array of DisplayStep in its own
// <driver>_init.hpp; the driver includes that header by name and walks the table.

#include <cstdint>

namespace hp2 {

enum class DisplayBusAction : std::uint8_t {
  kChipSelectAssert,
  kChipSelectDeassert,
  kWait,
  kCommand,
  kData,
};

// One bus action and its argument: command byte, data byte, or milliseconds.
struct DisplayStep {
  DisplayBusAction action;
  std::uint16_t value{0};
};

constexpr DisplayStep SelectPanel() {
  return {.action = DisplayBusAction::kChipSelectAssert};
}
constexpr DisplayStep DeselectPanel() {
  return {.action = DisplayBusAction::kChipSelectDeassert};
}
constexpr DisplayStep Command(std::uint8_t reg) {
  return {.action = DisplayBusAction::kCommand, .value = reg};
}
constexpr DisplayStep Data(std::uint8_t byte) {
  return {.action = DisplayBusAction::kData, .value = byte};
}
constexpr DisplayStep WaitMs(std::uint16_t millis) {
  return {.action = DisplayBusAction::kWait, .value = millis};
}

}  // namespace hp2
