#include "src/gesture-serialization.h"

#include "src/cf-util.h"
#include "src/event.h"
#include "src/fp1616.h"
#include "src/macos-private.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <absl/base/nullability.h>
#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

namespace fasterswiper {

namespace {

void SwapBytes(uint8_t* absl_nonnull ptr, size_t length) {
  for (size_t i = 0; i < length / 2; i++) {
    const size_t other_index = length - i - 1;
    const uint8_t temp = ptr[i];
    ptr[i] = ptr[other_index];
    ptr[other_index] = temp;
  }
}

template <typename T>
absl::StatusOr<T> ReadBE(absl::string_view& data) {
  constexpr auto type_size = sizeof(T);
  if (data.size() < type_size) {
    return absl::OutOfRangeError("Out of range");
  }

  T value = 0;

  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
  std::memcpy(&value, data.data(), type_size);

  if constexpr (std::endian::native != std::endian::big) {
    SwapBytes(reinterpret_cast<uint8_t*>(&value), type_size);
  }

  data = data.substr(type_size);
  return value;
}

constexpr int8_t kCGEventDataTagInt64OrBinaryBlob = 0b00;
constexpr int8_t kCGEventDataTagInt32 = 0b01;
constexpr int8_t kCGEventDataTagFloatingPoint = 0b11;

absl::StatusOr<CGEventDataElement> ReadInt64OrBinaryBlob(
    absl::string_view& data, int16_t element_size) {
  if (element_size == 1) {
    return ReadBE<int64_t>(data);
  }

  if (element_size <= 0) {
    return absl::OutOfRangeError(absl::StrCat(
        "Cannot read Int64OrBinaryBlob with invalid size ", element_size));
  }

  if (data.size() < static_cast<size_t>(element_size)) {
    return absl::OutOfRangeError("Out of range");
  }

  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
  std::string blob{data.data(), static_cast<size_t>(element_size)};
  data = data.substr(static_cast<size_t>(element_size));
  return blob;
}

absl::StatusOr<CGEventDataElement> ReadInt32(absl::string_view& data,
                                             int16_t element_size) {
  if (element_size == 1) {
    return ReadBE<int32_t>(data);
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Cannot read Int32 with invalid size ", element_size));
}

absl::StatusOr<CGEventDataElement> ReadFloatingPoint(absl::string_view& data,
                                                     int16_t element_size) {
  if (element_size == 1) {
    return ReadBE<float>(data);
  }

  if (element_size == 2) {
    return ReadBE<double>(data);
  }

  return absl::InvalidArgumentError(absl::StrCat(
      "Cannot read FloatingPoint with invalid size ", element_size));
}

struct CGEventDataFieldHeader {
  uint16_t element_size = 0;
  uint16_t tag = 0;
  uint16_t field = 0;
};

absl::StatusOr<CGEventDataFieldHeader> ReadFieldHeader(
    absl::string_view& data) {
  ASSIGN_OR_RETURN(const auto element_size, ReadBE<uint16_t>(data));
  ASSIGN_OR_RETURN(const auto tag_and_field, ReadBE<uint16_t>(data));

  const uint16_t tag = tag_and_field >> 14 & 0x0003;
  const uint16_t field = tag_and_field & 0x3FFF;

  return CGEventDataFieldHeader{
      .element_size = element_size,
      .tag = tag,
      .field = field,
  };
}

}  // namespace

absl::StatusOr<CGEventData> DeserializeCGEventData(absl::string_view data) {
  CGEventData result;

  ASSIGN_OR_RETURN(result.version, ReadBE<int32_t>(data));
  if (result.version != 2) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported CGEventData version ", result.version));
  }

  while (!data.empty()) {
    ASSIGN_OR_RETURN(CGEventDataFieldHeader field_header,
                     ReadFieldHeader(data));
    switch (field_header.tag) {
      case kCGEventDataTagInt64OrBinaryBlob: {
        ASSIGN_OR_RETURN(
            result.fields[field_header.field],
            ReadInt64OrBinaryBlob(data, field_header.element_size));
        break;
      }
      case kCGEventDataTagInt32: {
        ASSIGN_OR_RETURN(result.fields[field_header.field],
                         ReadInt32(data, field_header.element_size));
        break;
      }
      case kCGEventDataTagFloatingPoint: {
        ASSIGN_OR_RETURN(result.fields[field_header.field],
                         ReadFloatingPoint(data, field_header.element_size));
        break;
      }
      default: {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid tag ", field_header.tag));
      }
    }
  }

