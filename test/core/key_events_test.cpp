#include "core/key_events.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace hp2 {
namespace {

TEST(KeyEvents, KeepsArrivalOrder) {
  auto events = KeyEvents{};
  EXPECT_TRUE(events.Record({.key = Key::S, .action = KeyAction::Press}));
  EXPECT_TRUE(events.Record({.key = Key::T, .action = KeyAction::Press}));
  EXPECT_TRUE(events.Record({.key = Key::S, .action = KeyAction::Release}));
  EXPECT_EQ(events.Pending(), 3U);
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::S, .action = KeyAction::Press}));
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::T, .action = KeyAction::Press}));
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::S, .action = KeyAction::Release}));
  EXPECT_FALSE(events.Next().has_value());
}

TEST(KeyEvents, DropsTheNewestWhenFull) {
  auto events = KeyEvents{};
  for (auto index = std::size_t{0}; index < KeyEvents::kCapacity; ++index) {
    EXPECT_TRUE(events.Record({.key = Key::A, .action = KeyAction::Press}));
  }
  EXPECT_FALSE(events.Record({.key = Key::B, .action = KeyAction::Press}));
  EXPECT_EQ(events.Dropped(), 1U);
  EXPECT_EQ(events.Pending(), KeyEvents::kCapacity);
  EXPECT_EQ(events.Next()->key, Key::A);
  // Space again after one is consumed; the queue wraps.
  EXPECT_TRUE(events.Record({.key = Key::C, .action = KeyAction::Release}));
  for (auto index = std::size_t{1}; index < KeyEvents::kCapacity; ++index) {
    EXPECT_EQ(events.Next()->key, Key::A);
  }
  EXPECT_EQ(events.Next(), (KeyEvent{.key = Key::C, .action = KeyAction::Release}));
}

TEST(KeyEvents, ClearForgetsPendingKeystrokes) {
  auto events = KeyEvents{};
  events.Record({.key = Key::Escape, .action = KeyAction::Press});
  events.Clear();
  EXPECT_EQ(events.Pending(), 0U);
  EXPECT_FALSE(events.Next().has_value());
}

}  // namespace
}  // namespace hp2
