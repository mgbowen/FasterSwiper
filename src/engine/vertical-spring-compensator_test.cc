#include "src/engine/vertical-spring-compensator.h"

#include "src/easing.h"
#include "src/engine/spring-math.h"
#include "src/fp1616.h"
#include "third_party/chromium/cubic-bezier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

using third_party::chromium::gfx::CubicBezier;

constexpr double kRetina6KEpsilon = 0.5 / 3008.0;  // 1.66223e-4

// Exact closed-form critically damped (zeta = 1.0) spring step used only in
// tests to model WindowManager's .ended release springs (Win15 and SB38).
SpringState StepCriticalSpring(SpringState state, double target, double dt,
                               double response_sec) {
  if (dt <= 0.0) {
    return state;
  }
  const double gamma = (2.0 * std::numbers::pi) / response_sec;
  const double e0 = state.y - target;
  const double b = gamma * e0 + state.v;
  const double decay = std::exp(-gamma * dt);
  const double d9 = e0 + dt * b;
  const double d10 = b - gamma * d9;
  return SpringState{.y = target + decay * d9, .v = decay * d10};
}

double MaxCriticalReleaseOvershoot(SpringState state, double target,
                                   int direction, double response_sec) {
  double max_ov = 0.0;
  constexpr int kSteps = 200;
  constexpr double kDt = 0.002;
  SpringState cur = state;
  for (int i = 0; i <= kSteps; ++i) {
    const double ov = (direction > 0) ? (cur.y - target) : (target - cur.y);
    max_ov = std::max(max_ov, ov);
    cur = StepCriticalSpring(cur, target, kDt, response_sec);
  }
  return max_ov;
}

struct NamedCurve {
  std::string name;
  EasingFunction easing;
  double a2_dw25_240hz_ready_ms = 0.0;
  bool is_monotone = true;
};

std::vector<NamedCurve> GetAllTestCurves() {
  return {
      {"linear", MakeEasingFunctionLinear(), 312.5, true},
      {"quad", MakeEasingFunctionEaseOutQuadratic(), 275.0, true},
      {"quint", MakeEasingFunctionEaseOutQuintic(), 183.35, false},
      {"ease_in_out",
       MakeEasingFunctionBezier(CubicBezier(0.42, 0.0, 0.58, 1.0)), 275.0,
       true},
      {"ease_in", MakeEasingFunctionBezier(CubicBezier(0.42, 0.0, 1.0, 1.0)),
       316.7, true},
      {"overshoot_back_out",
       MakeEasingFunctionBezier(CubicBezier(0.34, 1.56, 0.64, 1.0)), 195.85,
       false},
      {"anticipate_overshoot",
       MakeEasingFunctionBezier(CubicBezier(0.68, -0.55, 0.27, 1.55)), 245.85,
       false},
  };
}

struct SimulationMetrics {
  double rmse = 0.0;
  double max_overshoot = 0.0;
  double u_dip = 0.0;
  double t_ready_ms = 0.0;
  bool settled = false;
  SpringState final_plant;
  double final_u = 0.0;
  int64_t command_dwell_ns = 0;
};

