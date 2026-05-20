#include "src/engine/deferred-position.h"

#include "src/engine/const.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace fasterswiper {

namespace {

constexpr int64_t kDeferAbsThreshold = 1;

} // namespace

DeferredPosition::DeferredPosition(int64_t initial_position)
    : effective_(initial_position) {}

void DeferredPosition::Set(int64_t new_position) {
  if (effective_ == new_position) {
    return;
  }

  const int64_t remainder = std::abs(new_position % kOneSwipeInNanoswipes);
  const int64_t distance_to_space_threshold =
      std::min(remainder, kOneSwipeInNanoswipes - remainder);

  if (distance_to_space_threshold <= kDeferAbsThreshold) {
    const int64_t direction = new_position > effective_ ? 1 : -1;
    const int64_t threshold =
        (new_position >= 0 ? new_position + kDeferAbsThreshold
                           : new_position - kDeferAbsThreshold) /
        kOneSwipeInNanoswipes * kOneSwipeInNanoswipes;
    effective_ = threshold - (kDeferAbsThreshold * direction);
    deferred_ = new_position;
  } else {
    effective_ = new_position;
    deferred_ = std::nullopt;
  }
}

void DeferredPosition::SetAndCommit(int64_t new_position) {
  Set(new_position);
  CommitDeferred();
}

void DeferredPosition::CommitDeferred() {
  if (deferred_.has_value()) {
    effective_ = *deferred_;
    deferred_ = std::nullopt;
  }
}

} // namespace fasterswiper
