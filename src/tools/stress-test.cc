#include "src/easing-functions.h"
#include "src/event-tap-manager.h"
#include "src/space-state.h"
#include "src/swipe-animator.h"
#include <iostream>
#include <thread>

#include "absl/flags/parse.h"

using namespace fasterswiper;

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
    auto maybe_space_state = LoadSpaceStateForActiveDisplay();
    if (!maybe_space_state.ok()) {
      std::cerr << "Failed to load space state\n";
      return;
    }
    auto animator = std::make_unique<SwipeAnimator>(
        std::make_unique<SpaceSwitcher>(std::move(*maybe_space_state)));

    constexpr auto easing_function = kEasingFunctionLinear;
    constexpr auto duration = absl::Milliseconds(200);

    while (true) {
      auto future = animator->AnimateToPosition({
          .target_position = 4'000'000,
          .duration = duration,
          .easing_function = GetEasingFunction(easing_function),
          .ticks_per_second = 240,
      });
      future.wait();
      auto future2 = animator->AnimateToPosition({
          .target_position = 0'000'000,
          .duration = duration,
          .easing_function = GetEasingFunction(easing_function),
          .ticks_per_second = 240,
      });
      future2.wait();
    }
  });

  std::cout << "Running\n";
  while (true) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
  }
}
