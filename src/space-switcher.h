#pragma once

#include "src/space-state.h"
#include "src/string-util.h"

#include <variant>

namespace fasterswiper {

struct EventSidecar {
  int64_t intended_position = 0;
  std::unique_ptr<SpaceState> space_state;
};

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

  struct SetPositionOptions {
    // If true and `new_position` is on a space boundary,
    bool wait_for_space_transition = true;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const SetPositionOptions &options) {
      absl::Format(&sink, "SetPositionOptions(wait_for_space_transition=%s)",
                   options.wait_for_space_transition ? "true" : "false");
    }
  };

  inline void SetPosition(int64_t new_position) ABSL_LOCKS_EXCLUDED(mutex_) {
    SetPosition(new_position, {});
  }

  void SetPosition(int64_t new_position, SetPositionOptions options)
      ABSL_LOCKS_EXCLUDED(mutex_);

  void WaitForPendingCommit() ABSL_LOCKS_EXCLUDED(mutex_);

private:
  const SpaceState space_state_;

  mutable absl::Mutex mutex_;
  int64_t current_position_ ABSL_GUARDED_BY(mutex_) = 0;

  struct HardCommitData {
    int64_t position = 0;
    int64_t space_id = 0;

    template <typename Sink>
    friend void AbslStringify(Sink &sink, const HardCommitData &data) {
      absl::Format(&sink, "HardCommitData(position=%d, space_id=%d)",
                   data.position, data.space_id);
    }
  };

  enum class CommitType {
    kSoftCommit,
    kHardCommit,
  };

  inline static constexpr absl::string_view
  CommitTypeToString(CommitType commit_type) {
    switch (commit_type) {
      using enum CommitType;
    case kSoftCommit:
      return "kSoftCommit";
    case kHardCommit:
      return "kHardCommit";
    }

    return "(unknown)";
  }

  struct States {
    class Idle {
    public:
      Idle() = default;

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Idle &) {
        absl::Format(&sink, "Idle()");
      }
    };

    class Active {
    public:
      explicit Active(int64_t origin_position)
          : origin_position_(origin_position) {}

      int64_t origin_position() const { return origin_position_; }

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Active &state) {
        absl::Format(&sink, "Active(origin_position=%d)",
                     state.origin_position());
      }

    private:
      int64_t origin_position_ = 0;
    };

    class PendingCommit {
    public:
      PendingCommit(CFSharedPtr<CFStringRef> display_id, CommitType commit_type)
          : display_id_(std::move(display_id)), commit_type_(commit_type) {}

      const CFSharedPtr<CFStringRef> display_id() const { return display_id_; }

      CommitType commit_type() const { return commit_type_; }

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const PendingCommit &state) {
        absl::Format(
            &sink, "PendingCommit(display_id=\"%s\", commit_type=%s)",
            StatusOrToString(StringFromCFStringRef(state.display_id().get())),
            CommitTypeToString(state.commit_type()));
      }

    private:
      CFSharedPtr<CFStringRef> display_id_;
      CommitType commit_type_;
    };
  };

  using StateVariant =
      std::variant<States::Idle, States::Active, States::PendingCommit>;
  StateVariant state_ ABSL_GUARDED_BY(mutex_);
  HardCommitData latest_hard_commit_ ABSL_GUARDED_BY(mutex_);

  [[nodiscard]] std::pair<int64_t, int64_t> unlocked_position_soft_limit() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetState(StateVariant state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  std::string StateToString(const StateVariant &state) const;

  int64_t GetNextBoundary(bool moving_right) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  inline void SetPositionLocked(int64_t new_position)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    SetPositionLocked(new_position, {});
  }

  void SetPositionLocked(int64_t new_position, SetPositionOptions options)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void WaitForPendingCommitLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

} // namespace fasterswiper
