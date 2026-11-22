#include "src/engine/spring-math.h"

#include "src/fp1616.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(SpringMathTest, ExactPlantStepResponseAndGlobalPeakOvershoot) {
  EXPECT_NEAR(kWindowManagerTrackingSpring.lag_time(), 0.0410620, 1e-6);

  const SpringState s50 =
      StepSpringExact(SpringState{.y = 0.0, .v = 0.0}, 1.0, 0.050);
  EXPECT_NEAR(s50.y, 0.676635, 1e-5);
  EXPECT_NEAR(s50.v, 11.8803, 1e-3);

  const SpringState s100 =
      StepSpringExact(SpringState{.y = 0.0, .v = 0.0}, 1.0, 0.100);
  EXPECT_NEAR(s100.y, 0.975876, 1e-5);

  const double t_peak =
      std::numbers::pi / kWindowManagerTrackingSpring.damped_omega();
  EXPECT_NEAR(t_peak, 0.146974, 1e-5);
  const SpringState s_peak =
      StepSpringExact(SpringState{.y = 0.0, .v = 0.0}, 1.0, t_peak);
  EXPECT_NEAR(s_peak.y, 1.005019, 1e-5);
  EXPECT_NEAR(s_peak.v, 0.0, 1e-10);

  // Verify global [0, infinity) ComputeAnalyticalPeakOvershoot against a dense
  // forward simulation in both directions, including peaks past 0.145 s and
  // initial overshoot at t = 0.
  struct PeakCase {
    SpringState state;
    double target;
    int dir;
  };
  const std::vector<PeakCase> cases = {
      {{0.0, 0.0}, 1.0, +1},
      {{0.95, 2.5}, 1.0, +1},
      {{1.003, -0.5}, 1.0, +1},
      {{1.0, 0.0}, 0.0, -1},
      {{0.05, -2.5}, 0.0, -1},
      {{0.999, -0.2}, 1.0, +1},  // late second-half-period peak beyond 0.145 s
  };

  for (const auto& c : cases) {
    const double analytical =
        ComputeAnalyticalPeakOvershoot(c.state, c.target, c.dir);
    double numerical = 0.0;
    for (int i = 0; i <= 10000; ++i) {
      const double t = static_cast<double>(i) * 0.0001;
      const double y = StepSpringExact(c.state, c.target, t).y;
      const double ov = (c.dir > 0) ? (y - c.target) : (c.target - y);
      numerical = std::max(numerical, ov);
    }
    EXPECT_NEAR(analytical, numerical, 1e-7);
  }
}

TEST(SpringMathTest, PrecomputedStepTablesMatchStepSpringExact) {
  const std::vector<SpringState> test_states = {
      {0.0, 0.0},
      {0.42, 3.15},
      {0.97, -1.85},
      {1.0, 0.0},
  };
  constexpr std::array<double, 4> kTargets = {0.0, kFixedFP1616Lsb, 0.65, 1.0};

  const auto& lookahead = GetLookaheadStepTable();
  EXPECT_EQ(lookahead.size(), static_cast<std::size_t>(kLookaheadSamples));
  const auto& safety = GetSafetyStepTable();
  EXPECT_EQ(safety.size(), static_cast<std::size_t>(kSafetyTrackSamples));

  auto verify_table = [&](const auto& table) {
    for (const auto& row : table) {
      for (const SpringState& st : test_states) {
        for (double u : kTargets) {
          const double y_exact = StepSpringExact(st, u, row.tau).y;
          const double y_table =
              row.a_y * st.y + row.a_v * st.v + row.y_step * u;
          EXPECT_NEAR(y_table, y_exact, 1e-14);
        }
      }
    }
  };

  verify_table(lookahead);
  verify_table(safety);
}

}  // namespace
}  // namespace fasterswiper
