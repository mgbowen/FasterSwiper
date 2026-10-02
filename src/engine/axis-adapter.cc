#include "src/engine/axis-adapter.h"

#include "src/cf-util.h"
#include "src/compatibility.h"
#include "src/engine/const.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/periodic-timer.h"

#include <absl/base/no_destructor.h>
#include <absl/log/log.h>
#include <absl/status/status_macros.h>
#include <thread>

namespace fasterswiper {

namespace {

constexpr int64_t kMissionControlPosition = 1 * kOneSwipeInNanoswipes;
constexpr int64_t kDesktopPosition = 0;
constexpr int64_t kAppExposePosition = -1 * kOneSwipeInNanoswipes;

const absl::NoDestructor<SpaceState> kAppExposeDummySpaceState([] {
  return SpaceState(WrapCFUnique(CFStringCreateWithCString(
                        nullptr, "dummy", kCFStringEncodingUTF8)),
                    /*spaces=*/{Space{.id = 0}, Space{.id = 1}},
                    /*index=*/0);
}());

absl::StatusOr<int64_t> GetCommittedPosition(const SpaceState &space_state) {
  const int64_t current_space_id = SLSManagedDisplayGetCurrentSpace(
      SLSMainConnectionID(), space_state.display_id().get());
  for (int i = 0; i < space_state.space_ids().size(); i++) {
    if (space_state.space_ids()[i] == current_space_id) {
      return i * kOneSwipeInNanoswipes;
    }
  }

  return absl::InternalError(
      absl::StrCat("System reports current space ID=", current_space_id,
                   " which is not among known space IDs [",
                   absl::StrJoin(space_state.space_ids(), ", "), "]"));
}

} // namespace

bool AxisAdapter::WaitForCommittedPositionChanged(
    int64_t original_position, absl::Duration deadline) const {
  // Wait for WindowServer to commit the space change.
  const int64_t start_time = UptimeInNanoseconds();
  const int64_t deadline_ns = start_time + absl::ToInt64Nanoseconds(deadline);
  while (UptimeInNanoseconds() < deadline_ns) {
    const int64_t new_committed_position = *committed_position();

    VLOG_EVERY_N_SEC(1, 0.1)
        << "WaitForPendingCommit: waiting for gesture "
           "commit, original_position="
        << original_position
        << ", new_committed_position=" << new_committed_position;

    if (original_position != new_committed_position) {
      const int64_t commit_latency_ns = UptimeInNanoseconds() - start_time;
      VLOG(1) << "Commit(): took " << absl::Nanoseconds(commit_latency_ns);
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
  return GetCommittedPosition(space_state_);
}

std::pair<int64_t, int64_t>
HorizontalAxisAdapter::position_soft_limits() const {
  return {0, static_cast<int64_t>(space_state_.count() - 1) *
                 kOneSwipeInNanoswipes};
}

VerticalAxisAdapter::VerticalAxisAdapter(SpaceState space_state)
    : space_state_(std::move(space_state)) {}

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
  const int64_t lower_limit =
      space_state_.current_space().is_desktop ? kAppExposePosition : 0;
  return {lower_limit, kMissionControlPosition};
}

AppExposeHorizontalAxisAdapter_MacOS26::AppExposeHorizontalAxisAdapter_MacOS26(
    SpaceState space_state)
    : space_state_(std::move(space_state)) {}

double AppExposeHorizontalAxisAdapter_MacOS26::NanoswipesToProgress(
    int64_t nanoswipes) const {
  return space_state_.SwipesToProgress(nanoswipes);
}

int64_t AppExposeHorizontalAxisAdapter_MacOS26::ProgressToNanoswipes(
    double progress) const {
  return space_state_.ProgressToSwipes(progress);
}

bool AppExposeHorizontalAxisAdapter_MacOS26::WaitForCommittedPositionChanged(
    int64_t /*original_position*/, absl::Duration /*deadline*/) const {
  return true;
}

absl::StatusOr<int64_t>
AppExposeHorizontalAxisAdapter_MacOS26::committed_position() const {
  return GetCommittedPosition(space_state_);
}

std::pair<int64_t, int64_t>
AppExposeHorizontalAxisAdapter_MacOS26::position_soft_limits() const {
  const int64_t current_space_position =
      space_state_.index() * kOneSwipeInNanoswipes;
  return {current_space_position, current_space_position};
}

double
AppExposeHorizontalAxisAdapter::NanoswipesToProgress(int64_t nanoswipes) const {
  return kAppExposeDummySpaceState->SwipesToProgress(nanoswipes);
}

int64_t
AppExposeHorizontalAxisAdapter::ProgressToNanoswipes(double progress) const {
  return kAppExposeDummySpaceState->ProgressToSwipes(progress);
}

bool AppExposeHorizontalAxisAdapter::WaitForCommittedPositionChanged(
    int64_t /*original_position*/, absl::Duration /*deadline*/) const {
  return true;
}

absl::StatusOr<int64_t>
AppExposeHorizontalAxisAdapter::committed_position() const {
  return 0;
}

std::pair<int64_t, int64_t>
AppExposeHorizontalAxisAdapter::position_soft_limits() const {
  return {-1 * kOneSwipeInNanoswipes, 1 * kOneSwipeInNanoswipes};
}

absl::StatusOr<std::unique_ptr<AxisAdapter>>
CreateAppExposeHorizontalAxisAdapter() {
  if (!IsMacOS27()) {
    ASSIGN_OR_RETURN(SpaceState space_state, LoadSpaceStateForActiveDisplay());
    return std::make_unique<AppExposeHorizontalAxisAdapter_MacOS26>(
        std::move(space_state));
  }

  return std::make_unique<AppExposeHorizontalAxisAdapter>();
}

} // namespace fasterswiper