SimulationMetrics RunCurveSimulation(
    const EasingFunction& easing, double duration_s, int tick_hz,
    double start_r, double target_r,
    const SpringParams& plant_params = kWindowManagerTrackingSpring) {
  const int dir = (target_r >= start_r) ? 1 : -1;
  const double target_cmd = std::clamp(target_r, kFixedFP1616Lsb, 1.0);
  const int64_t dt_ns = 1'000'000'000LL / tick_hz;
  const double dt_s = static_cast<double>(dt_ns) * 1e-9;

  VerticalSpringCompensator comp;
  const int64_t base_ns = 1'000'000'000LL;
  comp.Reset(start_r, 0.0, base_ns);

  SpringState plant{.y = start_r, .v = 0.0};
  double prev_u =
      std::clamp(QuantizeFP1616(std::clamp(start_r, kFixedFP1616Lsb, 1.0)),
                 kFixedFP1616Lsb, 1.0);

  SimulationMetrics out;
  double sq_err = 0.0;
  int rmse_count = 0;
  bool has_dwell = false;
  int64_t dwell_start_ns = 0;

  const double max_s = duration_s + 0.250;
  const int max_ticks = static_cast<int>(std::ceil(max_s / dt_s)) + 2;

  for (int tick = 0; tick <= max_ticks; ++tick) {
    const int64_t now_ns = base_ns + tick * dt_ns;
    const double elapsed_s = static_cast<double>(tick * dt_ns) * 1e-9;

    if (tick > 0) {
      // 8x sub-stepped plant peak check between consecutive ticks.
      constexpr int kSubSteps = 8;
      const double sub_dt = dt_s / kSubSteps;
      for (int s = 0; s < kSubSteps; ++s) {
        plant = StepSpringExact(plant, prev_u, sub_dt, plant_params);
        const double ov =
            (dir > 0) ? (plant.y - target_r) : (target_r - plant.y);
        out.max_overshoot = std::max(out.max_overshoot, ov);
      }
    }

    if (elapsed_s <= duration_s + 1e-9) {
      const double s = std::clamp(elapsed_s / duration_s, 0.0, 1.0);
      const double r_ref =
          std::clamp(start_r + (target_r - start_r) * easing(s), 0.0, 1.0);
      const double err = plant.y - r_ref;
      sq_err += err * err;
      ++rmse_count;
    }

    const auto res = comp.StepAnimationTick(elapsed_s, duration_s, start_r,
                                            target_r, easing, now_ns);
    if (tick > 0) {
      const double dip = (dir > 0) ? std::max(0.0, prev_u - res.command)
                                   : std::max(0.0, res.command - prev_u);
      out.u_dip = std::max(out.u_dip, dip);
    }
    prev_u = res.command;

    if (std::abs(target_cmd - res.command) <= 0.5 * kFixedFP1616Lsb) {
      if (!has_dwell) {
        has_dwell = true;
        dwell_start_ns = now_ns;
      }
      out.command_dwell_ns = now_ns - dwell_start_ns;
    } else {
      has_dwell = false;
      out.command_dwell_ns = 0;
    }

    if (res.settled || elapsed_s >= max_s) {
      out.settled = res.settled;
      out.t_ready_ms = elapsed_s * 1000.0;
      out.final_plant = plant;
      out.final_u = res.command;
      break;
    }
  }

  out.rmse = rmse_count > 0 ? std::sqrt(sq_err / rmse_count) : 0.0;
  return out;
}

TEST(VerticalSpringCompensatorTest, Tracks200msCurvesWithLowRmse) {
  const EasingFunction linear = MakeEasingFunctionLinear();
  const EasingFunction quad = MakeEasingFunctionEaseOutQuadratic();
  const EasingFunction ease_in_out =
      MakeEasingFunctionBezier(CubicBezier(0.42, 0.0, 0.58, 1.0));

  EXPECT_LT(RunCurveSimulation(linear, 0.200, 240, 0.0, 1.0).rmse, 0.025);
  EXPECT_LT(RunCurveSimulation(quad, 0.200, 240, 0.0, 1.0).rmse, 0.025);
  EXPECT_LT(RunCurveSimulation(ease_in_out, 0.200, 240, 0.0, 1.0).rmse, 0.025);
  EXPECT_LT(RunCurveSimulation(linear, 0.200, 120, 0.0, 1.0).rmse, 0.025);
  EXPECT_LT(RunCurveSimulation(quad, 0.200, 60, 0.0, 1.0).rmse, 0.025);
  EXPECT_LT(RunCurveSimulation(linear, 0.200, 60, 0.0, 1.0).rmse, 0.030);
}

