#pragma once

#include "src/cf-util.h"

#include <ApplicationServices/ApplicationServices.h>

#include "absl/status/statusor.h"

namespace fasterswiper {

class SpaceState {
public:
  SpaceState(CFUniquePtr<CFStringRef> display_id, CFIndex index, CFIndex count);
  SpaceState(CFSharedPtr<CFStringRef> display_id, CFIndex index, CFIndex count);

  absl_nonnull CFStringRef display_id() const { return display_id_.get(); }
  CFIndex index() const { return index_; }
  CFIndex count() const { return count_; }

  bool operator==(const SpaceState &other) const {
    return index_ == other.index_ && count_ == other.count_;
  }

  bool operator!=(const SpaceState &other) const { return !(*this == other); }

  [[nodiscard]] int64_t ProgressToSwipes(double progress) const;
  [[nodiscard]] double SwipesToProgress(int64_t nanoswipes) const;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const SpaceState &space_state) {
    absl::Format(&sink, "SpaceState{index=%d, count=%d}", space_state.index_,
                 space_state.count_);
  }

private:
  CFSharedPtr<CFStringRef> display_id_;
  CFIndex index_;
  CFIndex count_;
  double unit_factor_;
};

absl::StatusOr<SpaceState> LoadSpaceStateForActiveDisplay();

absl::StatusOr<std::vector<CFUniquePtr<CFStringRef>>> GetDisplaysUnderMouse();

} // namespace fasterswiper
