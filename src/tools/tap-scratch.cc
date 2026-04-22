#include "src/cf-util.h"

#include <iostream>
#include <mach/mach_time.h>
#include <optional>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"

namespace fasterswiper {
namespace {

using fasterswiper::CFUniquePtr;
using fasterswiper::WrapCFUnique;

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

absl::Status Run() {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    std::cout << "Received event\n";
    if (CGEventGetIntegerValueField(event, kCGEventGesturePhase) == 1) {
      return nullptr;
    }
    return event;
  };

  auto maybe_tap_manager =
      EventTapManager::Create(kCGSessionEventTap, kCGHeadInsertEventTap,
                              kCGEventTapOptionDefault, {30}, callback);
  if (!maybe_tap_manager.ok()) {
    return maybe_tap_manager.status();
  }

  std::unique_ptr<EventTapManager> tap_manager = std::move(*maybe_tap_manager);

  CFUniquePtr<CFRunLoopSourceRef> src =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));
  CFRunLoopAddSource(CFRunLoopGetMain(), src.get(), kCFRunLoopCommonModes);
  tap_manager->SetEnabled(true);

  std::cout << "Running\n";
  while (true) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
  }

  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main() {
  if (absl::Status status = fasterswiper::Run(); !status.ok()) {
    std::cerr << absl::StrCat("ERROR: ", status) << "\n";
    return 1;
  }

  return 0;
}
