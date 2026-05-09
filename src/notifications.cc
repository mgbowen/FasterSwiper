#include "src/notifications.h"

#include "src/macos-private.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace fasterswiper {

struct NotificationManagerCallbackData {
  absl::AnyInvocable<void(CGSEventType, const std::span<unsigned char>)>
      callback;
};

absl::StatusOr<std::unique_ptr<NotificationManager>>
NotificationManager::Create() {
  auto notification_manager = absl::WrapUnique(new NotificationManager());

  const CGError result =
      CGSRegisterNotifyProc(OnCGSNotificationReceivedShim,
                            kCGSWorkspaceDidChange, notification_manager.get());
  if (result != kCGErrorSuccess) {
    return absl::InternalError(
        absl::StrCat("CGSRegisterNotifyProc() for event type ",
                     kCGSWorkspaceDidChange, " returned error: ", result));
  }

  return notification_manager;
}

NotificationManager::NotificationManager() = default;

NotificationManager::~NotificationManager() = default;

std::future<void> NotificationManager::GetSpaceTransitionFuture() {
  absl::MutexLock lock(mutex_);
  std::promise<void> promise;
  std::future<void> future = promise.get_future();
  pending_promises_.push_back(std::move(promise));
  return future;
}

void NotificationManager::OnCGSNotificationReceived(
    CGSEventType event_type,
    std::optional<std::span<const unsigned char>> maybe_data) {
  VLOG(1) << "OnCGSNotificationReceived(): event_type=" << event_type
          << ", maybe_data="
          << (maybe_data.has_value()
                  ? absl::StrFormat("{data=%p, size=%d}", maybe_data->data(),
                                    maybe_data->size())
                  : "(std::nullopt)");

  if (maybe_data.has_value()) {
    VLOG(1) << "  Data: ["
            << absl::StrJoin(maybe_data->begin(), maybe_data->end(), ", ",
                             [](std::string *out, unsigned char byte) {
                               absl::StrAppendFormat(out, "0x%02x", byte);
                             })
            << "]";
  }

  absl::MutexLock lock(mutex_);
  for (std::promise<void> &promise : pending_promises_) {
    promise.set_value();
  }

  pending_promises_.clear();
}

void NotificationManager::OnCGSNotificationReceivedShim(
    CGSEventType event_type, void *data, unsigned int data_length,
    void *user_data) {
  CHECK(user_data != nullptr)
      << "OnCGSNotificationReceivedShim() invoked with nullptr userData";

  VLOG(1) << "OnCGSNotificationReceivedShim(): event_type=" << event_type
          << ", data=" << data << ", data_length=" << data_length;

  std::optional<std::span<const unsigned char>> maybe_data;
  if (data != nullptr) {
    maybe_data = std::span<const unsigned char>(
        reinterpret_cast<const unsigned char *>(data), data_length);
  }

  auto notification_manager =
      reinterpret_cast<NotificationManager *>(user_data);
  notification_manager->OnCGSNotificationReceived(event_type, maybe_data);
}

} // namespace fasterswiper