TEST(VerticalSpringCompensatorTest,
     T1Strict6kBoundsAllCurvesTickRatesAndDirections) {
  const std::vector<NamedCurve> curves = GetAllTestCurves();
  constexpr std::array<int, 3> kTickRates = {60, 120, 240};

  for (const auto& curve : curves) {
    for (int hz : kTickRates) {
      for (bool forward : {true, false}) {
        const double start_r = forward ? 0.0 : 1.0;
        const double target_r = forward ? 1.0 : 0.0;
        const SimulationMetrics m =
            RunCurveSimulation(curve.easing, 0.200, hz, start_r, target_r);

        SCOPED_TRACE(curve.name + " @" + std::to_string(hz) + "Hz " +
                     (forward ? "0->1" : "1->0"));
        EXPECT_TRUE(m.settled);
        EXPECT_DOUBLE_EQ(m.u_dip, 0.0);
        EXPECT_LE(m.max_overshoot, 1.0e-4);
        EXPECT_LT(std::abs(target_r - m.final_plant.y), kRetina6KEpsilon);
        EXPECT_LE(std::abs(m.final_plant.v), 0.04);
        EXPECT_GE(m.command_dwell_ns, 25'000'000LL);

        if (hz == 240 && forward) {
          EXPECT_LE(m.t_ready_ms, curve.a2_dw25_240hz_ready_ms);
        }
      }
    }
  }
}

TEST(VerticalSpringCompensatorTest, PlantMismatch15PercentStressTest) {
  const std::vector<NamedCurve> curves = GetAllTestCurves();

  // +-15% response period mismatch.
  for (double resp_scale : {0.85, 1.15}) {
    const SpringParams plant{
        .damping_ratio = kWindowManagerTrackingSpring.damping_ratio,
        .response_sec = kWindowManagerTrackingSpring.response_sec * resp_scale,
    };
    for (const auto& curve : curves) {
      const SimulationMetrics m =
          RunCurveSimulation(curve.easing, 0.200, 240, 0.0, 1.0, plant);
      SCOPED_TRACE(curve.name + " resp_scale=" + std::to_string(resp_scale));
      EXPECT_TRUE(m.settled);
      EXPECT_LE(m.max_overshoot, 0.002);
      EXPECT_GE(m.command_dwell_ns, 25'000'000LL);
    }
  }

  // +-15% damping ratio mismatch.
  for (double zeta_scale : {0.85, 1.15}) {
    const SpringParams plant{
        .damping_ratio =
            kWindowManagerTrackingSpring.damping_ratio * zeta_scale,
        .response_sec = kWindowManagerTrackingSpring.response_sec,
    };
    for (const auto& curve : curves) {
      const SimulationMetrics m =
          RunCurveSimulation(curve.easing, 0.200, 240, 0.0, 1.0, plant);
      SCOPED_TRACE(curve.name + " zeta_scale=" + std::to_string(zeta_scale));
      EXPECT_TRUE(m.settled);
      EXPECT_GE(m.command_dwell_ns, 25'000'000LL);
      if (zeta_scale > 1.0) {
        EXPECT_LE(m.max_overshoot, 0.003);
      } else if (curve.is_monotone) {
        // Plant-limited robustness check: even the uncompensated plant
        // overshoots ~1% at zeta = 0.731.
        EXPECT_LE(m.max_overshoot, 0.015);
      } else {
        EXPECT_LE(m.max_overshoot, 0.035);
      }
    }
  }
}

