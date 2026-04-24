#include "src/c-api.h"

#include "src/cf-util.h"
#include "src/event-tap-manager.h"
#include "src/gesture-controller.h"
#include "src/macos-private.h"

#include <cstring>
#include <iostream>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOTypes.h>

#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace {

using ::fasterswiper::CFUniquePtr;
using ::fasterswiper::EventTapManager;
using ::fasterswiper::GestureController;
using ::fasterswiper::kCGSEventDockControl;
using ::fasterswiper::WrapCFUnique;

} // namespace

extern "C" {

struct FasterSwiper {
  absl::Mutex mutex;

  std::unique_ptr<GestureController> gesture_controller;
  std::unique_ptr<EventTapManager> tap_manager;
  CFUniquePtr<CFRunLoopSourceRef> run_loop_source;

  bool is_running = false;
};

void InitializeFasterSwiperOptions(FasterSwiperOptions *options) {
  std::memset(options, 0, sizeof(FasterSwiperOptions));

  const GestureController::Options default_options;
  options->animation_duration_per_space_ns =
      absl::ToInt64Nanoseconds(default_options.animation_duration_per_space);
  options->easing_function_type = default_options.easing_function_type;
  options->ticks_per_second = default_options.ticks_per_second;
}

FasterSwiper *CreateFasterSwiper(const FasterSwiperOptions *options) {
  auto gesture_controller =
      std::make_unique<GestureController>(GestureController::Options{
          .animation_duration_per_space =
              absl::Nanoseconds(options->animation_duration_per_space_ns),
          .easing_function_type = options->easing_function_type,
          .ticks_per_second = options->ticks_per_second,
      });

  EventTapManager::Callback callback =
      [gesture_controller = gesture_controller.get()](
          CGEventTapProxy proxy, CGEventType event_type,
          CGEventRef event) -> CGEventRef {
    return gesture_controller->HandleEvent(proxy, event_type, event);
  };

  auto maybe_tap_manager = EventTapManager::Create(
      kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
      {kCGSEventDockControl}, std::move(callback));
  if (!maybe_tap_manager.ok()) {
    std::cerr << "Failed to create EventTapManager: "
              << maybe_tap_manager.status() << "\n";
    return nullptr;
  }

  std::unique_ptr<EventTapManager> tap_manager = std::move(*maybe_tap_manager);

  CFUniquePtr<CFRunLoopSourceRef> run_loop_source =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));

  return new FasterSwiper{
      .gesture_controller = std::move(gesture_controller),
      .tap_manager = std::move(tap_manager),
      .run_loop_source = std::move(run_loop_source),
  };
}

bool DestroyFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    return false;
  }

  delete state;
  return true;
}

bool StartFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    std::cerr << "StartFasterSwiper called with null state\n";
    return false;
  }

  absl::MutexLock lock(state->mutex);

  if (state->is_running) {
    std::cerr << "StartFasterSwiper called with already running state\n";
    return false;
  }

  CFRunLoopAddSource(CFRunLoopGetMain(), state->run_loop_source.get(),
                     kCFRunLoopCommonModes);
  state->tap_manager->SetEnabled(true);
  state->is_running = true;

  return true;
}

bool StopFasterSwiper(FasterSwiper *state) {
  if (state == nullptr) {
    std::cerr << "StopFasterSwiper called with null state\n";
    return false;
  }

  absl::MutexLock lock(state->mutex);

  if (!state->is_running) {
    return true;
  }

  state->tap_manager->SetEnabled(false);
  CFRunLoopRemoveSource(CFRunLoopGetMain(), state->run_loop_source.get(),
                        kCFRunLoopCommonModes);
  state->is_running = false;

  return true;
}

void ParseFasterSwiperCommandLine(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
}
}
