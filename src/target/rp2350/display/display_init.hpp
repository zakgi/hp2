#pragma once

// Display boot-sequence building blocks shared by the SPI panel drivers. A board describes its
// panel's register bring-up as a flat constexpr std::array of DisplayStep in its own
// <driver>_init.hpp; the driver includes that header by name and walks the table.

#include <cstdint>

namespace hp2 {

enum class DisplayBusAction : std::uint8_t {
  ChipSelectAssert,
  ChipSelectDeassert,
  Wait,
  Command,
  Data,
};

// One bus action and its argument: command byte, data byte, or milliseconds.
struct DisplayStep {
  DisplayBusAction action;
  std::uint16_t value{0};
};

constexpr DisplayStep SelectPanel() {
  return {.action = DisplayBusAction::ChipSelectAssert};
}
constexpr DisplayStep DeselectPanel() {
  return {.action = DisplayBusAction::ChipSelectDeassert};
}
constexpr DisplayStep Command(std::uint8_t reg) {
  return {.action = DisplayBusAction::Command, .value = reg};
}
constexpr DisplayStep Data(std::uint8_t byte) {
  return {.action = DisplayBusAction::Data, .value = byte};
}
constexpr DisplayStep WaitMs(std::uint16_t millis) {
  return {.action = DisplayBusAction::Wait, .value = millis};
}

}  // namespace hp2
