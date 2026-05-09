#include "src/cf-util.h"
#include "src/const.h"
#include "src/easing.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"
#include "src/space-state.h"
#include "src/swipe-animator.h"

#include <iostream>
#include <optional>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace fasterswiper {
namespace {

absl::Status CheckForAccessibilityPermissions() {
  const void *keys[] = {kAXTrustedCheckOptionPrompt};
  const void *values[] = {kCFBooleanTrue};
  const auto opts = WrapCFUnique(
      CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                         &kCFTypeDictionaryValueCallBacks));
  const bool ok = AXIsProcessTrustedWithOptions(opts.get());
  if (!ok) {
    return absl::PermissionDeniedError(
        "macOS accessibility permissions not granted.");
  }

  return absl::OkStatus();
}

std::optional<int> KeyCodeToDigit(CGKeyCode key_code) {
  switch (key_code) {
  case 18:
    return 1;
  case 19:
    return 2;
  case 20:
    return 3;
  case 21:
    return 4;
  case 23:
    return 5;
  case 22:
    return 6;
  case 26:
    return 7;
  case 28:
    return 8;
  case 25:
    return 9;
  default:
    return std::nullopt;
  }
}

absl::Status Run() {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  std::unique_ptr<SwipeAnimator> animator;

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    if (event_type != kCGEventKeyDown) {
      return event;
    }

    CGEventFlags flags = CGEventGetFlags(event);
    constexpr CGEventFlags kModifierMask =
        kCGEventFlagMaskAlphaShift | kCGEventFlagMaskShift |
        kCGEventFlagMaskControl | kCGEventFlagMaskAlternate |
        kCGEventFlagMaskCommand;

    if ((flags & kModifierMask) != kCGEventFlagMaskControl) {
      return event;
    }

    CGKeyCode key_code =
        (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    auto digit = KeyCodeToDigit(key_code);
    if (!digit) {
      return event;
    }

    // Refresh space state on every move.
    auto maybe_space_state = LoadSpaceStateForActiveDisplay();
    if (!maybe_space_state.ok()) {
      std::cerr << "Failed to load space state: " << maybe_space_state.status()
                << "\n";
      return nullptr;
    }

    // Recreating the animator ensures we have the latest space count and
    // current index. Note: If an animation was already running, this will
    // cancel it and start fresh from the system's current space.
    animator = std::make_unique<SwipeAnimator>(std::move(*maybe_space_state));

    const int target_space_index = *digit - 1;
    const int num_spaces = animator->space_state().count();

    if (target_space_index >= num_spaces) {
      std::cout << "Ignoring shortcut: target space " << *digit
                << " exceeds available spaces (" << num_spaces << ")\n";
      return nullptr;
    }

    int64_t target_position = target_space_index * kOneSwipeInNanoswipes;
    std::cout << "Animating to space " << *digit << " (position "
              << target_position << ")\n";

    (void)animator->AnimateToPosition({
        .target_position = target_position,
        .duration = absl::Milliseconds(250),
        .easing_function = MakeEasingFunctionEaseOutQuadratic(),
        .ticks_per_second = 240,
    });

    return nullptr; // Swallow the event
  };

  auto maybe_tap_manager = EventTapManager::Create(
      kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
      {kCGEventKeyDown}, callback);
  if (!maybe_tap_manager.ok()) {
    return maybe_tap_manager.status();
  }

  std::unique_ptr<EventTapManager> tap_manager = std::move(*maybe_tap_manager);

  CFUniquePtr<CFRunLoopSourceRef> src =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));
  CFRunLoopAddSource(CFRunLoopGetMain(), src.get(), kCFRunLoopCommonModes);
  tap_manager->SetEnabled(true);

  std::cout
      << "Space hotkeys tool running. Press Control+1-9 to switch spaces.\n";
  CFRunLoopRun();

  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper
int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  if (absl::Status status = fasterswiper::Run(); !status.ok()) {
    std::cerr << absl::StrCat("ERROR: ", status) << "\n";
    return 1;
  }

  return 0;
}
