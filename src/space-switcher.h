#pragma once

#include "src/space-state.h"
#include "src/string-util.h"

#include <variant>

namespace fasterswiper {

class SpaceSwitcher {
public:
  explicit SpaceSwitcher(SpaceState space_state);
  ~SpaceSwitcher();

  // Non-copyable, non-movable.
  SpaceSwitcher(const SpaceSwitcher &) = delete;
  SpaceSwitcher &operator=(const SpaceSwitcher &) = delete;
  SpaceSwitcher(SpaceSwitcher &&) = delete;
  SpaceSwitcher &operator=(SpaceSwitcher &&) = delete;

  [[nodiscard]] const SpaceState &space_state() const { return space_state_; }

  [[nodiscard]] int64_t position() const ABSL_LOCKS_EXCLUDED(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> position_soft_limit() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetPosition(int64_t nanoswipes) ABSL_LOCKS_EXCLUDED(mutex_);

private:
  const SpaceState space_state_;

  mutable absl::Mutex mutex_;
  int64_t current_position_ ABSL_GUARDED_BY(mutex_) = 0;

  struct States {
    class Idle {
    public:
      explicit Idle(int64_t space_id) : space_id_(space_id) {}

      int64_t space_id() const { return space_id_; }

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Idle &) {
        sink.Append("Idle()");
      }

    private:
      int64_t space_id_ = 0;
    };

    class Active {
    public:
      explicit Active(int64_t origin_position, int64_t original_space_id)
          : origin_position_(origin_position),
            original_space_id_((original_space_id)) {}

      int64_t origin_position() const { return origin_position_; }

      int64_t original_space_id() const { return original_space_id_; }

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Active &state) {
        absl::Format(&sink, "Active(origin_position=%d, original_space_id=%d)",
                     state.origin_position(), state.original_space_id());
      }

    private:
      int64_t origin_position_ = 0;
      int64_t original_space_id_ = 0;
    };

    class PendingCommit {
    public:
      PendingCommit(CFSharedPtr<CFStringRef> display_id,
                    int64_t original_space_id, bool wait_for_space_transition)
          : display_id_(std::move(display_id)),
            original_space_id_(original_space_id),
            wait_for_space_transition_(wait_for_space_transition) {}

      const CFSharedPtr<CFStringRef> display_id() const { return display_id_; }

      int64_t original_space_id() const { return original_space_id_; }

      bool wait_for_space_transition() const {
        return wait_for_space_transition_;
      }

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const PendingCommit &state) {
        absl::Format(
            &sink,
            "PendingCommit(display_id=\"%s\", original_space_id=%d, "
            "wait_for_space_transition=%s)",
            StatusOrToString(StringFromCFStringRef(state.display_id().get())),
            state.original_space_id(),
            state.wait_for_space_transition() ? "true" : "false");
      }

    private:
      CFSharedPtr<CFStringRef> display_id_;
      int64_t original_space_id_;
      bool wait_for_space_transition_;
    };
  };

  using StateVariant =
      std::variant<States::Idle, States::Active, States::PendingCommit>;
  StateVariant state_ ABSL_GUARDED_BY(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> unlocked_position_soft_limit() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetState(StateVariant state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  std::string StateToString(const StateVariant &state) const;

  int64_t GetNextBoundary(bool moving_right) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetPositionLocked(int64_t nanoswipes)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void WaitForPendingCommit() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

} // namespace fasterswiper
