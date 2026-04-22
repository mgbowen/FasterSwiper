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

std::optional<std::pair<std::string, std::string>>
TryParseField(CGEventRef event, int raw_field_id) {
  auto field = (CGEventField)raw_field_id;

  switch (raw_field_id) {
  case 123:
    return std::make_pair(
        "kCGEventGestureSwipeMotion",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 124:
    return std::make_pair(
        "kCGEventGestureSwipeProgress",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 125:
    return std::make_pair(
        "kCGEventGestureSwipePositionX",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 126:
    return std::make_pair(
        "kCGEventGestureSwipePositionY",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 129:
    return std::make_pair(
        "kCGEventGestureSwipeVelocityX",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 130:
    return std::make_pair(
        "kCGEventGestureSwipeVelocityY",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 135: {
    int64_t val = CGEventGetIntegerValueField(event, field);
    // val >>= 8;
    // std::bitset<32> bits(val);
    float *f = (float *)&val;
    return std::make_pair("kCGEventScrollGestureFlagBits", absl::StrCat(*f));
  }
  case 139:
    return std::make_pair(
        "kCGEventGestureZoomDeltaX",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 140:
    return std::make_pair(
        "kCGEventGestureZoomDeltaY",
        absl::StrCat(CGEventGetDoubleValueField(event, field)));
  case 169:
    return std::make_pair(
        "(unknown 169)",
        absl::StrCat(CGEventGetIntegerValueField(event, field)));
  }

  double double_value = CGEventGetDoubleValueField(event, field);
  int64_t integer_value = CGEventGetIntegerValueField(event, field);
  if (double_value == 0.0 && integer_value == 0) {
    return std::nullopt;
  }

  if (double_value != 0.0) {
    return std::make_pair(absl::StrCat("(unknown field ", raw_field_id, ")"),
                          absl::StrCat(double_value));
  }

  return std::make_pair(absl::StrCat("(unknown field ", raw_field_id, ")"),
                        absl::StrCat(integer_value));
}

absl::Status Run() {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    std::cout << "New event:\n";
    for (int i = 0; i < 1024; i++) {
      auto maybe_parsed_field = TryParseField(event, i);
      if (maybe_parsed_field) {
        auto [key, value] = *maybe_parsed_field;
        std::cout << "  " << key << ": " << value << "\n";
      }
    }
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