TEST(VerticalSpringCompensatorTest,
     ThreeStageWindowManagerAnimationGroupZeroLockoutsAcrossDisplayLinkPhases) {
  // Simulates WindowManager's 3-stage setter + AnimationGroup registration and
  // DisplayLink-sampled plant overshoot across 60/120 Hz DisplayLink periods
  // and 0..24 ms phase offsets.
  const std::vector<NamedCurve> curves = GetAllTestCurves();
  constexpr std::array<int, 2> kDisplayLinkHz = {60, 120};
  constexpr std::array<int64_t, 6> kPhaseOffsetsMs = {0, 3, 7, 12, 18, 24};

  for (const auto& curve : curves) {
    for (int dl_hz : kDisplayLinkHz) {
      const int64_t dl_period_ns = 1'000'000'000LL / dl_hz;
      for (int64_t phase_ms : kPhaseOffsetsMs) {
        const int64_t base_ns = 1'000'000'000LL;
        const int64_t tick_dt_ns = 1'000'000'000LL / 240;

        VerticalSpringCompensator comp;
        comp.Reset(0.0, 0.0, base_ns);

        SpringState dl_plant{.y = 0.0, .v = 0.0};
        double committed_old_target = kFixedFP1616Lsb;
        double latest_u = kFixedFP1616Lsb;
        double max_dl_overshoot = 0.0;
        int64_t next_dl_ns = base_ns + (phase_ms * 1'000'000LL) % dl_period_ns;
        int64_t prev_dl_ns = base_ns;

        auto step_dl_plant = [&](double dt_s, double target_u) {
          constexpr int kSubSteps = 8;
          const double sub_dt = dt_s / kSubSteps;
          for (int s = 0; s < kSubSteps; ++s) {
            dl_plant = StepSpringExact(dl_plant, target_u, sub_dt);
            max_dl_overshoot = std::max(max_dl_overshoot, dl_plant.y - 1.0);
          }
        };

        bool settled = false;
        for (int tick = 0; tick <= 120; ++tick) {
          const int64_t now_ns = base_ns + tick * tick_dt_ns;
          while (next_dl_ns <= now_ns) {
            const double dl_dt =
                static_cast<double>(next_dl_ns - prev_dl_ns) * 1e-9;
            if (dl_dt > 0.0) {
              step_dl_plant(dl_dt, latest_u);
            }
            committed_old_target = latest_u;
            prev_dl_ns = next_dl_ns;
            next_dl_ns += dl_period_ns;
          }

          const double elapsed_s =
              static_cast<double>(tick * tick_dt_ns) * 1e-9;
          const auto res = comp.StepAnimationTick(elapsed_s, 0.200, 0.0, 1.0,
                                                  curve.easing, now_ns);
          latest_u = res.command;
          if (res.settled) {
            settled = true;
            break;
          }
        }

        SCOPED_TRACE(curve.name + " DL=" + std::to_string(dl_hz) +
                     "Hz phase=" + std::to_string(phase_ms) + "ms");
        ASSERT_TRUE(settled);
        // WindowManager applies the end-of-frame latest_u retroactively across
        // each 8.33 ms (120 Hz) or 16.67 ms (60 Hz) DisplayLink interval,
        // introducing a half-frame phase lead on fast-rising curves. Bound
        // DisplayLink-sampled overshoot tightly by rate and curve class.
        const double dl_ov_limit = (dl_hz == 120)
                                       ? (curve.is_monotone ? 1.5e-4 : 8.0e-4)
                                       : (curve.is_monotone ? 3.0e-4 : 1.5e-3);
        EXPECT_LE(max_dl_overshoot, dl_ov_limit);

        // 3-stage AnimationGroup predicate: old_target was already committed
        // at 1.0 on a prior DisplayLink frame, AND live position is within 6K
        // Retina epsilon (or oldAnim != nil with unchanged target skips
        // AnimationGroup.add).
        const bool old_target_matches =
            std::abs(1.0 - committed_old_target) <= kRetina6KEpsilon;
        const bool live_pos_within_eps =
            std::abs(1.0 - dl_plant.y) <= kRetina6KEpsilon;
        EXPECT_TRUE(old_target_matches || live_pos_within_eps)
            << "old_target=" << committed_old_target
            << " dl_plant.y=" << dl_plant.y;
      }
    }
  }
}

TEST(VerticalSpringCompensatorTest, SlowDragNoTick1SnapAndNearOnePauseSafe) {
  VerticalSpringCompensator comp;
  const int64_t base_ns = 1'000'000'000LL;
  comp.Reset(0.0, 0.0, base_ns);

  // Slow drag at 0.2 swipes/s (dt = 8.333 ms -> r1 = 0.001667).
  const double u1 = comp.StepPhysicalDrag(0.001667, base_ns + 8'333'333LL);
  EXPECT_LT(u1, 0.01);

  // Near-1.0 pause: resting at r = 0.999 should not yank backward.
  comp.Reset(0.999, 0.0, base_ns);
  const double u_pause = comp.StepPhysicalDrag(0.999, base_ns + 16'666'667LL);
  EXPECT_GE(u_pause, 0.998);
}

