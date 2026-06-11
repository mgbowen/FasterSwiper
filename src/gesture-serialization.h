#pragma once

#include "src/cf-util.h"
#include "src/macos-private.h"

#include <string>

#include <ApplicationServices/ApplicationServices.h>

#include <absl/container/btree_map.h>
#include <absl/status/status.h>
#include <absl/strings/string_view.h>

namespace fasterswiper {

using CGEventDataElement =
    std::variant<int32_t, int64_t, float, double, std::string>;

struct CGEventData {
  int32_t version = 0;
  absl::btree_map<uint16_t, CGEventDataElement> fields;
};

absl::StatusOr<CGEventData> DeserializeCGEventData(absl::string_view data);

absl::StatusOr<CGEventData>
DeserializeCGEventData(const CGEventRef absl_nonnull event);

absl::StatusOr<std::string> SerializeCGEventData(const CGEventData &event_data);

using IOHIDEventData =
    std::variant<IOHIDFluidTouchGestureData, IOHIDVelocityEventData>;

struct IOHIDSystemQueueElementData {
  IOHIDSystemQueueElement header;
  std::vector<IOHIDEventData> events;
};

absl::StatusOr<IOHIDSystemQueueElementData>
DeserializeIOHIDSystemQueueElementData(absl::string_view data);

std::string SerializeIOHIDSystemQueueElementData(
    const IOHIDSystemQueueElementData &element);

absl::StatusOr<IOHIDSystemQueueElementData>
GenerateIOHIDSystemQueueElementDataFromCGEvent(
    const CGEventRef absl_nonnull event);

absl::StatusOr<CFUniquePtr<CGEventRef absl_nonnull>>
AugmentCGEvent(const CGEventRef absl_nonnull event,
               const IOHIDSystemQueueElementData &element);

absl::StatusOr<CFUniquePtr<CGEventRef absl_nonnull>>
AugmentCGEvent(const CGEventRef absl_nonnull event);

} // namespace fasterswiper
