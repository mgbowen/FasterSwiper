#pragma once

#include "src/cf-util.h"

#include <memory>

#include <absl/status/statusor.h>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

namespace fasterswiper {

class EventTapManager {
public:
  using Callback =
      absl::AnyInvocable<CGEventRef(CGEventTapProxy, CGEventType, CGEventRef)>;

  static absl::StatusOr<std::unique_ptr<EventTapManager>>
  Create(CGEventTapLocation tap, CGEventTapPlacement place,
         CGEventTapOptions options,
         const std::vector<CGEventType> &event_types_of_interest,
         Callback callback);

  CFMachPortRef get() const { return raw_tap_.get(); }

  void SetEnabled(bool enabled);

private:
  Callback callback_;
  CFUniquePtr<CFMachPortRef> raw_tap_;

  EventTapManager() = default;

  static CGEventRef CallbackShim(CGEventTapProxy proxy, CGEventType type,
                                 CGEventRef event, void *user_info);
};

} // namespace fasterswiper