TEST(VerticalSpringCompensatorTest, FastDragLinearReleaseLowOvershoot) {
  VerticalSpringCompensator comp;
  const int64_t base_ns = 1'000'000'000LL;
  comp.Reset(0.0, 0.0, base_ns);

  SpringState plant{.y = 0.0, .v = 0.0};
  double prev_u = kFixedFP1616Lsb;
  int64_t now_ns = base_ns;

  // Fast flick at 12 swipes/s up to r = 0.8 (120 Hz touch updates).
  constexpr int64_t kDragDtNs = 1'000'000'000LL / 120;
  constexpr double kDragDtSec = static_cast<double>(kDragDtNs) * 1e-9;
  double r = 0.0;
  while (r < 0.8) {
    now_ns += kDragDtNs;
    plant = StepSpringExact(plant, prev_u, kDragDtSec);
    r = std::min(0.8, r + 12.0 * kDragDtSec);
    prev_u = comp.StepPhysicalDrag(r, now_ns);
  }

  // Immediate linear release from estimated position to 1.0.
  const double start_r = std::clamp(comp.EstimateStateAt(now_ns).y, 0.0, 1.0);
  const double duration_s = std::max(0.01, (1.0 - start_r) * 0.200);
  const EasingFunction linear = MakeEasingFunctionLinear();

  constexpr int64_t kAnimDtNs = 1'000'000'000LL / 240;
  constexpr double kAnimDtSec = static_cast<double>(kAnimDtNs) * 1e-9;
  const int64_t anim_start_ns = now_ns;
  double u_dip = 0.0;
  bool settled = false;

  for (int tick = 0; tick <= 120; ++tick) {
    now_ns = anim_start_ns + tick * kAnimDtNs;
    if (tick > 0) {
      plant = StepSpringExact(plant, prev_u, kAnimDtSec);
    }
    const double elapsed_s = static_cast<double>(tick * kAnimDtNs) * 1e-9;
    const auto res = comp.StepAnimationTick(elapsed_s, duration_s, start_r, 1.0,
                                            linear, now_ns);
    if (tick > 0) {
      u_dip = std::max(u_dip, prev_u - res.command);
    }
    prev_u = res.command;
    if (res.settled) {
      settled = true;
      break;
    }
  }

  EXPECT_TRUE(settled);
  EXPECT_DOUBLE_EQ(u_dip, 0.0);
  EXPECT_LE(std::abs(1.0 - plant.y), 1.2e-4);
  EXPECT_LE(MaxCriticalReleaseOvershoot(plant, 1.0, +1, 0.15), 0.002);
  EXPECT_LT(MaxCriticalReleaseOvershoot(plant, 1.0, +1, 0.38), 0.005);
}

