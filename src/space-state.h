#pragma once

#include "src/cf-util.h"
#include "src/string-util.h"

#include <ApplicationServices/ApplicationServices.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_join.h>

namespace fasterswiper {

struct Space {
  int64_t id = 0;
  bool is_desktop = false;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Space& space) {
    absl::Format(&sink, "Space{id=%d, is_desktop=%s}", space.id,
                 space.is_desktop ? "true" : "false");
  }
};

class SpaceState {
public:
  SpaceState(CFUniquePtr<CFStringRef> display_id, std::vector<Space> spaces,
             CFIndex index);
  SpaceState(CFSharedPtr<CFStringRef> display_id, std::vector<Space> spaces,
             CFIndex index);

  SpaceState(const SpaceState& other) noexcept = default;
  SpaceState(SpaceState&&) = default;
  SpaceState& operator=(const SpaceState& other) noexcept = default;
  SpaceState& operator=(SpaceState&&) = default;

  ~SpaceState() = default;

  absl_nonnull CFSharedPtr<CFStringRef> display_id() const {
    return display_id_;
  }

  int64_t index() const { return index_; }

  const Space& current_space() const { return spaces_[index_]; }

  const std::vector<Space>& spaces() const { return spaces_; }

  std::vector<int64_t> space_ids() const;

  int64_t count() const { return static_cast<int64_t>(spaces_.size()); }

  [[nodiscard]] int64_t ProgressToSwipes(double progress) const;
  [[nodiscard]] double SwipesToProgress(int64_t nanoswipes) const;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const SpaceState& space_state) {
    absl::Format(
        &sink,
        "SpaceState{display_id=\"%s\", current_space=%v, count=%d, "
        "space_ids=[%s]}",
        StatusOrToString(StringFromCFStringRef(space_state.display_id().get())),
        space_state.current_space(), space_state.count(),
        absl::StrJoin(space_state.spaces(), ", ",
                      [](std::string* out, const Space& space) {
                        absl::StrAppend(out, space.id);
                      }));
  }

private:
  CFSharedPtr<CFStringRef> display_id_;
  std::vector<Space> spaces_;
  int64_t index_ = 0;
  double unit_factor_ = 0;
};

absl::StatusOr<SpaceState> LoadSpaceStateForActiveDisplay();

absl::StatusOr<std::vector<CFUniquePtr<CFStringRef>>> GetDisplaysUnderMouse();

}  // namespace fasterswiper
