#pragma once

#include "src/cf-util.h"

#include <memory>

#include "absl/status/statusor.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

namespace fasterswiper {

class EventTapManager {
public:
  using Callback =
      absl::AnyInvocable<CGEventRef(CGEventTapProxy, CGEventType, CGEventRef)>;

  static absl::StatusOr<std::unique_ptr<EventTapManager>>
  Create(CGEventTapLocation tap, CGEventTapPlacement place,
         CGEventTapOptions options, std::vector<uint64_t> eventTypesOfInterest,
         Callback callback);

  CFMachPortRef get() const { return raw_tap_.get(); }

  void SetEnabled(bool enabled);

private:
  Callback callback_;
  CFUniquePtr<CFMachPortRef> raw_tap_;

  EventTapManager() = default;

  static CGEventRef CallbackShim(CGEventTapProxy proxy, CGEventType type,
                                 CGEventRef event, void *userInfo);
};

} // namespace fasterswiper
