#include "src/gesture-serialization.h"

#include <ApplicationServices/ApplicationServices.h>
#include <arpa/inet.h>
#include <cstring>
#include <mach/mach_time.h>
#include <vector>

#include "src/macos-private.h"
#include <absl/log/log.h>

namespace fasterswiper {

namespace {

#pragma pack(push, 1)

struct IOHIDEventBase {
  uint32_t size; // size of the structure (e.g. 40 or 28)
  uint32_t type; // IOHIDEventType (e.g. 23 = FluidTouchGesture, 9 = Velocity)
  uint32_t options; // touch/gesture phase packed at high byte: ((phase & 0xFF)
                    // << 24)
  uint8_t depth;    // nesting depth
  uint8_t reserved[3]; // reserved padding
};

struct IOHIDFluidTouchGestureData {
  IOHIDEventBase base;
  int32_t position_x;      // 16.16 fixed-point
  int32_t position_y;      // 16.16 fixed-point
  int32_t position_z;      // 16.16 fixed-point (typically 0)
  uint32_t swipe_mask;     // swipe direction bitmask
  uint16_t gesture_motion; // IOHIDGestureMotion (e.g. 1 =
                           // kIOHIDGestureMotionHorizontalX)
  uint16_t gesture_flavor; // IOHIDGestureFlavor (e.g. 3 =
                           // kIOHIDGestureFlavorDockPrimary)
  int32_t swipe_progress;  // 16.16 fixed-point
};

struct IOHIDVelocityEventData {
  IOHIDEventBase base;
  int32_t velocity_x; // 16.16 fixed-point
  int32_t velocity_y; // 16.16 fixed-point
  int32_t velocity_z; // 16.16 fixed-point (typically 0)
};

struct IOHIDSystemQueueElementHeader {
  uint64_t timestamp;        // Mach Absolute Time
  uint64_t sender_id;        // IORegistryEntryID for sending device
  uint32_t options;          // consistently 0
  uint32_t attribute_length; // consistently 0
  uint32_t event_count;      // count of nested structs (1 or 2)
};

#pragma pack(pop)

int32_t DoubleToFixed16_16(double val) {
  const auto fixed_val = static_cast<int32_t>(val * 65536.0);
  if (fixed_val == 0 && std::abs(val) > 0) {
    const int32_t sign = val > 0 ? 1 : -1;
    return sign;
  }

  return fixed_val;
}

} // namespace

std::optional<std::string> SerializeGestureEvent(CGEventRef event) {
  if (!event) {
    return std::nullopt;
  }

  // Use the public API to serialize the event.
  CFUniquePtr<CFDataRef> cf_data(CGEventCreateData(nullptr, event));
  if (!cf_data) {
    LOG(ERROR) << "CGEventCreateData returned nullptr";
    return std::nullopt;
  }

  const uint8_t *data = CFDataGetBytePtr(cf_data.get());
  size_t len = CFDataGetLength(cf_data.get());

  if (len < 4) {
    LOG(ERROR) << "Serialized event data is too short: " << len;
    return std::nullopt;
  }

  // Re-serialize the fields, omitting any pre-existing field 4205.
  std::string new_data;
  new_data.append(reinterpret_cast<const char *>(data),
                  4); // copy version header

  size_t idx = 4;
  while (idx < len) {
    if (idx + 4 > len) {
      LOG(WARNING) << "Malformed serialization layout: trailing bytes";
      break;
    }

    uint16_t word1, word2;
    std::memcpy(&word1, &data[idx], 2);
    std::memcpy(&word2, &data[idx + 2], 2);

    uint16_t size_words = ntohs(word1);
    uint16_t tag_id = ntohs(word2);
    uint16_t field_id = tag_id & 0x3FFF;
    uint16_t type_bits = (tag_id >> 14) & 3;

    size_t field_size_bytes = 0;
    if (type_bits == 0) {
      if (size_words == 1) {
        field_size_bytes = 8;
      } else {
        field_size_bytes = (size_words + 3) & ~3;
      }
    } else if (type_bits == 1 || type_bits == 3) {
      field_size_bytes = size_words * 4;
    }

    if (idx + 4 + field_size_bytes > len) {
      LOG(WARNING)
          << "Malformed serialization layout: field goes out of bounds";
      break;
    }

    // Skip field 4205 if it is already present.
    if (field_id != 4205) {
      new_data.append(reinterpret_cast<const char *>(&data[idx]),
                      4 + field_size_bytes);
    }

    idx += 4 + field_size_bytes;
  }

  // Extract necessary gesture properties from the CGEventRef to construct 4205
  // payload.
  int64_t phase = CGEventGetIntegerValueField(event, kCGEventGesturePhase);
  int64_t motion =
      CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion);
  double progress =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress);
  double pos_x =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionX);
  double pos_y =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionY);
  double vel_x =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX);
  double vel_y =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY);
  int64_t swipe_mask = CGEventGetIntegerValueField(event, (CGEventField)115);

  uint64_t timestamp = CGEventGetTimestamp(event);
  if (timestamp == 0) {
    timestamp = mach_absolute_time();
  }

  // Determine configuration based on phase and velocity.
  bool has_velocity = (vel_x != 0.0 || vel_y != 0.0 || phase == kGestureEnded);
  uint32_t event_count = has_velocity ? 2 : 1;
  uint16_t payload_size = has_velocity ? 96 : 68;

  // Populate structures.
  IOHIDSystemQueueElementHeader header = {.timestamp = timestamp,
                                          .sender_id = 0x100003416ULL,
                                          .options = 0,
                                          .attribute_length = 0,
                                          .event_count = event_count};

  IOHIDFluidTouchGestureData inner1 = {
      .base = {.size = 40,
               .type = 23, // kIOHIDEventTypeFluidTouchGesture
               .options = static_cast<uint32_t>((phase & 0xFF) << 24),
               .depth = 0,
               .reserved = {0, 0, 0}},
      .position_x = DoubleToFixed16_16(pos_x),
      .position_y = DoubleToFixed16_16(pos_y),
      .position_z = 0,
      .swipe_mask = static_cast<uint32_t>(swipe_mask),
      .gesture_motion = static_cast<uint16_t>(motion),
      .gesture_flavor = 3, // kIOHIDGestureFlavorDockPrimary
      .swipe_progress = DoubleToFixed16_16(progress)};

  IOHIDVelocityEventData inner2 = {
      .base = {.size = 28,
               .type = 9, // kIOHIDEventTypeVelocity
               .options = 0,
               .depth = 1,
               .reserved = {0, 0, 0}},
      .velocity_x = DoubleToFixed16_16(vel_x),
      .velocity_y = DoubleToFixed16_16(vel_y),
      .velocity_z = 0};

  // Append Field 4205 Tag (Type 0b00, size = payload_size).
  uint16_t tag_word1 = htons(payload_size);
  uint16_t tag_word2 = htons(4205);
  new_data.append(reinterpret_cast<const char *>(&tag_word1), 2);
  new_data.append(reinterpret_cast<const char *>(&tag_word2), 2);

  // Append structures.
  new_data.append(reinterpret_cast<const char *>(&header), sizeof(header));
  new_data.append(reinterpret_cast<const char *>(&inner1), sizeof(inner1));
  if (has_velocity) {
    new_data.append(reinterpret_cast<const char *>(&inner2), sizeof(inner2));
  }

  return new_data;
}

CFUniquePtr<CGEventRef> DeserializeGestureEvent(absl::string_view data) {
  if (data.empty()) {
    return nullptr;
  }

  CFUniquePtr<CFDataRef> cf_data(CFDataCreate(
      nullptr, reinterpret_cast<const uint8_t *>(data.data()), data.size()));
  if (!cf_data) {
    LOG(ERROR) << "CFDataCreate returned nullptr";
    return nullptr;
  }

  return WrapCFUnique(CGEventCreateFromData(nullptr, cf_data.get()));
}

} // namespace fasterswiper
