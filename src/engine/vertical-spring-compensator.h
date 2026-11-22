#pragma once

#include "src/easing.h"
#include "src/engine/spring-math.h"
#include "src/fp1616.h"

#include <cstdint>

namespace fasterswiper {

// Closed-form 1-DOF monotonic lookahead + causal physical-drag pre-compensator
// for macOS 27 WindowManager.app's vertical Exposé tracking spring
// Spring(dampingRatio: 0.86, response: 0.15).
class VerticalSpringCompensator {
public:
  struct AnimationTickResult {
    // Q16.16-quantized wire command in [kFixedFP1616Lsb, 1.0].
    double command = kFixedFP1616Lsb;
    // True once the final endpoint segment has dwelled at its wire target and
    // settled within 6K Retina tolerance so .ended triggers 0 ms completion.
    bool settled = false;
  };

  VerticalSpringCompensator() = default;

  void Reset(double y = 0.0, double v = 0.0, int64_t now_ns = 0);

  [[nodiscard]] SpringState EstimateStateAt(int64_t now_ns) const;

  [[nodiscard]] double StepPhysicalDrag(double r_now, int64_t now_ns);

  [[nodiscard]] AnimationTickResult StepAnimationTick(
      double elapsed_s, double duration_s, double start_r, double target_r,
      const EasingFunction& easing, int64_t now_ns);

private:
  void AdvanceObserverTo(int64_t now_ns);
  void UpdateCommandDwell(double target_r, double target_cmd, int64_t now_ns);
  [[nodiscard]] bool HasCommandDwellAtTarget(double target_r, double target_cmd,
                                             int64_t now_ns) const;
  [[nodiscard]] bool IsSettledAtTarget(double target_r, double target_cmd,
                                       double r_now, double elapsed_s,
                                       double duration_s, int64_t now_ns) const;

  SpringState state_;
  double u_prev_ = kFixedFP1616Lsb;
  double r_prev_ = 0.0;
  double vr_ema_ = 0.0;
  int64_t last_update_ns_ = 0;
  bool has_time_ = false;
  bool has_u_target_dwell_ = false;
  double u_dwell_target_ = 0.0;
  int64_t u_dwell_start_ns_ = 0;
};

}  // namespace fasterswiper
