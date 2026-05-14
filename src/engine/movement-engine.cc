#include "src/engine/movement-engine.h"

#include "src/engine/const.h"

#include "absl/log/log.h"
#include "src/event.h"

namespace fasterswiper {

namespace {

// Integer division that rounds toward negative infinity, unlike C++ integer
// division which truncates toward zero. b must be positive.
int64_t FloorDiv(int64_t a, int64_t b) {
  return a / b - (a % b != 0 && (a ^ b) < 0);
}

size_t Sign(auto spaceship_operator_result) {
  if (spaceship_operator_result > 0) {
    return 1;
  }

  if (spaceship_operator_result < 0) {
    return -1;
  }

  return 0;
}

void PostEvent(int phase, const AxisAdapter *absl_nonnull axis_adapter,
               double progress, std::optional<double> velocity) {
  CFUniquePtr<CGEventRef> event = CreateDockControlGestureEvent(
      phase, static_cast<int>(axis_adapter->movement_direction()), progress,
      velocity);
  VLOG(1) << "PostEvent(): event=" << CFEventToDebugString(event.get());

  CGEventPost(kCGSessionEventTap, event.get());
}

} // namespace

ContinuousMovementEngine::ContinuousMovementEngine(
    AxisAdapter *absl_nonnull axis_adapter)
    : axis_adapter_(axis_adapter) {
  Reset();
}

void ContinuousMovementEngine::SetPosition(int64_t new_position) {
  if (new_position == current_position_) {
    return;
  }

  latest_direction_ = (new_position - current_position_) > 0 ? 1 : -1;

  if (!gesture_started_) {
    PostEvent(kGestureBegan, kEpsilon * latest_direction_);
    gesture_started_ = true;
  }

  const int64_t remainder = std::abs(new_position % kOneSwipeInNanoswipes);
  const int64_t distance_to_space_threshold =
      std::min(remainder, kOneSwipeInNanoswipes - remainder);

  constexpr int64_t defer_abs_threshold = 1;
  if (distance_to_space_threshold <= defer_abs_threshold) {
    const int64_t threshold =
        (new_position >= 0 ? new_position + defer_abs_threshold
                           : new_position - defer_abs_threshold) /
        kOneSwipeInNanoswipes * kOneSwipeInNanoswipes;
    current_position_ = threshold - defer_abs_threshold * latest_direction_;
    deferred_position_ = new_position;
  } else {
    current_position_ = new_position;
    deferred_position_ = std::nullopt;
  }

  PostEvent(kGestureChanged, progress_from_origin());
}

void ContinuousMovementEngine::Commit() {
  VLOG(1) << "Commit(): origin_position_=" << origin_position_
          << ", current_position_=" << current_position_
          << ", deferred_position_=" << OptionalToString(deferred_position_)
          << ", latest_direction_=" << latest_direction_
          << ", distance_from_origin=" << distance_from_origin()
          << ", progress_from_origin=" << progress_from_origin();

  if (deferred_position_.has_value()) {
    current_position_ = *deferred_position_;
    deferred_position_ = std::nullopt;
  }

  const int64_t num_spaces =
      std::abs(distance_from_origin() / kOneSwipeInNanoswipes);

  if (distance_from_origin() == 0) {
    PostEvent(kGestureCancelled, kEpsilon * latest_direction_ * -1,
              kEpsilon * latest_direction_);
  } else {
    const double direction_from_origin_to_target =
        distance_from_origin() > 0 ? 1 : -1;

    PostEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
              kInstantSwitchVelocity * latest_direction_);

    for (int i = 0; i < num_spaces - 1; i++) {
      PostEvent(kGestureBegan, kEpsilon * direction_from_origin_to_target);
      // PostEvent(kGestureChanged, kEpsilon * direction_from_origin_to_target);
      PostEvent(kGestureEnded, kEpsilon * direction_from_origin_to_target,
                kInstantSwitchVelocity * latest_direction_);
    }

    (void)axis_adapter_->WaitForCommittedPositionChanged(
        origin_position_, absl::Milliseconds(200));
  }

  Reset();
}

int64_t ContinuousMovementEngine::distance_from_origin() const {
  return current_position_ - origin_position_;
}

double ContinuousMovementEngine::progress_from_origin() const {
  return axis_adapter_->NanoswipesToProgress(distance_from_origin());
}

void ContinuousMovementEngine::Reset() {
  gesture_started_ = false;
  origin_position_ = *axis_adapter_->committed_position();
  current_position_ = origin_position_;
  latest_direction_ = 0;
}

void ContinuousMovementEngine::PostEvent(int phase, double progress,
                                         std::optional<double> velocity) const {
  ::fasterswiper::PostEvent(phase, axis_adapter_, progress, velocity);
}

SegmentedMovementEngine::SegmentedMovementEngine(
    AxisAdapter *absl_nonnull axis_adapter)
    : axis_adapter_(axis_adapter) {
  Reset();
}

