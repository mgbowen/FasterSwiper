#pragma once

#include "src/string-util.h"

#include <cstdint>
#include <optional>

#include <absl/strings/str_format.h>

namespace fasterswiper {

// A class that holds a position, but defers setting positions that align on a
// space boundary. This is primarily useful for avoiding emitting gesture change
// events directly on a boundary until the gesture is committed.
class DeferredPosition {
public:
  DeferredPosition() = default;
  explicit DeferredPosition(int64_t initial_position);

  [[nodiscard]] int64_t get() const { return effective_; }

  [[nodiscard]] int64_t deferred() const {
    return deferred_.value_or(effective_);
  }

  [[nodiscard]] bool has_deferred() const { return deferred_.has_value(); }

  void Set(int64_t new_position);
  void SetAndCommit(int64_t new_position);
  void CommitDeferred();

  explicit operator int64_t() const { return get(); }
  int64_t operator*() const { return get(); }

  auto operator<=>(int64_t rhs) const { return get() <=> rhs; }
  bool operator==(int64_t rhs) const { return get() == rhs; }

private:
  int64_t effective_ = 0;
  std::optional<int64_t> deferred_;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const DeferredPosition &pos) {
    absl::Format(&sink, "DeferredPosition{effective=%d, deferred_=%s}",
                 pos.effective_, OptionalToString(pos.deferred_));
  }
};

} // namespace fasterswiper
