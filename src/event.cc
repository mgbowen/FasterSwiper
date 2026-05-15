#include "src/event.h"

#include "src/macos-private.h"

#include <CoreGraphics/CGEventTypes.h>
#include <unistd.h>

#include "src/string-util.h"
#include <absl/base/no_destructor.h>
#include <absl/cleanup/cleanup.h>
#include <absl/log/log.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

namespace fasterswiper {

namespace {

// Used for recognizing physical vs. synthetic events. This won't work if we
// ever need to fork, but I don't anticipate needing to do that.
const absl::NoDestructor<pid_t> kOwnPid([] { return getpid(); }());

std::optional<DockControlEvent> ParseDockSwipeEvent(CGEventRef event) {
  if (CGEventGetIntegerValueField(event, kCGEventGestureHIDType) !=
      kIOHIDEventTypeDockSwipe) {
    return std::nullopt;
  }

  return DockControlEvent{
      .phase = static_cast<int>(
          CGEventGetIntegerValueField(event, kCGEventGesturePhase)),
      .direction = static_cast<int>(
          CGEventGetIntegerValueField(event, kCGEventGestureSwipeMotion)),
      .progress =
          CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress),
  };
}

std::optional<KeyEvent> ParseKeyEvent(CGEventRef event,
                                      CGEventType event_type) {
  const CGKeyCode key_code = static_cast<CGKeyCode>(
      CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
  const CGEventFlags modifiers = CGEventGetFlags(event);

  VLOG(1) << "ParseKeyEvent(): key_code=" << key_code
          << ", modifiers=" << modifiers;

  KeyState key_state;
  switch (event_type) {
  case kCGEventKeyDown:
    key_state = KeyState::kDown;
    break;
  case kCGEventKeyUp:
    key_state = KeyState::kUp;
    break;
  default:
    return std::nullopt;
  }

  return KeyEvent{
      .key_code = key_code, .modifiers = modifiers, .key_state = key_state};
}

} // namespace

bool KeyEvent::ConcernsHotkey(const Hotkey &hotkey) const {
  const auto adjusted_modifiers = modifiers & kModifierKeyMask;
  return hotkey.enabled && key_code == hotkey.key_code &&
         adjusted_modifiers == hotkey.modifiers;
}

bool KeyEvent::ConcernsAnyHotkey(
    const HotkeyConfigurations &hotkey_configs) const {
  return ConcernsHotkey(hotkey_configs.move_space_left) ||
         ConcernsHotkey(hotkey_configs.move_space_right) ||
         ConcernsHotkey(hotkey_configs.open_mission_control) ||
         ConcernsHotkey(hotkey_configs.open_app_expose);
}

std::optional<Event> ParseEvent(CGEventRef event) {
  VLOG(1) << "ParseEvent(): BEGIN";
  auto cleanup = absl::MakeCleanup([] { VLOG(1) << "ParseEvent(): END"; });

  const auto event_type = static_cast<CGEventType>(
      CGEventGetIntegerValueField(event, kCGSEventTypeField));
  VLOG(1) << "ParseEvent(): event_type=" << event_type;

  std::optional<EventData> event_data;
  switch (event_type) {
  case kCGSEventDockControl:
    event_data = ParseDockSwipeEvent(event);
    VLOG(1) << "ParseEvent(): ParseDockSwipeEvent result="
            << OptionalToString(event_data);
    break;
  case kCGEventKeyDown:
  case kCGEventKeyUp:
    event_data = ParseKeyEvent(event, event_type);
    VLOG(1) << "ParseEvent(): ParseKeyEvent result="
            << OptionalToString(event_data);
    break;
  default:
    break;
  }

  if (!event_data.has_value()) {
    return std::nullopt;
  }

  EventSource source = EventSource::kPhysical;
  if (CGEventGetIntegerValueField(event, kCGEventSourceUnixProcessID) ==
      *kOwnPid) {
    source = EventSource::kSynthetic;
  }

  return Event{
      .data = *std::move(event_data),
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

namespace {

std::string EventDoubleToString(double val) {
  if (val > 0 && val < 0.000'001) {
    return absl::StrFormat("<%f", val);
  }

  if (val < 0 && val > -0.000'001) {
    return absl::StrFormat(">%f", val);
  }

  return absl::StrFormat("%f", val);
}

} // namespace

std::string CFEventToDebugString(CGEventRef event) {

  return absl::StrFormat("CFEvent{phase=%s, progress=%s, velocity_x=%s}",
                         EventGesturePhaseToString(CGEventGetIntegerValueField(
                             event, kCGEventGesturePhase)),
                         EventDoubleToString(CGEventGetDoubleValueField(
                             event, kCGEventGestureSwipeProgress)),
                         EventDoubleToString(CGEventGetDoubleValueField(
                             event, kCGEventGestureSwipeVelocityX)));
}

CFUniquePtr<CGEventRef>
CreateDockControlGestureEvent(int phase, int direction, double progress,
                              std::optional<double> velocity) {
  auto event = WrapCFUnique(CGEventCreate(NULL));
  if (!event) {
    LOG(FATAL) << "CGEventCreate() return nullptr";
  }

  CGEventSetIntegerValueField(event.get(), kCGSEventTypeField,
                              static_cast<int64_t>(kCGSEventDockControl));
  CGEventSetIntegerValueField(event.get(), kCGEventGestureHIDType,
                              kIOHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(event.get(), kCGEventGesturePhase, phase);
  CGEventSetIntegerValueField(event.get(), kCGEventGestureSwipeMotion,
                              direction);
  CGEventSetDoubleValueField(event.get(), kCGEventGestureSwipeProgress,
                             progress);

  if (velocity.has_value()) {
    CGEventSetDoubleValueField(event.get(), kCGEventGestureSwipeVelocityX,
                               *velocity);
  }

  return event;
}

} // namespace fasterswiper
