#include "src/space-switcher.h"

#include "src/cf-util.h"
#include "src/const.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include "src/space-state.h"

#include <cfloat>
#include <limits>
#include <optional>
#include <thread>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace fasterswiper {

namespace {

constexpr double kEpsilon = FLT_TRUE_MIN;

}

SpaceSwitcher::SpaceSwitcher(SpaceState space_state)
    : space_state_(std::move(space_state)),
      state_(States::Idle(space_state_.space_ids()[space_state_.index()])) {
  current_position_ = space_state_.index() * OneSwipeInNanoswipes;
}

SpaceSwitcher::~SpaceSwitcher() {
  if (const auto *active_state = std::get_if<States::Active>(&state_)) {
    SetPosition(active_state->origin_position());
  }
}

int64_t SpaceSwitcher::position() const {
  absl::MutexLock lock(mutex_);
  return current_position_;
}

std::pair<int64_t, int64_t> SpaceSwitcher::position_soft_limit() const {
  absl::MutexLock lock(mutex_);
  return unlocked_position_soft_limit();
}

std::pair<int64_t, int64_t>
SpaceSwitcher::unlocked_position_soft_limit() const {
  return {0, static_cast<int64_t>(space_state_.count() - 1) *
                 OneSwipeInNanoswipes};
}

std::string SpaceSwitcher::StateToString(const StateVariant &state) const {
  return std::visit([](const auto &state) { return absl::StrCat(state); },
                    state);
}

void SpaceSwitcher::SetState(StateVariant state) {
  VLOG(1) << "Setting state to " << StateToString(state);
  state_ = std::move(state);
}

namespace {

// Integer division that rounds toward negative infinity, unlike C++ integer
// division which truncates toward zero. b must be positive.
int64_t FloorDiv(int64_t a, int64_t b) {
  return a / b - (a % b != 0 && (a ^ b) < 0);
}

void PostGestureEvent(int phase, double progress,
                      std::optional<double> velocity = std::nullopt) {
  auto dock = WrapCFUnique(CGEventCreate(NULL));
  if (!dock)
    return;

  CGEventSetIntegerValueField(dock.get(), kCGSEventTypeField,
                              static_cast<int64_t>(kCGSEventDockControl));
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureHIDType,
                              kIOHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(dock.get(), kCGEventGesturePhase, phase);
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureSwipeMotion,
                              kCGGestureMotionHorizontal);
  CGEventSetIntegerValueField(dock.get(), kCGEventSourceUserData,
                              kSyntheticEventMagicNumber);
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeProgress,
                             progress);

  if (velocity.has_value()) {
    CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityX,
                               *velocity);
  }

  VLOG(1) << "Posting event to session: " << CFEventToDebugString(dock.get());
  CGEventPost(kCGSessionEventTap, dock.get());
}

} // namespace

int64_t SpaceSwitcher::GetNextBoundary(bool is_moving_right) const {
  const auto [soft_min, soft_max] = unlocked_position_soft_limit();
  int64_t next_boundary =
      is_moving_right
          ? (FloorDiv(current_position_, OneSwipeInNanoswipes) + 1) *
                OneSwipeInNanoswipes
          : FloorDiv(current_position_ - 1, OneSwipeInNanoswipes) *
                OneSwipeInNanoswipes;
  if (next_boundary < soft_min) {
    return is_moving_right ? soft_min : std::numeric_limits<int64_t>::min();
  } else if (next_boundary > soft_max) {
    return is_moving_right ? std::numeric_limits<int64_t>::max() : soft_max;
  }

  return next_boundary;
}

namespace {

size_t Sign(auto spaceship_operator_result) {
  if (spaceship_operator_result > 0) {
    return 1;
  }

  if (spaceship_operator_result < 0) {
    return -1;
  }

  return 0;
}

} // namespace

