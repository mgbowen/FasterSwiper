#include "src/engine/axis-adapter.h"

#include "src/engine/const.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/periodic-timer.h"

#include <absl/log/log.h>
#include <absl/status/status_macros.h>
#include <thread>

namespace fasterswiper {

namespace {

constexpr int64_t kMissionControlPosition = 1 * kOneSwipeInNanoswipes;
constexpr int64_t kDesktopPosition = 0;
constexpr int64_t kAppExposePosition = -1 * kOneSwipeInNanoswipes;

} // namespace

bool AxisAdapter::WaitForCommittedPositionChanged(
    int64_t original_position, absl::Duration deadline) const {
  // Wait for WindowServer to commit the space change.
  const int64_t start_time = UptimeInNanoseconds();
  const int64_t deadline_ns = start_time + absl::ToInt64Nanoseconds(deadline);
  while (UptimeInNanoseconds() < deadline_ns) {
    const int64_t new_committed_position = *committed_position();

    VLOG_EVERY_N_SEC(1, 0.1) << "WaitForPendingCommit: waiting for gesture "
                                "commit, new_committed_position="
                             << new_committed_position;

    if (original_position != new_committed_position) {
      const int64_t commit_latency_ns = UptimeInNanoseconds() - start_time;
      VLOG(1) << "Commit(): took "
              << absl::Nanoseconds(commit_latency_ns);
      break;
    }

    std::this_thread::yield();
  }

  if (UptimeInNanoseconds() >= deadline_ns) {
    LOG(ERROR) << "Waiting for pending commit exceeded deadline, bailing out";
    return false;
  }

  return true;
}

HorizontalAxisAdapter::HorizontalAxisAdapter(SpaceState space_state)
    : space_state_(std::move(space_state)) {}

double HorizontalAxisAdapter::NanoswipesToProgress(int64_t nanoswipes) const {
  return space_state_.SwipesToProgress(nanoswipes);
}

int64_t HorizontalAxisAdapter::ProgressToNanoswipes(double progress) const {
  return space_state_.ProgressToSwipes(progress);
}

absl::StatusOr<int64_t> HorizontalAxisAdapter::committed_position() const {
  const int64_t current_space_id = SLSManagedDisplayGetCurrentSpace(
      SLSMainConnectionID(), space_state_.display_id().get());
  for (int i = 0; i < space_state_.space_ids().size(); i++) {
    if (space_state_.space_ids()[i] == current_space_id) {
      return i * kOneSwipeInNanoswipes;
    }
  }

  return absl::InternalError(
      absl::StrCat("System reports current space ID=", current_space_id,
                   " which is not among known space IDs [",
                   absl::StrJoin(space_state_.space_ids(), ", "), "]"));
}

std::pair<int64_t, int64_t>
HorizontalAxisAdapter::position_soft_limits() const {
  const absl::StatusOr<ActiveMultitaskingWindow> maybe_active_window =
      GetActiveMultitaskingWindow();

  if (maybe_active_window.ok() &&
      *maybe_active_window == ActiveMultitaskingWindow::kAppExpose) {
    const int64_t current_space_position =
        space_state_.index() * kOneSwipeInNanoswipes;
    return {current_space_position, current_space_position};
  }

  return {0, static_cast<int64_t>(space_state_.count() - 1) *
                 kOneSwipeInNanoswipes};
}

double VerticalAxisAdapter::NanoswipesToProgress(int64_t position) const {
  return static_cast<double>(position) / kOneSwipeInNanoswipes;
}

int64_t VerticalAxisAdapter::ProgressToNanoswipes(double progress) const {
  return static_cast<int64_t>(progress * kOneSwipeInNanoswipes);
}

absl::StatusOr<int64_t> VerticalAxisAdapter::committed_position() const {
  ASSIGN_OR_RETURN(const ActiveMultitaskingWindow active_window,
                   GetActiveMultitaskingWindow());
  switch (active_window) {
    using enum ActiveMultitaskingWindow;
  case kMissionControl:
    return kMissionControlPosition;
  case kDesktop:
    return kDesktopPosition;
  case kAppExpose:
    return kAppExposePosition;
  }

  return absl::InternalError(
      absl::StrCat("GetActiveMultitaskingWindow returned unknown enum value ",
                   active_window));
}

std::pair<int64_t, int64_t> VerticalAxisAdapter::position_soft_limits() const {
  return {kAppExposePosition, kMissionControlPosition};
}

} // namespace fasterswiper
