#include "src/event.h"

#include "src/macos-private.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace fasterswiper {

std::optional<DockSwipeEvent> ParseDockSwipeEvent(CGEventRef event) {
  int et = CGEventGetIntegerValueField(event, kCGSEventTypeField);
  if (et != kCGSEventDockControl)
    return std::nullopt;
  if (CGEventGetIntegerValueField(event, kCGEventGestureHIDType) !=
      kIOHIDEventTypeDockSwipe)
    return std::nullopt;

  DockSwipeEventSource source = DockSwipeEventSource::kPhysical;
  if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) ==
      kSyntheticEventMagicNumber) {
    source = DockSwipeEventSource::kSynthetic;
  }

  return DockSwipeEvent{
      .phase = static_cast<int>(
          CGEventGetIntegerValueField(event, kCGEventGesturePhase)),
      .direction = static_cast<int>(
          CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion)),
      .progress =
          CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress),
      .source = source,
  };
}

std::string EventGesturePhaseToString(int phase) {
  switch (phase) {
  case kGestureBegan:
    return "kGestureBegan";
  case kGestureChanged:
    return "kGestureChanged";
  case kGestureEnded:
    return "kGestureEnded";
  case kGestureCancelled:
    return "kGestureCancelled";
  }

  return absl::StrCat("(unknown gesture phase ", phase, ")");
}

std::string CFEventToDebugString(CGEventRef event) {
  return absl::StrFormat(
      "CFEvent{phase=%s, progress=%f, velocity_x=%f, user_data=%lld}",
      EventGesturePhaseToString(
          CGEventGetIntegerValueField(event, kCGEventGesturePhase)),
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress),
      CGEventGetDoubleValueField(event, kCGEventGestureSwipeVelocityX),
      CGEventGetIntegerValueField(event, kCGEventSourceUserData));
}

} // namespace fasterswiper
