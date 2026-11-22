#include "src/engine/vertical-spring-compensator.h"

#include "src/engine/spring-math.h"
#include "src/fp1616.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fasterswiper {

namespace {

// Exact Q16.16 wire value of kEpsilon (1/65536) sent on kGestureChanged.
constexpr double kWireMinProgress = kFixedFP1616Lsb;

// Max allowable mid-stroke lead ahead of the reference curve during tracking.
constexpr double kLeadTolerance = 0.015;

// Distance-tapered overshoot safety buffer during animated tracking.
constexpr double kTrackingSafetyMargin = 0.0015;

// Causal EMA time constant for physical finger velocity estimation (15 ms).
constexpr double kDragVelTimeConstantSec = 0.015;

// Symmetric physical-drag command headroom corridor [0.75 r, r + 0.25(1 - r)].
constexpr double kDragHeadroomAlpha = 0.25;

// Gated bidirectional tracking safety margin during physical dragging.
constexpr double kDragSafetyMargin = 0.002;

// Max analytical peak overshoot allowing T1_Strict_6K early u = target lock-in
// (<= 0.3 px on 6K Retina).
constexpr double kLockInMaxOvershoot = 1.0e-4;

// Strict 6K 2x Retina position settle tolerance (< 0.5 / 3008 = 1.662e-4).
constexpr double kSettledPositionTol = 1.2e-4;

// Low residual velocity bound at .ended to prevent mismatch overshoot.
constexpr double kSettledVelocityTol = 0.04;

// Reference curve completion tolerance for early-settling curves (e.g. quint).
constexpr double kSettledReferenceTol = 0.005;

// 1.5x 60 Hz DisplayLink frame dwell guaranteeing old_target commit prior to
// .ended so WindowManager short-circuits AnimationGroup in 0 ms.
constexpr int64_t kSettledCommandDwellNs = 25'000'000;

double EvalCurve(const EasingFunction& easing, double t_s, double duration_s,
                 double start_r, double target_r) {
  if (duration_s <= 0.0 || t_s >= duration_s) {
    return target_r;
  }
  if (t_s <= 0.0) {
    return start_r;
  }
  const double s = std::clamp(t_s / duration_s, 0.0, 1.0);
  return start_r + (target_r - start_r) * easing(s);
}

bool IsEndpointTarget(double target_r) {
  return std::abs(target_r - 1.0) <= 1e-9 || std::abs(target_r) <= 1e-9;
}

int ComputeAnimationDirection(double start_r, double target_r,
                              double target_cmd, double y, double u_prev) {
  if (target_r > start_r) {
    return 1;
  }
  if (target_r < start_r) {
    return -1;
  }
  if (std::abs(target_cmd - y) > 1e-9) {
    return (target_cmd > y) ? 1 : -1;
  }
  return (target_cmd >= u_prev) ? 1 : -1;
}

}  // namespace

void VerticalSpringCompensator::Reset(double y, double v, int64_t now_ns) {
  state_ = SpringState{.y = y, .v = v};
  u_prev_ = std::clamp(QuantizeFP1616(std::clamp(y, kWireMinProgress, 1.0)),
                       kWireMinProgress, 1.0);
  r_prev_ = std::clamp(y, 0.0, 1.0);
  vr_ema_ = v;
  last_update_ns_ = now_ns;
  has_time_ = now_ns > 0;
  has_u_target_dwell_ = false;
  u_dwell_target_ = 0.0;
  u_dwell_start_ns_ = 0;
}

SpringState VerticalSpringCompensator::EstimateStateAt(int64_t now_ns) const {
  if (!has_time_ || now_ns <= last_update_ns_) {
    return state_;
  }
  const double dt =
      std::max(0.0, static_cast<double>(now_ns - last_update_ns_) * 1e-9);
  return StepSpringExact(state_, u_prev_, dt, kWindowManagerTrackingSpring);
}

void VerticalSpringCompensator::AdvanceObserverTo(int64_t now_ns) {
  if (!has_time_) {
    last_update_ns_ = now_ns;
    has_time_ = now_ns > 0;
    return;
  }
  if (now_ns > last_update_ns_) {
    const double dt =
        std::max(0.0, static_cast<double>(now_ns - last_update_ns_) * 1e-9);
    state_ = StepSpringExact(state_, u_prev_, dt, kWindowManagerTrackingSpring);
    last_update_ns_ = now_ns;
  }
}

