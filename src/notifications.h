#pragma once

#include <future>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "src/macos-private.h"

namespace fasterswiper {

class NotificationManager {
public:
  static absl::StatusOr<std::unique_ptr<NotificationManager>> Create();
  ~NotificationManager();

  std::future<void> GetSpaceTransitionFuture() ABSL_LOCKS_EXCLUDED(mutex_);

private:
  NotificationManager();

  absl::Mutex mutex_;
  std::vector<std::promise<void>> pending_promises_ ABSL_GUARDED_BY(mutex_);

  void OnCGSNotificationReceived(
      CGSEventType event_type,
      std::optional<std::span<const unsigned char>> maybe_data);
  static void OnCGSNotificationReceivedShim(CGSEventType event_type, void *data,
                                            unsigned int data_length,
                                            void *user_data);
};

} // namespace fasterswiper
