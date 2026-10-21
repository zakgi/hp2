#include "core/component.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <type_traits>
#include <utility>

#include "core/engine_state.hpp"

namespace hp2 {
namespace {

struct Log {
  std::string events;
};

auto log = Log{};

// Empty controllers keep their countdown in the shared career data for these dispatch tests.
// Test controllers implement the same instance-based lifecycle protocol.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
template <ComponentType Type, ComponentType Next, char Name, std::uint32_t Stay>
class Fake {
 public:
  static constexpr ComponentType kType = Type;

  void OnEnter(EngineState& state) {
    log.events += Name;
    log.events += '>';
    state.game.score = Stay;
  }
  void OnExit([[maybe_unused]] EngineState& state) {
    log.events += Name;
    log.events += '<';
  }
  ComponentType Step(EngineState& state, float delta_seconds) {
    log.events += Name;
    log.events += std::to_string(static_cast<int>(delta_seconds));
    --state.game.score;
    return state.game.score > 0 ? kType : Next;
  }
};

using Title = Fake<ComponentType::kTitle, ComponentType::kOffice, 'T', 2>;
using Office = Fake<ComponentType::kOffice, ComponentType::kQuit, 'O', 1>;
using Orphan = Fake<ComponentType::kStation, ComponentType::kHighway, 'S', 1>;

class PrivateCounter {
 public:
  static constexpr ComponentType kType = ComponentType::kTitle;

  void OnEnter([[maybe_unused]] EngineState& state) { log.events += '>'; }
  void OnExit([[maybe_unused]] EngineState& state) { log.events += '<'; }
  ComponentType Step(EngineState& state, [[maybe_unused]] float delta_seconds) {
    state.game.score = ++steps_;
    return kType;
  }

 private:
  std::uint32_t steps_{};
};

class QuitController {
 public:
  static constexpr ComponentType kType = ComponentType::kTitle;

  QuitController() { log.events += 'C'; }
  ~QuitController() { log.events += 'D'; }
  void OnEnter([[maybe_unused]] EngineState& state) { log.events += '>'; }
  void OnExit([[maybe_unused]] EngineState& state) { log.events += '<'; }
  ComponentType Step([[maybe_unused]] EngineState& state, [[maybe_unused]] float delta_seconds) {
    return ComponentType::kQuit;
  }
};

// NOLINTEND(readability-convert-member-functions-to-static)

class EngineTest : public testing::Test {
 protected:
  void SetUp() override { log.events.clear(); }

