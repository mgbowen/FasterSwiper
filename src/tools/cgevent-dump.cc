#include <bit>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "src/gesture-serialization.h"
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/strings/escaping.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <nlohmann/json.hpp>

ABSL_FLAG(std::string, input_json, "", "Path to JSON file containing events");
ABSL_FLAG(std::string, input_binary, "",
          "Path to binary file containing raw CGEvent data");

namespace {

// Helper to handle std::visit overloaded lambdas
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

std::string GetFieldName(uint16_t field_id) {
  switch (field_id) {
  case 55:
    return "kCGSEventTypeField";
  case 110:
    return "kCGEventGestureHIDType";
  case 115:
    return "kCGEventGestureSwipeMask";
  case 119:
    return "kCGEventGestureScrollY";
  case 123:
    return "kCGEventGestureSwipeMotion";
  case 124:
    return "kCGEventGestureSwipeProgress";
  case 125:
    return "kCGEventGestureSwipePositionX";
  case 126:
    return "kCGEventGestureSwipePositionY";
  case 129:
    return "kCGEventGestureSwipeVelocityX";
  case 130:
    return "kCGEventGestureSwipeVelocityY";
  case 132:
    return "kCGEventGesturePhase";
  case 135:
    return "kCGEventScrollGestureFlagBits";
  case 139:
    return "kCGEventGestureZoomDeltaX";
  case 140:
    return "kCGEventGestureZoomDeltaY";
  case 4205:
    return "kCGEventGesturePayload (4205)";
  case 4502:
    return "kCGEventGesturePayload (4502)";
  default:
    return "(unknown)";
  }
}

std::string GetPhaseName(int64_t phase) {
  switch (phase) {
  case 1:
    return "Began";
  case 2:
    return "Changed";
  case 4:
    return "Ended";
  case 8:
    return "Cancelled";
  default:
    return "Unknown";
  }
}

std::string GetMotionName(int64_t motion) {
  switch (motion) {
  case 1:
    return "Horizontal";
  case 2:
    return "Vertical";
  default:
    return "Unknown";
  }
}

std::string FormatIOHIDSystemQueueElement(
    const fasterswiper::IOHIDSystemQueueElementData &element) {
  std::string s;
  absl::StrAppendFormat(&s, "timestamp: %u\n", element.header.timestamp);
  absl::StrAppendFormat(&s, "sender_id: 0x%x\n", element.header.sender_id);
  absl::StrAppendFormat(&s, "options: %u\n", element.header.options);
  absl::StrAppendFormat(&s, "attribute_length: %u\n",
                        element.header.attribute_length);
  absl::StrAppendFormat(&s, "event_count: %u", element.header.event_count);

  for (size_t i = 0; i < element.events.size(); ++i) {
    absl::StrAppend(&s, "\n");
    std::visit(
        overloaded{
            [&](const fasterswiper::IOHIDFluidTouchGestureData &ev) {
              absl::StrAppendFormat(&s, "event[%u]: FluidTouchGesture\n", i);
              absl::StrAppendFormat(
                  &s, "  size: %u, type: %u, options/phase: 0x%x (%s)\n",
                  ev.base.size, static_cast<uint32_t>(ev.base.type),
                  ev.base.options,
                  GetPhaseName((ev.base.options >> 24) & 0xFF));
              absl::StrAppendFormat(
                  &s, "  position: (%f, %f, %f)\n",
                  static_cast<double>(ev.position_x) / 65536.0,
                  static_cast<double>(ev.position_y) / 65536.0,
                  static_cast<double>(ev.position_z) / 65536.0);
              absl::StrAppendFormat(&s, "  swipe_mask: 0x%x\n",
                                    static_cast<uint32_t>(ev.swipe_mask));
              absl::StrAppendFormat(
                  &s, "  gesture_motion: %u (%s)\n",
                  static_cast<uint32_t>(ev.gesture_motion),
                  GetMotionName(static_cast<int64_t>(ev.gesture_motion)));
              absl::StrAppendFormat(&s, "  gesture_flavor: %u\n",
                                    static_cast<uint32_t>(ev.gesture_flavor));
              absl::StrAppendFormat(&s, "  swipe_progress: %f",
                                    static_cast<double>(ev.swipe_progress) /
                                        65536.0);
            },
            [&](const fasterswiper::IOHIDVelocityEventData &ev) {
              absl::StrAppendFormat(&s, "event[%u]: Velocity\n", i);
              absl::StrAppendFormat(
                  &s, "  size: %u, type: %u, options: 0x%x\n", ev.base.size,
                  static_cast<uint32_t>(ev.base.type), ev.base.options);
              absl::StrAppendFormat(
                  &s, "  velocity: (%f, %f, %f)",
                  static_cast<double>(ev.velocity_x) / 65536.0,
                  static_cast<double>(ev.velocity_y) / 65536.0,
                  static_cast<double>(ev.velocity_z) / 65536.0);
            }},
        element.events[i]);
  }
  return s;
}

void PrintTable(
    int32_t version,
    const absl::btree_map<uint16_t, fasterswiper::CGEventDataElement> &fields) {
  std::cout << "CGEvent Version: " << version << "\n";
  std::cout << "+------------+-------------------------------+--------------+--"
               "--------------------------------------------------\n";
  std::cout << "| Field ID   | Field Name                    | Type         | "
               "Value\n";
  std::cout << "+------------+-------------------------------+--------------+--"
               "--------------------------------------------------\n";

  for (const auto &[field_id, value_variant] : fields) {
    std::string name = GetFieldName(field_id);
    std::string type;
    std::string val_str;

    std::visit(
        overloaded{
            [&](int32_t v) {
              type = "int32_t";
              if (field_id == 132) {
                val_str =
                    absl::StrFormat("%d (0x%x) (%s)", v, v, GetPhaseName(v));
              } else if (field_id == 123) {
                val_str =
                    absl::StrFormat("%d (0x%x) (%s)", v, v, GetMotionName(v));
              } else {
                val_str = absl::StrFormat("%d (0x%x)", v, v);
              }
            },
            [&](int64_t v) {
              type = "int64_t";
              if (field_id == 132) {
                val_str = absl::StrFormat("%lld (0x%llx) (%s)", v, v,
                                          GetPhaseName(v));
              } else if (field_id == 123) {
                val_str = absl::StrFormat("%lld (0x%llx) (%s)", v, v,
                                          GetMotionName(v));
              } else {
                val_str = absl::StrFormat("%lld (0x%llx)", v, v);
              }
            },
            [&](float v) {
              type = "float";
              val_str =
                  absl::StrFormat("%f (0x%08x)", v, std::bit_cast<uint32_t>(v));
            },
            [&](double v) {
              type = "double";
              val_str = absl::StrFormat("%f (0x%016x)", v,
                                        std::bit_cast<uint64_t>(v));
            },
            [&](const std::string &v) {
              type = "std::string";
              if (field_id == 4205 || field_id == 4502) {
                auto decoded_or =
                    fasterswiper::DeserializeIOHIDSystemQueueElementData(v);
                if (decoded_or.ok()) {
                  val_str = FormatIOHIDSystemQueueElement(*decoded_or);
                  return;
                }
              }
              bool printable = true;
              for (char c : v) {
                if (!std::isprint(static_cast<unsigned char>(c))) {
                  printable = false;
                  break;
                }
              }
              if (printable && !v.empty()) {
                val_str = absl::StrCat("\"", v, "\"");
              } else {
                val_str = absl::StrCat("[", v.size(), " bytes] ",
                                       absl::BytesToHexString(v));
              }
            }},
        value_variant);

    std::vector<absl::string_view> lines = absl::StrSplit(val_str, '\n');
    if (lines.empty()) {
      absl::PrintF("| %-10d | %-29s | %-12s | \n", field_id, name, type);
    } else {
      absl::PrintF("| %-10d | %-29s | %-12s | %s\n", field_id, name, type,
                   lines[0]);
      for (size_t i = 1; i < lines.size(); ++i) {
        absl::PrintF(
            "|            |                               |              | "
            "%s\n",
            lines[i]);
      }
    }
  }
  std::cout << "+------------+-------------------------------+--------------+--"
               "--------------------------------------------------\n";
}

} // namespace

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);

  std::string input_json_path = absl::GetFlag(FLAGS_input_json);
  std::string input_binary_path = absl::GetFlag(FLAGS_input_binary);

  if (input_json_path.empty() && input_binary_path.empty()) {
    std::cerr
        << "Error: Either --input_json or --input_binary must be specified.\n";
    return 1;
  }
  if (!input_json_path.empty() && !input_binary_path.empty()) {
    std::cerr << "Error: Only one of --input_json or --input_binary should be "
                 "specified.\n";
    return 1;
  }

  if (!input_json_path.empty()) {
    std::ifstream in(input_json_path);
    if (!in.is_open()) {
      std::cerr << "Error: Could not open JSON file: " << input_json_path
                << "\n";
      return 1;
    }

    nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
    if (root.is_discarded()) {
      std::cerr << "Error: Failed to parse JSON file as valid JSON.\n";
      return 1;
    }
    if (!root.contains("events") || !root["events"].is_array()) {
      std::cerr << "Error: JSON must contain an 'events' array.\n";
      return 1;
    }

    const auto &events = root["events"];
    for (size_t i = 0; i < events.size(); ++i) {
      const auto &event = events[i];
      if (!event.contains("data") || !event["data"].is_string()) {
        std::cerr << "Warning: Event " << i
                  << " is missing 'data' string field. Skipping.\n";
        continue;
      }
      std::string base64_data = event["data"];
      std::string raw_bytes_str;
      if (!absl::Base64Unescape(base64_data, &raw_bytes_str)) {
        std::cerr << "Error: Failed to base64-decode data for event " << i
                  << "\n";
        return 1;
      }

      auto deserialized_or =
          fasterswiper::DeserializeCGEventData(raw_bytes_str);
      if (!deserialized_or.ok()) {
        std::cerr << "Error: Failed to deserialize event " << i << ": "
                  << deserialized_or.status().ToString() << "\n";
        return 1;
      }

      std::cout << "Event #" << i;
      if (event.contains("delta_ns") && event["delta_ns"].is_number()) {
        std::cout << " (delta_ns: " << event["delta_ns"].get<int64_t>() << ")";
      }
      std::cout << ":\n";
      PrintTable(deserialized_or->version, deserialized_or->fields);
      std::cout << "\n";
    }
  } else {
    std::ifstream in(input_binary_path, std::ios::binary);
    if (!in.is_open()) {
      std::cerr << "Error: Could not open binary file: " << input_binary_path
                << "\n";
      return 1;
    }

    std::string raw_bytes_str((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());

    auto deserialized_or = fasterswiper::DeserializeCGEventData(raw_bytes_str);
    if (!deserialized_or.ok()) {
      std::cerr << "Error: Failed to deserialize binary event data: "
                << deserialized_or.status().ToString() << "\n";
      return 1;
    }

    std::cout << "Binary Event:\n";
    PrintTable(deserialized_or->version, deserialized_or->fields);
  }

  return 0;
}
