#pragma once

#include "src/macos-private.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <absl/base/nullability.h>
#include <absl/container/btree_map.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

namespace fasterswiper {

using CGEventDataElement =
    std::variant<int32_t, int64_t, float, double, std::string>;

struct CGEventData {
  int32_t version = 0;
  absl::btree_map<uint16_t, CGEventDataElement> fields;
};

absl::StatusOr<CGEventData> DeserializeCGEventData(absl::string_view data);

absl::StatusOr<CGEventData> DeserializeCGEventData(CGEventRef
                                                   absl_nonnull event);

using IOHIDEventData =
    std::variant<IOHIDFluidTouchGestureData, IOHIDVelocityEventData>;

struct IOHIDSystemQueueElementData {
  IOHIDSystemQueueElement header{};
  std::vector<IOHIDEventData> events;
};

absl::StatusOr<IOHIDSystemQueueElementData>
DeserializeIOHIDSystemQueueElementData(absl::string_view data);

absl::StatusOr<IOHIDSystemQueueElementData>
GenerateIOHIDSystemQueueElementDataFromCGEvent(CGEventRef absl_nonnull event);

absl::Status AugmentCGEvent(CGEventRef absl_nonnull event,
                            const IOHIDSystemQueueElementData& element);

absl::Status AugmentCGEvent(CGEventRef absl_nonnull event);

}  // namespace fasterswiper