  std::array<std::uint8_t, RoadMapView::kSize * RoadMapView::kSize> cells_{};
  EngineAssets assets_{.road_map = RoadMapView{.cells = cells_}};
  Screen screen_;
  KeyEvents keys_;
  AudioEngine audio_;
  EngineState state_{assets_, screen_, keys_, audio_, 42};
};

TEST_F(EngineTest, RunsTransitionsAndQuits) {
  auto engine = Engine<Title, Office>{state_};
  EXPECT_FALSE(engine.Running());
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  EXPECT_TRUE(engine.Running());
  EXPECT_EQ(state_.game.score, 2U);
  EXPECT_TRUE(engine.Step(1.0F));
  EXPECT_EQ(state_.game.score, 1U);
  EXPECT_TRUE(engine.Step(2.0F));
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_TRUE(engine.Step(3.0F));
  EXPECT_FALSE(engine.Running());
  EXPECT_FALSE(engine.Step(4.0F));
  EXPECT_EQ(log.events, "T>T1T2T<O>O3O<");
}

TEST_F(EngineTest, RefusesUnregisteredTransitions) {
  auto engine = Engine<Orphan>{state_};
  EXPECT_FALSE(engine.Start(ComponentType::kOffice));
  EXPECT_FALSE(engine.Running());
  ASSERT_TRUE(engine.Start(ComponentType::kStation));
  EXPECT_FALSE(engine.Step(1.0F));
  EXPECT_EQ(engine.Active(), ComponentType::kStation);
  EXPECT_TRUE(engine.Running());
  EXPECT_EQ(log.events, "S>S1");
}

TEST_F(EngineTest, SwitchesFromOutside) {
  using LongTitle = Fake<ComponentType::kTitle, ComponentType::kOffice, 'T', 5>;
  using LongOffice = Fake<ComponentType::kOffice, ComponentType::kQuit, 'O', 5>;
  auto engine = Engine<LongTitle, LongOffice>{state_};
  EXPECT_FALSE(engine.Switch(ComponentType::kOffice));
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  EXPECT_TRUE(engine.Step(1.0F));
  EXPECT_TRUE(engine.Switch(ComponentType::kOffice));
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_FALSE(engine.Switch(ComponentType::kHighway));
  EXPECT_EQ(engine.Active(), ComponentType::kOffice);
  EXPECT_TRUE(engine.Switch(ComponentType::kOffice));
  EXPECT_EQ(log.events, "T>T1T<O>O<O>");
}

TEST_F(EngineTest, IndependentEnginesKeepSeparateState) {
  auto other_state = EngineState{assets_, screen_, keys_, audio_, 99};
  auto first = Engine<Title>{state_};
  auto second = Engine<Title>{other_state};
  ASSERT_TRUE(first.Start(ComponentType::kTitle));
  ASSERT_TRUE(second.Start(ComponentType::kTitle));
  ASSERT_TRUE(first.Step(1.0F));
  EXPECT_EQ(state_.game.score, 1U);
  EXPECT_EQ(other_state.game.score, 2U);
}

TEST_F(EngineTest, MovingTheEngineLeavesMissionStorageInPlace) {
  // The empty map completes on the first tick; a fake ending accepts that transition.
  using Ending = Fake<ComponentType::kMissionEnd, ComponentType::kQuit, 'E', 1>;
  auto engine = Engine<Highway, Ending>{state_};
  // Exercise relocation even when the registered controllers are trivially copyable.
  // NOLINTNEXTLINE(performance-move-const-arg)
  auto moved = std::move(engine);
  ASSERT_TRUE(moved.Start(ComponentType::kHighway));
  EXPECT_EQ(state_.mission.GetTick(), 0U);
  EXPECT_TRUE(moved.Step(Mission::kTickSeconds));
  EXPECT_EQ(state_.mission.GetTick(), 1U);
  EXPECT_EQ(moved.Active(), ComponentType::kMissionEnd);
}

TEST_F(EngineTest, ReenteringConstructsFreshPrivateControllerState) {
  auto engine = Engine<PrivateCounter>{state_};
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_EQ(state_.game.score, 1U);
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_EQ(state_.game.score, 2U);
  ASSERT_TRUE(engine.Switch(ComponentType::kTitle));
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_EQ(state_.game.score, 1U);
  EXPECT_EQ(log.events, "><>");
}

TEST_F(EngineTest, ConstructsOnlyOnEntryAndDestroysOnQuit) {
  auto engine = Engine<QuitController>{state_};
  EXPECT_TRUE(log.events.empty());
  EXPECT_EQ(engine.Active(), ComponentType::kQuit);
  ASSERT_TRUE(engine.Start(ComponentType::kTitle));
  EXPECT_EQ(log.events, "C>");
  ASSERT_TRUE(engine.Step(0.0F));
  EXPECT_FALSE(engine.Running());
  EXPECT_EQ(engine.Active(), ComponentType::kQuit);
  EXPECT_EQ(log.events, "C><D");
}

static_assert(ComponentLike<PrivateCounter>);
static_assert(Engine<Title, Office>::IsRegistered(ComponentType::kOffice));
static_assert(not Engine<Title, Office>::IsRegistered(ComponentType::kHighway));
static_assert(not std::is_copy_constructible_v<EngineState>);
static_assert(not std::is_move_constructible_v<EngineState>);

}  // namespace
}  // namespace hp2