TEST(VerticalSpringCompensatorTest,
     MidFlightRegrabPreservesVelocityLeadAndLongPauseConverges) {
  // Verify on linear 200 ms (where headroom envelope does not clip lead) that
  // forward velocity lead survives re-grab (catching the r_prev_ = r_now bug).
  {
    VerticalSpringCompensator comp;
    const int64_t base_ns = 1'000'000'000LL;
    comp.Reset(0.0, 0.0, base_ns);

    const EasingFunction linear = MakeEasingFunctionLinear();
    constexpr int64_t kDtNs = 1'000'000'000LL / 240;
    int64_t now_ns = base_ns;
    for (int tick = 0; tick < 15; ++tick) {
      now_ns = base_ns + tick * kDtNs;
      (void)comp.StepAnimationTick(static_cast<double>(tick * kDtNs) * 1e-9,
                                   0.200, 0.0, 1.0, linear, now_ns);
    }

    const SpringState grab = comp.EstimateStateAt(now_ns);
    ASSERT_GT(grab.y, 0.15);
    ASSERT_LT(grab.y, 0.6);
    ASSERT_GT(grab.v, 2.0);

    now_ns += 4'000'000LL;
    const double u_drag = comp.StepPhysicalDrag(grab.y, now_ns);
    EXPECT_GE(u_drag,
              grab.y + 0.5 * kWindowManagerTrackingSpring.lag_time() * grab.v);
    EXPECT_LT(u_drag, 0.95);
  }

  // Verify on quint 200 ms (largest gap between r_now and state_.y) that
  // re-grab never pulls u_drag below visual y_grab, and that a 400 ms pause
  // converges smoothly to u_drag (no 50 ms dt clamp).
  {
    VerticalSpringCompensator comp;
    const int64_t base_ns = 1'000'000'000LL;
    comp.Reset(0.0, 0.0, base_ns);

    const EasingFunction quint = MakeEasingFunctionEaseOutQuintic();
    constexpr int64_t kDtNs = 1'000'000'000LL / 240;
    int64_t now_ns = base_ns;
    for (int tick = 0; tick < 12; ++tick) {
      now_ns = base_ns + tick * kDtNs;
      (void)comp.StepAnimationTick(static_cast<double>(tick * kDtNs) * 1e-9,
                                   0.200, 0.0, 1.0, quint, now_ns);
    }

    const SpringState grab = comp.EstimateStateAt(now_ns);
    ASSERT_GT(grab.y, 0.1);
    ASSERT_LT(grab.y, 0.9);
    ASSERT_GT(grab.v, 2.0);

    now_ns += 4'000'000LL;
    const double u_drag = comp.StepPhysicalDrag(grab.y, now_ns);
    const double expected_lead =
        std::min(0.5 * kWindowManagerTrackingSpring.lag_time() * grab.v,
                 0.2 * (1.0 - grab.y));
    EXPECT_GE(u_drag, grab.y + expected_lead);
    EXPECT_LT(u_drag, 0.95);

    const SpringState after_pause =
        comp.EstimateStateAt(now_ns + 400'000'000LL);
    EXPECT_NEAR(after_pause.y, u_drag, 1e-4);
    EXPECT_NEAR(after_pause.v, 0.0, 1e-3);
  }
}

TEST(VerticalSpringCompensatorTest,
     StartEqualsTargetZeroConvergesWithoutLockup) {
  VerticalSpringCompensator comp;
  const int64_t base_ns = 1'000'000'000LL;
  comp.Reset(0.2, 0.0, base_ns);

  const EasingFunction linear = MakeEasingFunctionLinear();
  constexpr int64_t kDtNs = 1'000'000'000LL / 240;
  double prev_u = QuantizeFP1616(0.2);
  bool settled = false;

  for (int tick = 0; tick <= 120; ++tick) {
    const int64_t now_ns = base_ns + tick * kDtNs;
    const double elapsed_s = static_cast<double>(tick * kDtNs) * 1e-9;
    const auto res =
        comp.StepAnimationTick(elapsed_s, 0.100, 0.0, 0.0, linear, now_ns);
    EXPECT_LE(res.command, prev_u + 1e-12);
    prev_u = res.command;
    if (res.settled) {
      settled = true;
      break;
    }
  }

  EXPECT_TRUE(settled);
  EXPECT_NEAR(prev_u, kFixedFP1616Lsb, 1e-12);
}