void SpaceSwitcher::SetPosition(int64_t new_position) {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN SetPosition(" << new_position
          << ") current_position_=" << current_position_;
  SetPositionLocked(new_position);
  VLOG(1) << "END SetPosition(" << new_position
          << ") current_position_=" << current_position_;
}

void SpaceSwitcher::SetPositionLocked(int64_t new_position) {
  if (new_position == current_position_) {
    return;
  }

  const auto [soft_min, soft_max] = unlocked_position_soft_limit();

  // Progress toward the target position, starting and committing gestures as
  // needed.
  while (current_position_ != new_position) {
    const bool is_moving_right = new_position > current_position_;

    WaitForPendingCommitLocked();

    if (auto idle_state = std::get_if<States::Idle>(&state_)) {
      PostGestureEvent(kGestureBegan, is_moving_right ? kEpsilon : -kEpsilon);
      SetState(States::Active(
          /*origin_position=*/current_position_, idle_state->space_id()));
    }

    const auto &gesture_active = std::get<States::Active>(state_);

    int64_t next_boundary =
        is_moving_right
            ? (FloorDiv(current_position_, OneSwipeInNanoswipes) + 1) *
                  OneSwipeInNanoswipes
            : FloorDiv(current_position_ - 1, OneSwipeInNanoswipes) *
                  OneSwipeInNanoswipes;
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
    const int64_t distance_from_origin =
        target_position - gesture_active.origin_position();
    const double progress_from_origin =
        space_state_.SwipesToProgress(distance_from_origin);

    const bool boundary_reached =
        (target_position % OneSwipeInNanoswipes) == 0 &&
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
          Sign(current_position_ <=> gesture_active.origin_position());
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

      bool should_wait_for_space_transition = false;
      if (origin_to_current_position_sign == 0) {
        // Instant switch to an adjacent space.
        PostGestureEvent(kGestureChanged,
                         kEpsilon * current_to_new_position_sign);
        PostGestureEvent(kGestureEnded, kEpsilon * current_to_new_position_sign,
                         2000 * current_to_new_position_sign);
        should_wait_for_space_transition = true;
      } else if (origin_to_current_position_sign ==
                 current_to_new_position_sign) {
        // Moving away from the gesture origin.
        const double transitory_progress =
            progress_from_origin - (kEpsilon * origin_to_current_position_sign);
        PostGestureEvent(kGestureChanged, transitory_progress);

        const double velocity = kEpsilon * current_to_new_position_sign;
        PostGestureEvent(kGestureEnded, transitory_progress, velocity);
        should_wait_for_space_transition = true;
      } else {
        // Moving towards the gesture origin.
        const double transitory_progress =
            progress_from_origin - (kEpsilon * current_to_new_position_sign);
        PostGestureEvent(kGestureChanged, transitory_progress);

        // Velocity is based on the direction a hand would be moving to cause
        // the space movement we're making, but if we're rubberbanding, we
        // reverse the initially calculated velocity because the actual space
        // movement will be the opposite of what the "hand" is doing, e.g. if
        // we're at the soft min and the hand is moving left, the space movement
        // will initially be left, but when the hand lets go, the space will
        // move right to go back to the soft min.
        const double velocity = (is_rubberbanding ? -kEpsilon : kEpsilon) *
                                current_to_new_position_sign;
        PostGestureEvent(kGestureCancelled, transitory_progress, velocity);
      }

      if (is_rubberbanding) {
        should_wait_for_space_transition = false;
      }

      SetState(States::PendingCommit(space_state_.display_id(),
                                     gesture_active.original_space_id(),
                                     should_wait_for_space_transition));
    } else {
      PostGestureEvent(kGestureChanged, progress_from_origin);
    }

    current_position_ = target_position;
  }
}

void SpaceSwitcher::WaitForPendingCommit() {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN WaitForPendingCommit() (external invocation)";
  WaitForPendingCommitLocked();
  VLOG(1) << "END WaitForPendingCommit() (external invocation)";
}

void SpaceSwitcher::WaitForPendingCommitLocked() {
  const auto *maybe_pending_commit =
      std::get_if<States::PendingCommit>(&state_);
  if (maybe_pending_commit == nullptr) {
    return;
  }

  const States::PendingCommit &pending_commit = *maybe_pending_commit;

  const int cid = SLSMainConnectionID();
  const CFSharedPtr<CFStringRef> display_id = pending_commit.display_id();
  int64_t new_space_id = 0;

  const int64_t start_time = UptimeInNanoseconds();
  const int64_t deadline =
      start_time + absl::ToInt64Nanoseconds(absl::Milliseconds(200));
  while (UptimeInNanoseconds() < deadline) {
    const bool is_done_animating =
        !SLSManagedDisplayIsAnimating(cid, display_id.get());
    new_space_id = SLSManagedDisplayGetCurrentSpace(cid, display_id.get());

    const bool space_id_changed =
        pending_commit.wait_for_space_transition()
            ? (new_space_id != pending_commit.original_space_id())
            : true;

    if (is_done_animating && space_id_changed) {
      const int64_t commit_latency_ns = UptimeInNanoseconds() - start_time;
      VLOG(1) << "WaitForPendingCommit: commit took " << commit_latency_ns / 1e6
              << "ms";
      break;
    }

    VLOG_EVERY_N_SEC(1, 0.1)
        << "WaitForPendingCommit: waiting for gesture commit "
           "(is_done_animating="
        << is_done_animating
        << ", original_space_id=" << pending_commit.original_space_id()
        << ", space_id_changed=" << space_id_changed << ")";

    std::this_thread::yield();
  }

  if (UptimeInNanoseconds() >= deadline) {
    LOG(ERROR) << "Waiting for pending commit exceeded deadline, bailing out";
  }

  VLOG(1) << "WaitForPendingCommit: done waiting, original_space_id="
          << pending_commit.original_space_id()
          << ", new_space_id=" << new_space_id;
  SetState(States::Idle{new_space_id});
}

} // namespace fasterswiper
