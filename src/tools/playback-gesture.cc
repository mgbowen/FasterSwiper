#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "nlohmann/json.hpp"
#include "src/cf-util.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include <absl/status/status.h>
#include <absl/strings/str_cat.h>

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
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeProgress,
                             j.value("progress", 0.0));
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityX,
                             j.value("velocity_x", 0.0));
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipeVelocityY,
                             j.value("velocity_y", 0.0));
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipePositionX,
                             j.value("position_x", 0.0));
  CGEventSetDoubleValueField(dock.get(), kCGEventGestureSwipePositionY,
                             j.value("position_y", 0.0));

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

  const int64_t playback_start_ns = UptimeInNanoseconds();
  int64_t cumulative_delta_ns = 0;

  for (const auto &j : events) {
    const int64_t delta_ns = j["delta_ns"];
    cumulative_delta_ns += delta_ns;

    const int64_t now_ns = UptimeInNanoseconds();
    auto target_ns = playback_start_ns + cumulative_delta_ns;

    if (target_ns > now_ns) {
      std::this_thread::sleep_for(std::chrono::nanoseconds(target_ns - now_ns));
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
  if (absl::Status status = fasterswiper::PlaybackGestures(input_path);
      !status.ok()) {
    std::cerr << "ERROR: " << status << "\n";
    return 1;
  }

  return 0;
}