void VerticalSpringCompensator::UpdateCommandDwell(double target_r,
                                                   double target_cmd,
                                                   int64_t now_ns) {
  if (!IsEndpointTarget(target_r) || !has_time_ || now_ns <= 0 ||
      std::abs(target_cmd - u_prev_) > 0.5 * kFixedFP1616Lsb) {
    has_u_target_dwell_ = false;
    u_dwell_target_ = 0.0;
    u_dwell_start_ns_ = 0;
    return;
  }
  if (!has_u_target_dwell_ || std::abs(u_dwell_target_ - target_cmd) > 1e-9 ||
      now_ns < u_dwell_start_ns_) {
    has_u_target_dwell_ = true;
    u_dwell_target_ = target_cmd;
    u_dwell_start_ns_ = now_ns;
  }
}

bool VerticalSpringCompensator::HasCommandDwellAtTarget(double target_r,
                                                        double target_cmd,
                                                        int64_t now_ns) const {
  if (!IsEndpointTarget(target_r) || !has_time_ || now_ns <= 0 ||
      !has_u_target_dwell_ || std::abs(u_dwell_target_ - target_cmd) > 1e-9) {
    return false;
  }
  if (now_ns < u_dwell_start_ns_ ||
      (now_ns - u_dwell_start_ns_) < kSettledCommandDwellNs) {
    return false;
  }
  return std::abs(target_cmd - u_prev_) <= 0.5 * kFixedFP1616Lsb;
}

bool VerticalSpringCompensator::IsSettledAtTarget(
    double target_r, double target_cmd, double r_now, double elapsed_s,
    double duration_s, int64_t now_ns) const {
  if (!HasCommandDwellAtTarget(target_r, target_cmd, now_ns)) {
    return false;
  }
  const SpringState st =
      (now_ns > last_update_ns_) ? EstimateStateAt(now_ns) : state_;
  const bool ref_ok = elapsed_s >= duration_s ||
                      std::abs(target_r - r_now) <= kSettledReferenceTol;
  return ref_ok && std::abs(target_r - st.y) <= kSettledPositionTol &&
         std::abs(st.v) <= kSettledVelocityTol;
}

double VerticalSpringCompensator::StepPhysicalDrag(double r_now,
                                                   int64_t now_ns) {
  const double r_clamped = std::clamp(r_now, 0.0, 1.0);
  double dt = 0.0;
  if (has_time_ && now_ns > last_update_ns_) {
    dt = std::max(0.0, static_cast<double>(now_ns - last_update_ns_) * 1e-9);
  }
  AdvanceObserverTo(now_ns);

  if (dt > 1e-4) {
    const double v_inst = (r_clamped - r_prev_) / dt;
    const double alpha = 1.0 - std::exp(-dt / kDragVelTimeConstantSec);
    vr_ema_ = (1.0 - alpha) * vr_ema_ + alpha * v_inst;
  }
  r_prev_ = r_clamped;

  const double u_raw =
      r_clamped + kWindowManagerTrackingSpring.lag_time() * vr_ema_;
  const double u_hi_env = r_clamped + kDragHeadroomAlpha * (1.0 - r_clamped);
  const double u_lo_env = r_clamped * (1.0 - kDragHeadroomAlpha);

  const double cap_hi =
      (state_.y < 0.998 || state_.v > 1e-4)
          ? ComputeTrackingSafetyCap(state_, 1.0, +1, kDragSafetyMargin,
                                     kWireMinProgress)
          : 1.0;
  const double cap_lo =
      (state_.y > 0.002 || state_.v < -1e-4)
          ? ComputeTrackingSafetyCap(state_, kWireMinProgress, -1,
                                     kDragSafetyMargin, kWireMinProgress)
          : kWireMinProgress;

  double lo = std::max(std::min(u_lo_env, cap_hi), cap_lo);
  const double hi = std::min(u_hi_env, cap_hi);
  if (lo > hi) {
    lo = hi;
  }

  const double u_clamped = std::clamp(u_raw, lo, hi);
  const double u_quant =
      std::clamp(QuantizeFP1616(std::clamp(u_clamped, kWireMinProgress, 1.0)),
                 kWireMinProgress, 1.0);
  u_prev_ = u_quant;
  has_u_target_dwell_ = false;
  u_dwell_target_ = 0.0;
  u_dwell_start_ns_ = 0;
  return u_quant;
}

