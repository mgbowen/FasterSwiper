#include "src/event-tap-manager.h"

#include <iostream>
#include <thread>

#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status_macros.h>
#include <absl/time/time.h>

namespace fasterswiper {
namespace {

void SendKeyWithControl(CGKeyCode key_code) {
  CFUniquePtr<CGEventSourceRef> source =
      WrapCFUnique(CGEventSourceCreate(kCGEventSourceStateHIDSystemState));

  CFUniquePtr<CGEventRef> down =
      WrapCFUnique(CGEventCreateKeyboardEvent(source.get(), key_code, true));
  CGEventSetFlags(down.get(), kCGEventFlagMaskControl);
  CGEventPost(kCGHIDEventTap, down.get());

  CFUniquePtr<CGEventRef> up =
      WrapCFUnique(CGEventCreateKeyboardEvent(source.get(), key_code, false));
  CGEventSetFlags(up.get(), kCGEventFlagMaskControl);
  CGEventPost(kCGHIDEventTap, up.get());
}

absl::Status Run() {
  EventTapManager::Callback callback = [](CGEventTapProxy proxy,
                                          CGEventType event_type,
                                          CGEventRef event) -> CGEventRef {
    if (event_type == kCGEventKeyDown) {
      auto keycode = static_cast<CGKeyCode>(
          CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
      if (keycode == 53) { // Escape key
        std::abort();
      }
    }

    return event;
  };

  ASSIGN_OR_RETURN(
      auto tap_manager,
      EventTapManager::Create(kCGSessionEventTap, kCGHeadInsertEventTap,
                              kCGEventTapOptionDefault, {kCGEventKeyDown},
                              std::move(callback)));

  CFUniquePtr<CFRunLoopSourceRef> run_loop_source =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));
  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source.get(),
                     kCFRunLoopCommonModes);
  tap_manager->SetEnabled(true);

  std::thread animator_thread([&] {
    constexpr auto sleep_duration = absl::Milliseconds(1200);

    while (true) {
      SendKeyWithControl(25); // Control+9
      absl::SleepFor(sleep_duration);
      SendKeyWithControl(18); // Control+1
      absl::SleepFor(sleep_duration);
    }
  });

  std::cout << "Running stress test sending Control+5 and Control+1.\n";
  std::cout << "Press Escape to stop.\n";
  while (true) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
  }

  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);
  QCHECK_OK(::fasterswiper::Run());
}