  return result;
}

absl::StatusOr<CGEventData> DeserializeCGEventData(CGEventRef
                                                   absl_nonnull event) {
  const auto serialized_data_ref =
      WrapCFUnique(CGEventCreateData(/*allocator=*/nullptr, event));
  if (serialized_data_ref == nullptr) {
    return absl::InvalidArgumentError("Failed to serialize event");
  }

  const uint8_t* data_ptr = CFDataGetBytePtr(serialized_data_ref.get());
  if (data_ptr == nullptr) {
    return absl::InternalError(
        "Failed to retrieve pointer to serialized event data");
  }

  const auto size = CFDataGetLength(serialized_data_ref.get());
  if (size == 0) {
    return absl::InternalError(
        "Failed to retrieve size of serialized event data");
  }

  const absl::string_view data(reinterpret_cast<const char*>(data_ptr), size);
  return DeserializeCGEventData(data);
}

namespace {

template <typename T>
absl::StatusOr<T> ReadStruct(
    absl::string_view data,
    std::optional<uint32_t> indicated_size = std::nullopt) {
  if (data.size() < sizeof(T)) {
    return absl::OutOfRangeError("Out of range");
  }

  if (indicated_size.has_value() && *indicated_size != sizeof(T)) {
    return absl::DataLossError(absl::StrCat("Unexpected size (expected ",
                                            sizeof(T), ", got ",
                                            *indicated_size, ")"));
  }

  T event_data_copy{};

  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
  std::memcpy(&event_data_copy, data.data(), sizeof(T));
  return event_data_copy;
}

template <typename T>
absl::StatusOr<T> ConsumeStruct(
    absl::string_view& data,
    std::optional<uint32_t> indicated_size = std::nullopt) {
  ASSIGN_OR_RETURN(T copied_struct, ReadStruct<T>(data, indicated_size));
  data = data.substr(sizeof(T));
  return copied_struct;
}

absl::StatusOr<IOHIDEventData> ConsumeIOHIDEventData(absl::string_view& data) {
  if (data.size() < sizeof(IOHIDEventBase)) {
    return absl::OutOfRangeError("Out of range");
  }

  const auto* event_data_header =
      reinterpret_cast<const IOHIDEventBase*>(data.data());

  switch (event_data_header->type) {
    using enum IOHIDEventType;
    case kIOHIDEventTypeVelocity: {
      return ConsumeStruct<IOHIDVelocityEventData>(data,
                                                   event_data_header->size);
    }
    case kIOHIDEventTypeFluidTouchGesture: {
      return ConsumeStruct<IOHIDFluidTouchGestureData>(data,
                                                       event_data_header->size);
    }
    case kIOHIDEventTypeUnknown: {
      break;
    }
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported IOHIDEventType: ",
                   static_cast<uint32_t>(event_data_header->type)));
}

constexpr size_t kMaxDockSwipeHIDBytes = sizeof(IOHIDSystemQueueElement) +
                                         sizeof(IOHIDFluidTouchGestureData) +
                                         sizeof(IOHIDVelocityEventData);
static_assert(kMaxDockSwipeHIDBytes == 96, "Unexpected kMaxDockSwipeHIDBytes");

struct DockSwipeQueueElement {
  IOHIDSystemQueueElement header{};
  IOHIDFluidTouchGestureData gesture{};
  std::optional<IOHIDVelocityEventData> velocity;
};

DockSwipeQueueElement BuildDockSwipeQueueElement(CGEventRef
                                                 absl_nonnull event) {
  const int64_t phase =
      CGEventGetIntegerValueField(event, kCGEventGesturePhase);
  const int64_t motion =
      CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion);
  const double progress =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress);
  const double pos_x =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionX);
  const double pos_y =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionY);
  const double vel_x =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX);
  const double vel_y =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY);
  const int64_t swipe_mask =
      CGEventGetIntegerValueField(event, kCGEventGestureSwipeMask);

  DockSwipeQueueElement result{};
  result.gesture = IOHIDFluidTouchGestureData{
      .base = {.size = sizeof(IOHIDFluidTouchGestureData),
               .type = IOHIDEventType::kIOHIDEventTypeFluidTouchGesture,
               .options = static_cast<uint32_t>((phase & 0xFF) << 24),
               .depth = 0,
               .reserved = {0, 0, 0}},
      .position_x = DoubleToFixedFP1616(pos_x),
      .position_y = DoubleToFixedFP1616(pos_y),
      .position_z = 0,
      .swipe_mask = static_cast<IOHIDSwipeMask>(swipe_mask),
      .gesture_motion = static_cast<IOHIDGestureMotion>(motion),
      .gesture_flavor = IOHIDGestureFlavor::kIOHIDGestureFlavorDockPrimary,
      .swipe_progress = DoubleToFixedFP1616(progress)};

  if (vel_x != 0.0 || vel_y != 0.0 || phase == kGestureEnded) {
    result.velocity = IOHIDVelocityEventData{
        .base = {.size = sizeof(IOHIDVelocityEventData),
                 .type = IOHIDEventType::kIOHIDEventTypeVelocity,
                 .options = 0,
                 .depth = 1,
                 .reserved = {0, 0, 0}},
        .velocity_x = DoubleToFixedFP1616(vel_x),
        .velocity_y = DoubleToFixedFP1616(vel_y),
        .velocity_z = 0};
  }

  const uint64_t cg_timestamp_ns = CGEventGetTimestamp(event);
  const uint64_t timestamp = cg_timestamp_ns != 0
                                 ? NanosecondsToMachTicks(cg_timestamp_ns)
                                 : NextMonotonicMachTimestamp();

  result.header = {.timestamp = timestamp,
                   .sender_id = 0,
                   .options = 0,
                   .attribute_length = 0,
                   .event_count = result.velocity.has_value() ? 2u : 1u};

  return result;
}

