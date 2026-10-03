#include "core/component.hpp"

#include <gtest/gtest.h>

#include <string>

namespace hp2 {
namespace {

struct Log {
  std::string events;
};

// Stays for `stay` steps, then hands over to `next`.
template <ComponentType Type, ComponentType Next>
class Fake {
 public:
  static constexpr ComponentType kType = Type;

  Fake(Log& log, char name, int stay) : log_(&log), name_(name), stay_(stay) {}

  void OnEnter() {
    log_->events += name_;
    log_->events += '>';
    remaining_ = stay_;
  }
  void OnExit() {
    log_->events += name_;
    log_->events += '<';
  }
  ComponentType Step(float delta_seconds) {
    log_->events += name_;
    log_->events += std::to_string(static_cast<int>(delta_seconds));
    --remaining_;
    return remaining_ > 0 ? kType : Next;
  }

 private:
  Log* log_;
  char name_;
  int stay_;
  int remaining_{};
};

using Title = Fake<ComponentType::kTitle, ComponentType::kOffice>;
using Office = Fake<ComponentType::kOffice, ComponentType::kQuit>;
using Orphan = Fake<ComponentType::kStation, ComponentType::kDriving>;

TEST(Engine, RunsTransitionsAndQuits) {
  Log log;
  Engine<Title, Office> engine{Title{log, 'T', 2}, Office{log, 'O', 1}};
  EXPECT_FALSE(engine.Running());
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  EXPECT_TRUE(engine.Running());
  EXPECT_TRUE(engine.Step(1.0F));
  EXPECT_TRUE(engine.Step(2.0F));
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_TRUE(engine.Step(3.0F));
  EXPECT_FALSE(engine.Running());
  EXPECT_FALSE(engine.Step(4.0F));
  EXPECT_EQ(log.events, "T>T1T2T<O>O3O<");
}

TEST(Engine, RefusesUnregisteredTransitions) {
  Log log;
  Engine<Orphan> engine{Orphan{log, 'S', 1}};
  EXPECT_FALSE(engine.Start(ComponentType::kOffice));
  EXPECT_FALSE(engine.Running());
  ASSERT_TRUE(engine.Start(ComponentType::kStation));
  EXPECT_FALSE(engine.Step(1.0F));  // asks for kDriving, which is not registered
  EXPECT_EQ(engine.Active(), ComponentType::kStation);
  EXPECT_TRUE(engine.Running());
  EXPECT_EQ(log.events, "S>S1");
}

TEST(Engine, SwitchesFromOutside) {
  Log log;
  Engine<Title, Office> engine{Title{log, 'T', 5}, Office{log, 'O', 5}};
  EXPECT_FALSE(engine.Switch(ComponentType::kOffice));  // not running yet
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  EXPECT_TRUE(engine.Step(1.0F));
  EXPECT_TRUE(engine.Switch(ComponentType::kOffice));
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_FALSE(engine.Switch(ComponentType::kDriving));  // unregistered: stays
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_TRUE(engine.Switch(ComponentType::kOffice));  // re-entered
  EXPECT_EQ(log.events, "T>T1T<O>O<O>");
}

static_assert(Engine<Title, Office>::IsRegistered(ComponentType::kOffice));
static_assert(not Engine<Title, Office>::IsRegistered(ComponentType::kDriving));

}  // namespace
}  // namespace hp2
