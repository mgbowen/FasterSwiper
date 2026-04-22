#pragma once

#include "src/space-state.h"

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
    struct Idle {
      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Idle &) {
        sink.Append("Idle()");
      }
    };

    struct Active {
      explicit Active(int64_t origin_position)
          : origin_position(origin_position) {}

      template <typename Sink>
      friend void AbslStringify(Sink &sink, const Active &state) {
        absl::Format(&sink, "Active(origin_position=%d)",
                     state.origin_position);
      }

      int64_t origin_position = 0;
    };

    struct PendingCommit {
      template <typename Sink>
      friend void AbslStringify(Sink &sink, const PendingCommit &state) {
        sink.Append("PendingCommit");
      }
    };
  };

  using StateVariant =
      std::variant<States::Idle, States::Active, States::PendingCommit>;
  StateVariant state_ ABSL_GUARDED_BY(mutex_) = States::Idle{};

  [[nodiscard]] std::pair<int64_t, int64_t> unlocked_position_soft_limit() const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetState(StateVariant state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  std::string StateToString(const StateVariant &state) const;

  int64_t GetNextBoundary(bool moving_right) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  void SetPositionLocked(int64_t nanoswipes)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
};

} // namespace fasterswiper