TEST(VerticalSpringCompensatorTest, MultiReversalStrictPerSegmentMonotonicity) {
  VerticalSpringCompensator comp;
  const int64_t base_ns = 1'000'000'000LL;
  comp.Reset(0.0, 0.0, base_ns);

  SpringState plant{.y = 0.0, .v = 0.0};
  double prev_u = kFixedFP1616Lsb;
  int64_t now_ns = base_ns;
  constexpr int64_t kDtNs = 1'000'000'000LL / 240;
  constexpr double kDtSec = static_cast<double>(kDtNs) * 1e-9;
  const EasingFunction quad = MakeEasingFunctionEaseOutQuadratic();

  struct Segment {
    double target;
    int ticks;
    bool settle_to_end;
  };
  const std::vector<Segment> segments = {
      {0.7, 25, false},
      {0.2, 25, false},
      {0.9, 25, false},
      {0.0, 120, true},
  };

  bool final_settled = false;
  double max_final_undershoot = 0.0;
  for (const auto& seg : segments) {
    const double start_r = comp.EstimateStateAt(now_ns).y;
    const int dir = (seg.target >= start_r) ? 1 : -1;
    const int64_t seg_start_ns = now_ns;
    for (int t = 0; t < seg.ticks; ++t) {
      now_ns = seg_start_ns + (t + 1) * kDtNs;
      plant = StepSpringExact(plant, prev_u, kDtSec);
      if (seg.settle_to_end) {
        max_final_undershoot = std::max(max_final_undershoot, -plant.y);
      }
      const double elapsed_s = static_cast<double>((t + 1) * kDtNs) * 1e-9;
      const auto res = comp.StepAnimationTick(elapsed_s, 0.150, start_r,
                                              seg.target, quad, now_ns);
      if (dir > 0) {
        EXPECT_GE(res.command, prev_u - 1e-12);
      } else {
        EXPECT_LE(res.command, prev_u + 1e-12);
      }
      prev_u = res.command;
      if (seg.settle_to_end && res.settled) {
        final_settled = true;
        break;
      }
    }
  }

  EXPECT_TRUE(final_settled);
  EXPECT_LE(max_final_undershoot, 1.0e-4);
  EXPECT_LE(std::abs(plant.y), 1.2e-4);
}

TEST(VerticalSpringCompensatorTest,
     ExactDwellBoundaryTimestampsAndNonEndpointNonSettle) {
  const EasingFunction linear = MakeEasingFunctionLinear();

  // Exact 24,999,999 ns vs 25,000,000 ns dwell boundary from already-settled
  // state at 1.0.
  {
    VerticalSpringCompensator comp;
    const int64_t base_ns = 1'000'000'000LL;
    comp.Reset(1.0, 0.0, base_ns);

    const auto r0 = comp.StepAnimationTick(0.200, 0.200, 0.0, 1.0, linear,
                                           base_ns + 1'000'000LL);
    EXPECT_DOUBLE_EQ(r0.command, 1.0);
    EXPECT_FALSE(r0.settled);

    const auto r_before = comp.StepAnimationTick(
        0.225, 0.200, 0.0, 1.0, linear, base_ns + 1'000'000LL + 24'999'999LL);
    EXPECT_FALSE(r_before.settled);

    const auto r_exact = comp.StepAnimationTick(
        0.225, 0.200, 0.0, 1.0, linear, base_ns + 1'000'000LL + 25'000'000LL);
    EXPECT_TRUE(r_exact.settled);

    // Regressing timestamp resets dwell and must not report settled.
    const auto r_regress = comp.StepAnimationTick(
        0.225, 0.200, 0.0, 1.0, linear, base_ns + 1'000'000LL + 10'000'000LL);
    EXPECT_FALSE(r_regress.settled);

    // Zero timestamp must never report settled.
    const auto r_zero =
        comp.StepAnimationTick(0.250, 0.200, 0.0, 1.0, linear, 0);
    EXPECT_FALSE(r_zero.settled);
  }

  // Interior 0.5 and multi-segment 2.0 / -1.0 targets must never settle.
  for (double non_endpoint : {0.5, 2.0, -1.0}) {
    VerticalSpringCompensator comp;
    const int64_t base_ns = 1'000'000'000LL;
    comp.Reset(std::clamp(non_endpoint, 0.0, 1.0), 0.0, base_ns);
    const auto r1 = comp.StepAnimationTick(0.200, 0.200, 0.0, non_endpoint,
                                           linear, base_ns + 1'000'000LL);
    EXPECT_FALSE(r1.settled) << "target=" << non_endpoint;
    const auto r2 = comp.StepAnimationTick(0.300, 0.200, 0.0, non_endpoint,
                                           linear, base_ns + 50'000'000LL);
    EXPECT_FALSE(r2.settled) << "target=" << non_endpoint;
  }
}

}  // namespace
}  // namespace fasterswiper
