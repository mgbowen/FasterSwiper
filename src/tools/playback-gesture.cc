#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"
#include "src/cf-util.h"
#include "src/event.h"
#include "src/macos-private.h"

namespace fasterswiper {
namespace {

using nlohmann::json;

absl::Status CheckForAccessibilityPermissions() {
  const void *keys[] = {kAXTrustedCheckOptionPrompt};
  const void *values[] = {kCFBooleanTrue};
  const auto opts = WrapCFUnique(
      CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                         &kCFTypeDictionaryValueCallBacks));
  const bool ok = AXIsProcessTrustedWithOptions(opts.get());
  if (!ok) {
    return absl::PermissionDeniedError(
        "macOS accessibility permissions not granted. Please grant them in "
        "System Settings > Privacy & Security > Accessibility.");
  }

  return absl::OkStatus();
}

void PostEvent(const json &j) {
  auto dock = WrapCFUnique(CGEventCreate(NULL));
  if (!dock)
    return;

  CGEventSetIntegerValueField(dock.get(), kCGSEventTypeField,
                              static_cast<int64_t>(kCGSEventDockControl));
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureHIDType,
                              kIOHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(dock.get(), kCGEventGesturePhase,
                              j.value("phase", 0));
  CGEventSetIntegerValueField(dock.get(), kCGEventGestureSwipeMotion,
                              j.value("motion", 0));
  // CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeProgress,
  //                            j.value("progress", 0.0));
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityX,
                             j.value("velocity_x", 0.0));
  // CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityY,
  //                            j.value("velocity_y", 0.0));
  //   CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipePositionX,
  //                              j.value("position_x", 0.0));
  //   CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipePositionY,
  //                              j.value("position_y", 0.0));

  {
    double progress = j.value("progress", 0.0);
    float progress_float = progress;
    int32_t progress_bits = *reinterpret_cast<int32_t *>(&progress_float);
    CGEventSetIntegerValueField(dock.get(), kCGEventScrollGestureFlagBits,
                                progress_bits);
  }
  // CGEventSetDoubleValueField(dock.get(), kCGEventGestureZoomDeltaX,
  //                            j.value("zoom_delta_x", 0.0));
  // CGEventSetDoubleValueField(dock.get(), kCGEventGestureZoomDeltaY,
  //                            j.value("zoom_delta_y", 0.0));

  CGEventPost(kCGSessionEventTap, dock.get());
}

absl::Status PlaybackGestures(const std::string &input_path) {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  std::ifstream in(input_path);
  if (!in.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Could not open file for reading: ", input_path));
  }

  json root;
  try {
    in >> root;
  } catch (const json::parse_error &e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse JSON: ", e.what()));
  }

  if (!root.contains("events") || !root["events"].is_array()) {
    return absl::InvalidArgumentError("JSON missing 'events' array.");
  }

  auto events = root["events"];
  if (events.empty()) {
    std::cout << "No events to playback.\n";
    return absl::OkStatus();
  }

  std::cout << "Starting playback of " << events.size() << " events...\n";

  auto playback_start = std::chrono::steady_clock::now();

  for (const auto &j : events) {
    int64_t target_ns = j.value("timestamp_ns", 0LL);

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          now - playback_start)
                          .count();

    if (target_ns > elapsed_ns) {
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(target_ns - elapsed_ns));
    }

    PostEvent(j);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "Playback completed.\n";
  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input_json_path>\n";
    return 1;
  }

  std::string input_path = argv[1];
  if (absl::Status status = fasterswiper::PlaybackGestures(input_path); !status.ok()) {
    std::cerr << "ERROR: " << status << "\n";
    return 1;
  }

  return 0;
}