void SegmentedMovementEngine::SetPosition(int64_t new_position) {
  if (new_position == current_position_) {
    return;
  }

  const auto [soft_min, soft_max] = axis_adapter_->position_soft_limits();

  // Progress toward the target position, starting and committing gestures as
  // needed.
  while (current_position_ != new_position) {
    const bool is_moving_right = new_position > current_position_;

    if (!gesture_started_) {
      PostEvent(kGestureBegan, is_moving_right ? kEpsilon : -kEpsilon);
      gesture_started_ = true;
    }

    int64_t next_boundary =
        is_moving_right
            ? (FloorDiv(current_position_, kOneSwipeInNanoswipes) + 1) *
                  kOneSwipeInNanoswipes
            : FloorDiv(current_position_ - 1, kOneSwipeInNanoswipes) *
                  kOneSwipeInNanoswipes;
    if (next_boundary < soft_min) {
      next_boundary =
          is_moving_right ? soft_min : std::numeric_limits<int64_t>::min();
    } else if (next_boundary > soft_max) {
      next_boundary =
          is_moving_right ? std::numeric_limits<int64_t>::max() : soft_max;
    }

    const int64_t target_position =
        ((is_moving_right && new_position >= next_boundary) ||
         (!is_moving_right && new_position <= next_boundary))
            ? next_boundary
            : new_position;
    const int64_t distance_from_origin = target_position - origin_position_;
    const double progress_from_origin =
        axis_adapter_->NanoswipesToProgress(distance_from_origin);

    const bool boundary_reached =
        (target_position % kOneSwipeInNanoswipes) == 0 &&
        target_position >= soft_min && target_position <= soft_max;

    VLOG(1) << "Preparing to send gesture events, target_position="
            << target_position
            << ", progress_from_origin=" << progress_from_origin;

    if (boundary_reached) {
      // We've reached a boundary between spaces, commit the pending space
      // transition.
      //
      // How we commit the transition depends on the gesture origin position
      // and the target position we're moving towards. If we're moving away
      // from the origin, we end the gesture, but if we're moving back towards
      // the origin, we cancel it; in macOS' parlance, ending a gesture
      // indicates the user wants to move to an adjacent space, whereas
      // cancelling a gesture indicates they want to return to the space they
      // were at when they started the gesture.
      const int64_t origin_to_current_position_sign =
          Sign(current_position_ <=> origin_position_);
      const int64_t current_to_new_position_sign =
          Sign(new_position <=> current_position_);

      const bool is_rubberbanding =
          (current_position_ < soft_min && current_to_new_position_sign > 0) ||
          (current_position_ > soft_max && current_to_new_position_sign < 0);

      VLOG(1) << "Boundary reached, origin_to_current_position_sign="
              << origin_to_current_position_sign
              << ", current_to_new_position_sign="
              << current_to_new_position_sign
              << ", is_rubberbanding=" << is_rubberbanding;

      if (origin_to_current_position_sign == 0) {
        // Instant switch to an adjacent space.
        PostEvent(kGestureChanged, kEpsilon * current_to_new_position_sign);
        PostEvent(kGestureEnded, kEpsilon * current_to_new_position_sign,
                  2000 * current_to_new_position_sign);
      } else if (origin_to_current_position_sign ==
                 current_to_new_position_sign) {
        // Moving away from the gesture origin.
        const double transitory_progress = axis_adapter_->NanoswipesToProgress(
            (kOneSwipeInNanoswipes - 1) * current_to_new_position_sign);
        PostEvent(kGestureChanged, transitory_progress);

        const double velocity = kEpsilon * current_to_new_position_sign;
        PostEvent(kGestureEnded, kEpsilon * current_to_new_position_sign,
                  kInstantSwitchVelocity * current_to_new_position_sign);
      } else {
        // Moving towards the gesture origin.
        const double transitory_progress =
            progress_from_origin - (kEpsilon * current_to_new_position_sign);
        PostEvent(kGestureChanged, transitory_progress);

        // Velocity is based on the direction a hand would be moving to cause
        // the space movement we're making, but if we're rubberbanding, we
        // reverse the initially calculated velocity because the actual space
        // movement will be the opposite of what the "hand" is doing, e.g. if
        // we're at the soft min and the hand is moving left, the space movement
        // will initially be left, but when the hand lets go, the space will
        // move right to go back to the soft min.
        const double velocity = (is_rubberbanding ? -kEpsilon : kEpsilon) *
                                current_to_new_position_sign;
        PostEvent(kGestureCancelled, transitory_progress, velocity);
      }

      origin_position_ = current_position_;
      gesture_started_ = false;
    } else {
      PostEvent(kGestureChanged, progress_from_origin);
    }

    current_position_ = target_position;
  }
}

void SegmentedMovementEngine::Commit() {
  if (overall_origin_position_ - current_position_ != 0) {
    (void)axis_adapter_->WaitForCommittedPositionChanged(
        overall_origin_position_, absl::Milliseconds(200));
  }

  Reset();
}

void SegmentedMovementEngine::Reset() {
  gesture_started_ = false;
  overall_origin_position_ = *axis_adapter_->committed_position();
  origin_position_ = overall_origin_position_;
  current_position_ = overall_origin_position_;
}

int64_t SegmentedMovementEngine::GetNextBoundary(bool is_moving_right) const {
  const auto [soft_min, soft_max] = axis_adapter_->position_soft_limits();
  int64_t next_boundary =
      is_moving_right
          ? (FloorDiv(current_position_, kOneSwipeInNanoswipes) + 1) *
                kOneSwipeInNanoswipes
          : FloorDiv(current_position_ - 1, kOneSwipeInNanoswipes) *
                kOneSwipeInNanoswipes;
  if (next_boundary < soft_min) {
    return is_moving_right ? soft_min : std::numeric_limits<int64_t>::min();
  } else if (next_boundary > soft_max) {
    return is_moving_right ? std::numeric_limits<int64_t>::max() : soft_max;
  }

  return next_boundary;
}

void SegmentedMovementEngine::PostEvent(int phase, double progress,
                                        std::optional<double> velocity) const {
  ::fasterswiper::PostEvent(phase, axis_adapter_, progress, velocity);
}

} // namespace fasterswiper
