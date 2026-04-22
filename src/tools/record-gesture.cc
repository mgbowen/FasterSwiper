#include <csignal>
#include <fstream>
#include <iostream>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"
#include "src/cf-util.h"
#include "src/event-tap-manager.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"

namespace fasterswiper {
namespace {

using nlohmann::json;

std::atomic<bool> g_stop_requested{false};

void SignalHandler(int signal) {
  if (signal == SIGINT) {
    g_stop_requested = true;
    CFRunLoopStop(CFRunLoopGetMain());
  }
}

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

json CaptureEvent(CGEventRef event, int64_t relative_timestamp_ns) {
  json j;
  j["timestamp_ns"] = relative_timestamp_ns;
  j["phase"] = static_cast<int>(
      CGEventGetIntegerValueField(event, kCGEventGesturePhase));
  j["motion"] = static_cast<int>(
      CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion));
  j["progress"] =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress);
  j["velocity_x"] =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX);
  j["velocity_y"] =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityY);
  j["position_x"] =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionX);
  j["position_y"] =
      CGEventGetDoubleValueField(event, kCGEventGestureSwipePositionY);
  j["scroll_gesture_flag_bits"] = static_cast<int64_t>(
      CGEventGetIntegerValueField(event, kCGEventScrollGestureFlagBits));
  j["zoom_delta_x"] =
      CGEventGetDoubleValueField(event, kCGEventGestureZoomDeltaX);
  j["zoom_delta_y"] =
      CGEventGetDoubleValueField(event, kCGEventGestureZoomDeltaY);
  return j;
}

absl::Status RecordGestures(const std::string &output_path) {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  std::vector<json> captured_events;
  std::optional<int64_t> first_event_time_ns;

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    // Filter for physical dock swipe events
    int et = CGEventGetIntegerValueField(event, kCGSEventTypeField);
    if (et != kCGSEventDockControl)
      return event;

    if (CGEventGetIntegerValueField(event, kCGEventGestureHIDType) !=
        kIOHIDEventTypeDockSwipe)
      return event;

    if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) ==
        kSyntheticEventMagicNumber) {
      return event;
    }

    int64_t now_ns = UptimeInNanoseconds();
    if (!first_event_time_ns) {
      first_event_time_ns = now_ns;
    }

    captured_events.push_back(
        CaptureEvent(event, now_ns - *first_event_time_ns));
    std::cout << "Captured event (phase="
              << CGEventGetIntegerValueField(event, kCGEventGesturePhase)
              << ", progress="
              << CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress)
              << ")\n";

    return event;
  };

  auto maybe_tap_manager = EventTapManager::Create(
      kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
      {kCGSEventDockControl}, callback);
  if (!maybe_tap_manager.ok()) {
    return maybe_tap_manager.status();
  }

  std::unique_ptr<EventTapManager> tap_manager = std::move(*maybe_tap_manager);
  CFUniquePtr<CFRunLoopSourceRef> src =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));
  CFRunLoopAddSource(CFRunLoopGetMain(), src.get(), kCFRunLoopCommonModes);
  tap_manager->SetEnabled(true);

  std::signal(SIGINT, SignalHandler);

  std::cout << "Recording... Perform gestures now. Press Ctrl+C to stop.\n";

  CFRunLoopRun();

  std::cout << "\nStopping and saving " << captured_events.size()
            << " events to " << output_path << "...\n";

  if (captured_events.empty()) {
    std::cout << "No events captured.\n";
    return absl::OkStatus();
  }

  json root;
  root["events"] = captured_events;

  std::ofstream out(output_path);
  if (!out.is_open()) {
    return absl::InternalError(
        absl::StrCat("Could not open file for writing: ", output_path));
  }
  out << root.dump(2) << std::endl;

  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <output_json_path>\n";
    return 1;
  }

  std::string output_path = argv[1];
  if (absl::Status status = fasterswiper::RecordGestures(output_path); !status.ok()) {
    std::cerr << "ERROR: " << status << "\n";
    return 1;
  }

  return 0;
}
