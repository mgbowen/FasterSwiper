#include "src/gesture-serialization.h"

#include "src/cf-util.h"
#include "src/macos-private.h"
#include "src/variant-util.h"

#include <bit>
#include <cstring>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>

#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status_macros.h>

namespace fasterswiper {

namespace {

FixedFP1616 DoubleToFixedFP1616(double val) {
  const auto fixed_val = static_cast<FixedFP1616>(val * 65536.0);
  if (fixed_val == 0 && std::abs(val) > 0) {
    const FixedFP1616 sign = val > 0 ? 1 : -1;
    return sign;
  }

  return fixed_val;
}

void SwapBytes(uint8_t *absl_nonnull ptr, size_t length) {
  for (auto i = 0; i < length / 2; i++) {
    const auto other_index = length - i - 1;
    const auto temp = ptr[i];
    ptr[i] = ptr[other_index];
    ptr[other_index] = temp;
  }
}

template <typename T> absl::StatusOr<T> ReadBE(absl::string_view &data) {
  constexpr auto type_size = sizeof(T);
  if (data.size() < type_size) {
    return absl::OutOfRangeError("Out of range");
  }

  T value = 0;

  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
  std::memcpy(&value, data.data(), type_size);

  if constexpr (std::endian::native != std::endian::big) {
    SwapBytes(reinterpret_cast<uint8_t *>(&value), type_size);
  }

  data = data.substr(type_size);
  return value;
}

template <typename T> void WriteBE(absl::Cord &cord, T value) {
  constexpr size_t type_size = sizeof(T);
  if constexpr (std::endian::native != std::endian::big) {
    SwapBytes(reinterpret_cast<uint8_t *>(&value), type_size);
  }

  absl::string_view bytes{reinterpret_cast<const char *>(&value), type_size};
  cord.Append(bytes);
}

constexpr int8_t kCGEventDataTagInt64OrBinaryBlob = 0b00;
constexpr int8_t kCGEventDataTagInt32 = 0b01;
constexpr int8_t kCGEventDataTagFloatingPoint = 0b11;

absl::StatusOr<CGEventDataElement>
ReadInt64OrBinaryBlob(absl::string_view &data, int16_t element_size) {
  if (element_size == 1) {
    return ReadBE<int64_t>(data);
  }

  if (element_size <= 0) {
    return absl::OutOfRangeError(absl::StrCat(
        "Cannot read Int64OrBinaryBlob with invalid size ", element_size));
  }

  if (data.size() < element_size) {
    return absl::OutOfRangeError("Out of range");
  }

  // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
  std::string blob{data.data(), static_cast<size_t>(element_size)};
  data = data.substr(element_size);
  return blob;
}

absl::StatusOr<CGEventDataElement> ReadInt32(absl::string_view &data,
                                             int16_t element_size) {
  if (element_size == 1) {
    return ReadBE<int32_t>(data);
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Cannot read Int32 with invalid size ", element_size));
}

absl::StatusOr<CGEventDataElement> ReadFloatingPoint(absl::string_view &data,
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

absl::StatusOr<CGEventDataFieldHeader>
ReadFieldHeader(absl::string_view &data) {
  ASSIGN_OR_RETURN(const auto element_size, ReadBE<uint16_t>(data));
  ASSIGN_OR_RETURN(const auto tag_and_field, ReadBE<uint16_t>(data));

  const uint16_t tag = (tag_and_field >> 14) & 0x0003;
  const uint16_t field = tag_and_field & 0x3FFF;

  return CGEventDataFieldHeader{
      .element_size = element_size,
      .tag = tag,
      .field = field,
  };
}

void WriteFieldHeader(absl::Cord &cord, const CGEventDataFieldHeader &header) {
  const uint16_t tag_and_field =
      ((header.tag & 0x0003) << 14) | (header.field & 0x3FFF);

  WriteBE(cord, header.element_size);
  WriteBE(cord, tag_and_field);
}

void WriteField(absl::Cord &cord, uint16_t field,
                const CGEventDataElement &element) {
  std::visit(
      overloaded{
          [&](int32_t value) {
            WriteFieldHeader(cord, CGEventDataFieldHeader{
                                       .element_size = 1,
                                       .tag = kCGEventDataTagInt32,
                                       .field = field,
                                   });
            WriteBE(cord, value);
          },
          [&](int64_t value) {
            WriteFieldHeader(cord, CGEventDataFieldHeader{
                                       .element_size = 1,
                                       .tag = kCGEventDataTagInt64OrBinaryBlob,
                                       .field = field,
                                   });
            WriteBE(cord, value);
          },
          [&](float value) {
            WriteFieldHeader(cord, CGEventDataFieldHeader{
                                       .element_size = 1,
                                       .tag = kCGEventDataTagFloatingPoint,
                                       .field = field,
                                   });
            WriteBE(cord, value);
          },
          [&](double value) {
            WriteFieldHeader(cord, CGEventDataFieldHeader{
                                       .element_size = 2,
                                       .tag = kCGEventDataTagFloatingPoint,
                                       .field = field,
                                   });
            WriteBE(cord, value);
          },
          [&](const std::string &value) {
            WriteFieldHeader(
                cord, CGEventDataFieldHeader{
                          .element_size = static_cast<uint16_t>(value.size()),
                          .tag = kCGEventDataTagInt64OrBinaryBlob,
                          .field = field,
                      });
            cord.Append(value);
          }},
      element);
}

} // namespace

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
      ASSIGN_OR_RETURN(result.fields[field_header.field],
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

absl::StatusOr<CGEventData>
DeserializeCGEventData(const CGEventRef absl_nonnull event) {
  const auto serialized_data_ref =
      WrapCFUnique(CGEventCreateData(/*allocator=*/nullptr, event));
  if (serialized_data_ref == nullptr) {
    return absl::InvalidArgumentError("Failed to serialize event");
  }

  const uint8_t *data_ptr = CFDataGetBytePtr(serialized_data_ref.get());
  if (data_ptr == nullptr) {
    return absl::InternalError(
        "Failed to retrieve pointer to serialized event data");
  }

  const auto size = CFDataGetLength(serialized_data_ref.get());
  if (size == 0) {
    return absl::InternalError(
        "Failed to retrieve size of serialized event data");
  }

  const absl::string_view data(reinterpret_cast<const char *>(data_ptr), size);
  return DeserializeCGEventData(data);
}

absl::StatusOr<std::string>
SerializeCGEventData(const CGEventData &event_data) {
  if (event_data.version != 2) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported DeserializedCGEventData version ", event_data.version));
  }

  absl::Cord result;
  WriteBE<int32_t>(result, event_data.version);

  for (const auto &[field_id, element] : event_data.fields) {
    WriteField(result, field_id, element);
  }

  return std::string(result);
}

namespace {

template <typename T>
absl::StatusOr<T>
ReadStruct(absl::string_view data,
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
absl::StatusOr<T>
ConsumeStruct(absl::string_view &data,
              std::optional<uint32_t> indicated_size = std::nullopt) {
  ASSIGN_OR_RETURN(T copied_struct, ReadStruct<T>(data, indicated_size));
  data = data.substr(sizeof(T));
  return copied_struct;
}

absl::StatusOr<IOHIDEventData> ConsumeIOHIDEventData(absl::string_view &data) {
  if (data.size() < sizeof(IOHIDEventBase)) {
    return absl::OutOfRangeError("Out of range");
  }

  const auto *event_data_header =
      reinterpret_cast<const IOHIDEventBase *>(data.data());

  switch (event_data_header->type) {
    using enum IOHIDEventType;
  case kIOHIDEventTypeVelocity: {
    return ConsumeStruct<IOHIDVelocityEventData>(data, event_data_header->size);
  }
  case kIOHIDEventTypeFluidTouchGesture: {
    return ConsumeStruct<IOHIDFluidTouchGestureData>(data,
                                                     event_data_header->size);
  }
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported IOHIDEventType: ",
                   static_cast<uint32_t>(event_data_header->type)));
}

} // namespace

absl::StatusOr<IOHIDSystemQueueElementData>
DeserializeIOHIDSystemQueueElementData(absl::string_view data) {
  IOHIDSystemQueueElementData result;

  ASSIGN_OR_RETURN(result.header, ConsumeStruct<IOHIDSystemQueueElement>(data));

  for (int i = 0; i < result.header.event_count; i++) {
    ASSIGN_OR_RETURN(IOHIDEventData event_data, ConsumeIOHIDEventData(data));
    result.events.push_back(event_data);
  }

  if (data.size() != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("When deserializing an IOHIDSystemQueueElement, had ",
                     data.size(), " bytes left over"));
  }

  return result;
}

std::string SerializeIOHIDSystemQueueElementData(
    const IOHIDSystemQueueElementData &element) {
  absl::Cord result;

  {
    absl::string_view bytes{reinterpret_cast<const char *>(&element.header),
                            sizeof(element.header)};
    result.Append(bytes);
  }

  for (auto i = 0; i < element.events.size(); i++) {
    std::visit(
        [&](const auto &event_data) {
          absl::string_view bytes{reinterpret_cast<const char *>(&event_data),
                                  sizeof(event_data)};
          result.Append(bytes);
        },
        element.events[i]);
  }

  return std::string(result);
}

absl::StatusOr<IOHIDSystemQueueElementData>
GenerateIOHIDSystemQueueElementDataFromCGEvent(
    const CGEventRef absl_nonnull event) {
  CHECK(event != nullptr);

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

  IOHIDSystemQueueElementData result{};

  result.events.push_back(IOHIDFluidTouchGestureData{
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
      .swipe_progress = DoubleToFixedFP1616(progress)});

  if (vel_x != 0.0 || vel_y != 0.0 || phase == kGestureEnded) {
    result.events.push_back(IOHIDVelocityEventData{
        .base = {.size = sizeof(IOHIDVelocityEventData),
                 .type = IOHIDEventType::kIOHIDEventTypeVelocity,
                 .options = 0,
                 .depth = 1,
                 .reserved = {0, 0, 0}},
        .velocity_x = DoubleToFixedFP1616(vel_x),
        .velocity_y = DoubleToFixedFP1616(vel_y),
        .velocity_z = 0});
  }

  uint64_t timestamp = CGEventGetTimestamp(event);
  if (timestamp == 0) {
    timestamp = mach_absolute_time();
  }

  result.header = {.timestamp = timestamp,
                   .sender_id = 0,
                   .options = 0,
                   .attribute_length = 0,
                   .event_count = static_cast<uint32_t>(result.events.size())};

  return result;
}

absl::StatusOr<CFUniquePtr<CGEventRef absl_nonnull>>
AugmentCGEvent(const CGEventRef absl_nonnull event,
               const IOHIDSystemQueueElementData &element) {
  CHECK(event != nullptr);

  ASSIGN_OR_RETURN(CGEventData event_data, DeserializeCGEventData(event));
  std::string serialized_io_hid_element =
      SerializeIOHIDSystemQueueElementData(element);
  event_data.fields[4205] = serialized_io_hid_element;
  ASSIGN_OR_RETURN(std::string serialized_event_data,
                   SerializeCGEventData(event_data));

  CFUniquePtr<CFDataRef> data_ref = WrapCFUnique(CFDataCreateWithBytesNoCopy(
      /*allocator=*/kCFAllocatorDefault,
      reinterpret_cast<const uint8_t *>(serialized_event_data.data()),
      serialized_event_data.size(), kCFAllocatorNull));
  auto result = WrapCFUnique(
      CGEventCreateFromData(/*allocator=*/kCFAllocatorDefault, data_ref.get()));
  if (result == nullptr) {
    return absl::InternalError("Failed to augment CGEvent");
  }

  return result;
}

absl::StatusOr<CFUniquePtr<CGEventRef absl_nonnull>>
AugmentCGEvent(const CGEventRef absl_nonnull event) {
  CHECK(event != nullptr);

  ASSIGN_OR_RETURN(const IOHIDSystemQueueElementData element,
                   GenerateIOHIDSystemQueueElementDataFromCGEvent(event));
  return AugmentCGEvent(event, element);
}

} // namespace fasterswiper
