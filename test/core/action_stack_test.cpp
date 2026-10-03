#include "core/action_stack.hpp"

#include <gtest/gtest.h>

#include <string>

namespace hp2 {
namespace {

struct Log {
  std::string events;
};

// Completes after `duration` of ticks, recording its lifecycle.
class Wait {
 public:
  Wait(Log& log, char name, float duration_seconds) : log_(&log), name_(name), duration_seconds_(duration_seconds) {}

  void Init() {
    log_->events += name_;
    log_->events += 'i';
    remaining_ = duration_seconds_;
  }
  bool Tick(float delta_seconds) {
    log_->events += name_;
    remaining_ -= delta_seconds;
    return remaining_ <= 0.0F;
  }

 private:
  Log* log_;
  char name_;
  float duration_seconds_;
  float remaining_{};
};

// Completes on its first tick.
class Instant {
 public:
  explicit Instant(Log& log) : log_(&log) {}
  void Init() { log_->events += "Ii"; }
  bool Tick([[maybe_unused]] float delta_seconds) {
    log_->events += 'I';
    return true;
  }

 private:
  Log* log_;
};

using Stack = ActionStack<3, Wait, Instant>;

TEST(ActionStack, RunsPushedSequenceInReverseOrderInitialisingOnce) {
  Log log;
  Stack stack;
  ASSERT_TRUE(stack.Push<Instant>(log));
  ASSERT_TRUE(stack.Push<Wait>(log, 'a', 0.002F));
  EXPECT_EQ(stack.Size(), 2U);

  stack.Tick(0.001F);
  stack.Tick(0.001F);
  EXPECT_EQ(log.events, "aiaa");
  EXPECT_EQ(stack.Size(), 1U);
  stack.Tick(0.001F);
  EXPECT_EQ(log.events, "aiaaIiI");
  EXPECT_TRUE(stack.Empty());
  stack.Tick(0.001F);  // ticking an empty stack does nothing
  EXPECT_EQ(log.events, "aiaaIiI");
}

TEST(ActionStack, ClearAbandonsPendingActions) {
  Log log;
  Stack stack;
  ASSERT_TRUE(stack.Push<Instant>(log));
  ASSERT_TRUE(stack.Push<Wait>(log, 'b', 0.010F));
  stack.Tick(0.001F);
  stack.Clear();
  EXPECT_TRUE(stack.Empty());
  stack.Tick(0.001F);
  EXPECT_EQ(log.events, "bib");
}

TEST(ActionStack, RefusesPushBeyondCapacity) {
  Log log;
  Stack stack;
  EXPECT_TRUE(stack.Push<Instant>(log));
  EXPECT_TRUE(stack.Push<Instant>(log));
  EXPECT_TRUE(stack.Push<Instant>(log));
  EXPECT_FALSE(stack.Push<Instant>(log));
  EXPECT_EQ(stack.Size(), Stack::MaxSize());
}

}  // namespace
}  // namespace hp2