absl::Status AttachIOHIDEventBytes(CGEventRef absl_nonnull event,
                                   const uint8_t* absl_nonnull bytes,
                                   size_t length) {
  const CFUniquePtr<IOHIDEventRef> hid_event =
      WrapCFUnique(IOHIDEventCreateWithBytes(kCFAllocatorDefault, bytes,
                                             static_cast<CFIndex>(length)));
  if (hid_event == nullptr) {
    return absl::InternalError(
        "Failed to create IOHIDEvent from serialized bytes");
  }

  SLEventSetIOHIDEvent(event, hid_event.get());
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<IOHIDSystemQueueElementData>
DeserializeIOHIDSystemQueueElementData(absl::string_view data) {
  IOHIDSystemQueueElementData result;

  ASSIGN_OR_RETURN(result.header, ConsumeStruct<IOHIDSystemQueueElement>(data));

  for (uint32_t i = 0; i < result.header.event_count; i++) {
    ASSIGN_OR_RETURN(IOHIDEventData event_data, ConsumeIOHIDEventData(data));
    result.events.push_back(event_data);
  }

  if (!data.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("When deserializing an IOHIDSystemQueueElement, had ",
                     data.size(), " bytes left over"));
  }

  return result;
}

absl::StatusOr<IOHIDSystemQueueElementData>
GenerateIOHIDSystemQueueElementDataFromCGEvent(CGEventRef absl_nonnull event) {
  CHECK(event != nullptr);

  const DockSwipeQueueElement built = BuildDockSwipeQueueElement(event);

  IOHIDSystemQueueElementData result{};
  result.header = built.header;
  result.events.reserve(built.header.event_count);
  result.events.emplace_back(built.gesture);
  if (built.velocity.has_value()) {
    result.events.emplace_back(*built.velocity);
  }

  return result;
}

absl::Status AugmentCGEvent(CGEventRef absl_nonnull event,
                            const IOHIDSystemQueueElementData& element) {
  CHECK(event != nullptr);

  if (element.events.empty()) {
    return absl::InvalidArgumentError(
        "IOHIDSystemQueueElementData must contain at least one event");
  }
  if (element.header.event_count != element.events.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "IOHIDSystemQueueElementData event_count (", element.header.event_count,
        ") does not match events size (", element.events.size(), ")"));
  }

  size_t total_size = sizeof(element.header);
  for (const IOHIDEventData& event_data : element.events) {
    total_size +=
        std::visit([](const auto& typed_event) { return sizeof(typed_event); },
                   event_data);
  }

  auto serialize_into = [&](uint8_t* absl_nonnull dst) {
    size_t offset = 0;
    std::memcpy(dst + offset, &element.header, sizeof(element.header));
    offset += sizeof(element.header);
    for (const IOHIDEventData& event_data : element.events) {
      std::visit(
          [&](const auto& typed_event) {
            std::memcpy(dst + offset, &typed_event, sizeof(typed_event));
            offset += sizeof(typed_event);
          },
          event_data);
    }
  };

  constexpr size_t kStackBufferSize = 128;
  if (total_size <= kStackBufferSize) {
    std::array<uint8_t, kStackBufferSize> buffer{};
    serialize_into(buffer.data());
    return AttachIOHIDEventBytes(event, buffer.data(), total_size);
  }

  std::vector<uint8_t> buffer(total_size);
  serialize_into(buffer.data());
  return AttachIOHIDEventBytes(event, buffer.data(), buffer.size());
}

absl::Status AugmentCGEvent(CGEventRef absl_nonnull event) {
  CHECK(event != nullptr);

  const DockSwipeQueueElement built = BuildDockSwipeQueueElement(event);
  std::array<uint8_t, kMaxDockSwipeHIDBytes> buffer{};
  size_t offset = 0;

  std::memcpy(buffer.data() + offset, &built.header, sizeof(built.header));
  offset += sizeof(built.header);
  std::memcpy(buffer.data() + offset, &built.gesture, sizeof(built.gesture));
  offset += sizeof(built.gesture);
  if (built.velocity.has_value()) {
    std::memcpy(buffer.data() + offset, &(*built.velocity),
                sizeof(*built.velocity));
    offset += sizeof(*built.velocity);
  }

  return AttachIOHIDEventBytes(event, buffer.data(), offset);
}

}  // namespace fasterswiper