VerticalSpringCompensator::AnimationTickResult
VerticalSpringCompensator::StepAnimationTick(double elapsed_s,
                                             double duration_s, double start_r,
                                             double target_r,
                                             const EasingFunction& easing,
                                             int64_t now_ns) {
  AdvanceObserverTo(now_ns);

  const double target_cmd = std::clamp(target_r, kWireMinProgress, 1.0);
  if (duration_s <= 0.0) {
    r_prev_ = std::clamp(target_r, 0.0, 1.0);
    vr_ema_ = 0.0;
    const double u_end =
        std::clamp(QuantizeFP1616(target_cmd), kWireMinProgress, 1.0);
    u_prev_ = u_end;
    UpdateCommandDwell(target_r, target_cmd, now_ns);
    return AnimationTickResult{
        .command = u_end,
        .settled =
            IsSettledAtTarget(target_r, target_cmd, target_r, 0.0, 0.0, now_ns),
    };
  }

  const double clamped_elapsed = std::max(0.0, elapsed_s);
  const double r_now =
      EvalCurve(easing, clamped_elapsed, duration_s, start_r, target_r);
  // Track actual visual observer position so a mid-flight re-grab at state_.y
  // never injects an artificial negative velocity spike.
  r_prev_ = std::clamp(state_.y, 0.0, 1.0);
  vr_ema_ = state_.v;

  const double upc = std::clamp(u_prev_, kWireMinProgress, 1.0);
  const int dir =
      ComputeAnimationDirection(start_r, target_r, target_cmd, state_.y, upc);

  // T1_Strict_6K early u = target lock-in: once on a final endpoint segment
  // (target is 0.0 or 1.0) and the reference curve is complete, lock u to
  // target_cmd whenever holding it keeps global [0, infinity) analytical peak
  // overshoot within kLockInMaxOvershoot (1.0e-4) and preserves monotonicity.
  const bool ref_complete = clamped_elapsed >= duration_s ||
                            std::abs(target_r - r_now) <= kSettledReferenceTol;
  const bool lock_preserves_monotonicity =
      (dir > 0) ? (target_cmd >= upc) : (target_cmd <= upc);
  if (IsEndpointTarget(target_r) && ref_complete &&
      lock_preserves_monotonicity &&
      ComputeAnalyticalPeakOvershoot(state_, target_cmd, dir) <=
          kLockInMaxOvershoot) {
    const double u_locked =
        std::clamp(QuantizeFP1616(target_cmd), kWireMinProgress, 1.0);
    u_prev_ = u_locked;
    UpdateCommandDwell(target_r, target_cmd, now_ns);
    return AnimationTickResult{
        .command = u_locked,
        .settled = IsSettledAtTarget(target_r, target_cmd, r_now,
                                     clamped_elapsed, duration_s, now_ns),
    };
  }

  // 1-DOF closed-form scalar lookahead least squares + per-sample lead cap
  // using precomputed linear superposition rows.
  double num = 0.0;
  double den = 0.0;
  double u_lead_cap = target_cmd;
  for (const PrecomputedStepRow& row : GetLookaheadStepTable()) {
    const double r_f = std::clamp(EvalCurve(easing, clamped_elapsed + row.tau,
                                            duration_s, start_r, target_r),
                                  0.0, 1.0);
    const double y_free = row.a_y * state_.y + row.a_v * state_.v;
    num += row.y_step * (r_f - y_free);
    den += row.y_step * row.y_step;
    if (row.y_step > 1e-6) {
      const double cand = (dir > 0)
                              ? (r_f + kLeadTolerance - y_free) / row.y_step
                              : (r_f - kLeadTolerance - y_free) / row.y_step;
      u_lead_cap =
          (dir > 0) ? std::min(u_lead_cap, cand) : std::max(u_lead_cap, cand);
    }
  }

  const double u_star = (den > 1e-12) ? (num / den) : target_cmd;
  double u_cap = ComputeTrackingSafetyCap(
      state_, target_cmd, dir, kTrackingSafetyMargin, kWireMinProgress);
  u_cap = (dir > 0) ? std::min(u_cap, u_lead_cap) : std::max(u_cap, u_lead_cap);

  const double lo = (dir > 0) ? upc : std::min(upc, u_cap);
  const double hi = (dir > 0) ? std::max(upc, u_cap) : upc;

  double u_quant = std::clamp(QuantizeFP1616(std::clamp(u_star, lo, hi)),
                              kWireMinProgress, 1.0);
  if (dir > 0) {
    u_quant = std::max(lo, u_quant);
  } else {
    if (u_quant < lo) {
      u_quant = u_quant + kFixedFP1616Lsb;
    }
    u_quant = std::min(hi, u_quant);
  }
  u_quant = std::clamp(u_quant, kWireMinProgress, 1.0);
  u_prev_ = u_quant;
  UpdateCommandDwell(target_r, target_cmd, now_ns);
  return AnimationTickResult{
      .command = u_quant,
      .settled = IsSettledAtTarget(target_r, target_cmd, r_now, clamped_elapsed,
                                   duration_s, now_ns),
  };
}

}  // namespace fasterswiper
