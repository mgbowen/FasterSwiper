#include "src/event-tap-manager.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace fasterswiper {

absl::StatusOr<std::unique_ptr<EventTapManager>>
EventTapManager::Create(CGEventTapLocation tap, CGEventTapPlacement place,
                        CGEventTapOptions options,
                        std::vector<CGEventType> eventTypesOfInterest,
                        Callback callback) {
  auto result = absl::WrapUnique(new EventTapManager());
  result->callback_ = std::move(callback);

  CGEventMask mask = 0;
  for (auto event_type : eventTypesOfInterest) {
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
                                         void *userInfo) {
  if (userInfo == nullptr) {
    LOG(FATAL) << "Received a CGEventTap callback with null userInfo!";
  }

  auto tap = reinterpret_cast<EventTapManager *>(userInfo);
  return tap->callback_(proxy, type, event);
}

} // namespace fasterswiper
