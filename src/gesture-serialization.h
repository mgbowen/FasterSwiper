#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <string>
#include <optional>
#include <absl/strings/string_view.h>

#include "src/cf-util.h"

namespace fasterswiper {

// Serializes a CGEventRef to an opaque binary string, injecting the private
// Field 4205 (IOHIDSystemQueueElement) payload computed from standard gesture fields.
std::optional<std::string> SerializeGestureEvent(CGEventRef event);

// Deserializes a serialized CGEvent data string back into a CGEventRef.
CFUniquePtr<CGEventRef> DeserializeGestureEvent(absl::string_view data);

} // namespace fasterswiper
