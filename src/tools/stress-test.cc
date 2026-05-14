#include "src/event-tap-manager.h"

#include <iostream>
#include <thread>

#include "src/space-state.h"
#include <absl/flags/parse.h>
#include <absl/time/time.h>

using namespace fasterswiper;

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

} // namespace

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  EventTapManager::Callback callback = [](CGEventTapProxy proxy,
                                          CGEventType event_type,
                                          CGEventRef event) -> CGEventRef {
    if (event_type == kCGEventKeyDown) {
      CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(
          event, kCGKeyboardEventKeycode);
      if (keycode == 53) { // Escape key
        std::abort();
      }
    }

    return event;
  };

  auto maybe_tap_manager = EventTapManager::Create(
      kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
      {kCGEventKeyDown}, std::move(callback));
  if (!maybe_tap_manager.ok()) {
    std::cerr << "Failed to create tap manager\n";
    return 1;
  }
  auto tap_manager = std::move(*maybe_tap_manager);

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
}
