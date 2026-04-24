#pragma once

#include "src/cf-util.h"
#include "src/string-util.h"

#include <ApplicationServices/ApplicationServices.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"

namespace fasterswiper {

class SpaceState {
public:
  SpaceState(CFUniquePtr<CFStringRef> display_id,
             std::vector<int64_t> space_ids, CFIndex index);
  SpaceState(CFSharedPtr<CFStringRef> display_id,
             std::vector<int64_t> space_ids, CFIndex index);

  absl_nonnull CFSharedPtr<CFStringRef> display_id() const {
    return display_id_;
  }

  CFIndex index() const { return index_; }

  const std::vector<int64_t> space_ids() const { return space_ids_; }

  CFIndex count() const { return space_ids_.size(); }

  [[nodiscard]] int64_t ProgressToSwipes(double progress) const;
  [[nodiscard]] double SwipesToProgress(int64_t nanoswipes) const;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const SpaceState &space_state) {
    absl::Format(
        &sink,
        "SpaceState{display_id=\"%s\", index=%d, count=%d, space_ids=[%s]}",
        StatusOrToString(StringFromCFStringRef(space_state.display_id().get())),
        space_state.index(), space_state.count(),
        absl::StrJoin(space_state.space_ids(), ", "));
  }

private:
  CFSharedPtr<CFStringRef> display_id_;
  std::vector<int64_t> space_ids_;
  CFIndex index_;
  double unit_factor_;
};

absl::StatusOr<SpaceState> LoadSpaceStateForActiveDisplay();

absl::StatusOr<std::vector<CFUniquePtr<CFStringRef>>> GetDisplaysUnderMouse();

} // namespace fasterswiper
