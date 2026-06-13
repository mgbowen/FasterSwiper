#include "src/cf-util.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"
#include "src/periodic-timer.h"
#include "src/tools/util/accessibility-check.h"

#include <csignal>
#include <fstream>
#include <iostream>
#include <vector>

#include <absl/base/no_destructor.h>
#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/strings/escaping.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <nlohmann/json.hpp>

namespace fasterswiper {
namespace {

using ::nlohmann::json;

const absl::NoDestructor<pid_t> kOwnPid([] { return getpid(); }());

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> stop_requested{false};

void SignalHandler(int signal) {
  if (signal == SIGINT) {
    stop_requested = true;
    CFRunLoopStop(CFRunLoopGetMain());
  }
}

absl::Status RecordGestures(const std::string &output_path) {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  std::vector<json> captured_events;
  std::optional<int64_t> last_event_ns;

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    int et = CGEventGetIntegerValueField(event, kCGSEventTypeField);
    if (et != kCGSEventDockControl) {
      return event;
    }

    if (CGEventGetIntegerValueField(event, kCGEventGestureHIDType) !=
        kIOHIDEventTypeDockSwipe) {
      return event;
    }

    const int64_t now_ns = UptimeInNanoseconds();
    int64_t delta_ns = 0;
    if (last_event_ns.has_value()) {
      delta_ns = now_ns - *last_event_ns;
    }

    last_event_ns = now_ns;

    auto event_data = WrapCFUnique(CGEventCreateData(nullptr, event));

    const auto buffer_length = CFDataGetLength(event_data.get());
    std::vector<uint8_t> buffer;
    buffer.resize(buffer_length);
    CFDataGetBytes(event_data.get(),
                   CFRangeMake(0, CFDataGetLength(event_data.get())),
                   buffer.data());

    absl::string_view view(reinterpret_cast<const char *>(buffer.data()),
                           buffer.size());

    {
      json j;
      j["delta_ns"] = delta_ns;
      j["data"] = absl::Base64Escape(view);
      captured_events.push_back(std::move(j));
    }

    std::cout << "Captured event (phase="
              << CGEventGetIntegerValueField(event, kCGEventGesturePhase)
              << ", progress="
              << CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress)
              << ")\n";

    return event;
  };

  ASSIGN_OR_RETURN(auto tap_manager,
                   EventTapManager::Create(kCGSessionEventTap,
                                           kCGHeadInsertEventTap,
                                           kCGEventTapOptionDefault,
                                           {kCGSEventDockControl}, callback));
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
  out << root.dump(2) << "\n";

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
  QCHECK_OK(fasterswiper::RecordGestures(output_path));
  return 0;
}
