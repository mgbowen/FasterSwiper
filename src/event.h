#pragma once

#include <optional>
#include <string>
#include <variant>

#include <ApplicationServices/ApplicationServices.h>

#include "absl/strings/str_format.h"
#include "src/string-util.h"
#include "src/variant-util.h"

namespace fasterswiper {

enum class EventSource {
  kPhysical,
  kSynthetic,
};

inline constexpr absl::string_view EventSourceToString(EventSource source) {
  switch (source) {
    using enum EventSource;
  case kPhysical:
    return "kPhysical";
  case kSynthetic:
    return "kSynthetic";
  }

  return "(unknown)";
}

struct DockControlEvent {
  int phase;
  int direction;
  double progress;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const DockControlEvent &event) {
    absl::Format(&sink, "DockControlEvent{phase=%d, direction=%d, progress=%f}",
                 event.phase, event.direction, event.progress);
  }
};

enum class KeyState {
  kUp,
  kDown,
};

inline constexpr absl::string_view KeyStateToString(KeyState key_state) {
  switch (key_state) {
    using enum KeyState;
  case kUp:
    return "kUp";
  case kDown:
    return "kDown";
  }

  return "(unknown)";
}

struct KeyEvent {
  CGKeyCode key_code = 0;
  CGEventFlags flags = 0;
  KeyState key_state = KeyState::kUp;

  bool IsModifierDown(CGEventFlags flag) const {
    return (flags & flag) == flag;
  }

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const KeyEvent &event) {
    absl::Format(&sink, "KeyEvent{key_code=%d, flags=%d, key_state=%s}",
                 event.key_code, event.flags,
                 KeyStateToString(event.key_state));
  }
};

using EventData = std::variant<DockControlEvent, KeyEvent>;

template <typename Sink>
void AbslStringify(Sink &sink, const EventData &event_data) {
  std::visit(overloaded{[&](auto &data) { AbslStringify(sink, data); }},
             event_data);
}

struct Event {
  EventData data;
  EventSource source;

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const Event &event) {
    absl::Format(&sink, "Event{data=%s, source=%s}",
                 OptionalToString(event.data),
                 EventSourceToString(event.source));
  }
};

std::optional<Event> ParseEvent(CGEventRef event);

std::string EventGesturePhaseToString(int phase);

std::string CFEventToDebugString(CGEventRef event);

} // namespace fasterswiper
