#include "src/event-tap-manager.h"

#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>

namespace fasterswiper {

absl::StatusOr<std::unique_ptr<EventTapManager>>
EventTapManager::Create(CGEventTapLocation tap, CGEventTapPlacement place,
                        CGEventTapOptions options,
                        const std::vector<CGEventType>& event_types_of_interest,
                        Callback callback) {
  auto result = absl::WrapUnique(new EventTapManager());
  result->callback_ = std::move(callback);

  CGEventMask mask = 0;
  for (auto event_type : event_types_of_interest) {
    mask |= CGEventMaskBit(event_type);
  }

  result->raw_tap_ = WrapCFUnique(
      CGEventTapCreate(tap, place, options, mask, CallbackShim, result.get()));
  if (result->raw_tap_ == nullptr) {
    return absl::InternalError("Failed to create event tap");
  }

  return result;
}

void EventTapManager::SetEnabled(bool enabled) {
  CGEventTapEnable(raw_tap_.get(), enabled);
}

CGEventRef EventTapManager::CallbackShim(CGEventTapProxy proxy,
                                         CGEventType type, CGEventRef event,
                                         void *user_info) {
  if (user_info == nullptr) {
    LOG(FATAL) << "Received a CGEventTap callback with null userInfo!";
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto tap = reinterpret_cast<EventTapManager *>(user_info);
  return tap->callback_(proxy, type, event);
}

} // namespace fasterswiper
