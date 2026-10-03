#include "core/key_events.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace hp2 {
namespace {

TEST(KeyEvents, KeepsArrivalOrder) {
  auto events = KeyEvents{};
  EXPECT_TRUE(events.Record({.key = Key::kS, .action = KeyAction::kPress}));
  EXPECT_TRUE(events.Record({.key = Key::kT, .action = KeyAction::kPress}));
  EXPECT_TRUE(events.Record({.key = Key::kS, .action = KeyAction::kRelease}));
  EXPECT_EQ(events.Pending(), 3U);
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::kS, .action = KeyAction::kPress}));
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::kT, .action = KeyAction::kPress}));
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::kS, .action = KeyAction::kRelease}));
  EXPECT_FALSE(events.Next().has_value());
}

TEST(KeyEvents, DropsTheNewestWhenFull) {
  auto events = KeyEvents{};
  for (auto index = std::size_t{0}; index < KeyEvents::kCapacity; ++index) {
    EXPECT_TRUE(events.Record({.key = Key::kA, .action = KeyAction::kPress}));
  }
  EXPECT_FALSE(events.Record({.key = Key::kB, .action = KeyAction::kPress}));
  EXPECT_EQ(events.Dropped(), 1U);
  EXPECT_EQ(events.Pending(), KeyEvents::kCapacity);
  EXPECT_EQ(events.Next()->key, Key::kA);
  // Space again after one is consumed; the queue wraps.
  EXPECT_TRUE(events.Record({.key = Key::kC, .action = KeyAction::kRelease}));
  for (auto index = std::size_t{1}; index < KeyEvents::kCapacity; ++index) {
    EXPECT_EQ(events.Next()->key, Key::kA);
  }
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::kC, .action = KeyAction::kRelease}));
}

TEST(KeyEvents, ClearForgetsPendingKeystrokes) {
  auto events = KeyEvents{};
  events.Record({.key = Key::kEscape, .action = KeyAction::kPress});
  events.Clear();
  EXPECT_EQ(events.Pending(), 0U);
  EXPECT_FALSE(events.Next().has_value());
}

}  // namespace
}  // namespace hp2
