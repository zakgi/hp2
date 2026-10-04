#include "core/mission.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "assets.hpp"
#include "core/ai.hpp"
#include "core/game_state.hpp"
#include "core/road.hpp"
#include "core/world.hpp"
#include "host/asset_manager.hpp"

namespace hp2 {
namespace {

class MissionTest : public testing::Test {
 protected:
  void SetUp() override {
    if (not test::Executable()) {
      GTEST_SKIP() << "game disk not present at " << test::DiskImage();
    }
    ASSERT_TRUE(manager_.Load(test::DiskImage()));
  }
  [[nodiscard]] Road MakeRoad() const {
    const auto& assets = manager_.Engine();
    return Road{assets.road_map, assets.road_shapes, assets.scenery};
  }
  host::AssetManager manager_;
};

TEST_F(MissionTest, StartsTheCarsAtTheOriginalsPlaces) {
  auto mission = Mission{MakeRoad()};
  for (auto seed = std::uint64_t{0}; seed < 32; ++seed) {
    mission.Start(kMissions[0], AiPolicies{}, seed);
    const auto& road = mission.GetRoad();
    const auto player = mission.GetPlayer().position;
    const auto cell = GetCell(player);
    EXPECT_TRUE(cell == (Cell{.x = 11, .y = 15}) or cell == (Cell{.x = 24, .y = 11}) or
                cell == (Cell{.x = 25, .y = 28}))
        << "seed " << seed;
    EXPECT_TRUE(road.IsOnRoad(player)) << "seed " << seed;
    // Every start of the criminal is a crossroads.
    EXPECT_EQ(road.GetCellType(GetCell(mission.GetTarget().position)), 10) << "seed " << seed;
  }
  EXPECT_EQ(mission.GetProgress().bounty, 2000);
}

TEST_F(MissionTest, TheSameSeedStartsTheSameMission) {
  auto first = Mission{MakeRoad()};
  auto second = Mission{MakeRoad()};
  first.Start(kMissions[2], AiPolicies{}, 1234);
  second.Start(kMissions[2], AiPolicies{}, 1234);
  EXPECT_EQ(first.GetPlayer().position.x, second.GetPlayer().position.x);
  EXPECT_EQ(first.GetPlayer().position.y, second.GetPlayer().position.y);
  EXPECT_EQ(first.GetTarget().position.x, second.GetTarget().position.x);
  EXPECT_EQ(first.GetTarget().position.y, second.GetTarget().position.y);
}

}  // namespace
}  // namespace hp2
