#include "src/c-api.h"

#include <pthread/qos.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <CoreFoundation/CFRunLoop.h>
#include <sys/qos.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"

enum class EasingFunction {
  kLinear,
  kEaseOutQuadratic,
  kEaseOutQuintic,
};

constexpr absl::string_view kLinearStr = "linear";
constexpr absl::string_view kEaseOutQuadraticStr = "ease_out_quadratic";
constexpr absl::string_view kEaseOutQuinticStr = "ease_out_quintic";

bool AbslParseFlag(absl::string_view text, EasingFunction *easing_function,
                   std::string *error) {
  if (text == kLinearStr) {
    *easing_function = EasingFunction::kLinear;
    return true;
  }

  if (text == kEaseOutQuadraticStr) {
    *easing_function = EasingFunction::kEaseOutQuadratic;
    return true;
  }

  if (text == kEaseOutQuinticStr) {
    *easing_function = EasingFunction::kEaseOutQuintic;
    return true;
  }

  *error = absl::StrCat("unknown EasingFunction \"", text, "\"");
  return false;
}

std::string AbslUnparseFlag(EasingFunction easing_function) {
  switch (easing_function) {
  case EasingFunction::kLinear:
    return std::string(kLinearStr);
  case EasingFunction::kEaseOutQuadratic:
    return std::string(kEaseOutQuadraticStr);
  case EasingFunction::kEaseOutQuintic:
    return std::string(kEaseOutQuinticStr);
  default:
    return absl::StrCat(easing_function);
  }
}

ABSL_FLAG(std::optional<int64_t>, animation_speed_per_space, std::nullopt,
          "Animation speed in milliseconds per space");
ABSL_FLAG(std::optional<EasingFunction>, easing_function, std::nullopt,
          "Easing function");

static FasterSwiper *state = NULL;

void handle_sigint(int sig) {
  printf("\nExiting...\n");
  CFRunLoopStop(CFRunLoopGetMain());
}

int main(int argc, char *argv[]) {
  absl::ParseCommandLine(argc, argv);

  signal(SIGINT, handle_sigint);

  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

  FasterSwiperOptions options;
  InitializeFasterSwiperOptions(&options);

  const std::optional<int64_t> maybe_animation_speed_per_space =
      absl::GetFlag(FLAGS_animation_speed_per_space);
  if (maybe_animation_speed_per_space.has_value()) {
    options.animation_duration_per_space_ns = *maybe_animation_speed_per_space * 1000000;
  }

  const std::optional<EasingFunction> maybe_easing_function =
      absl::GetFlag(FLAGS_easing_function);
  if (maybe_easing_function.has_value()) {
    switch (*maybe_easing_function) {
    case EasingFunction::kLinear:
      options.easing_function_type = kEasingFunctionLinear;
      break;
    case EasingFunction::kEaseOutQuadratic:
      options.easing_function_type = kEasingFunctionEaseOutQuadratic;
      break;
    case EasingFunction::kEaseOutQuintic:
      options.easing_function_type = kEasingFunctionEaseOutQuintic;
      break;
    }
  }

  state = CreateFasterSwiper(&options);
  if (state == NULL) {
    return 1;
  }

  printf("Running...\n");
  if (!StartFasterSwiper(state)) {
    fprintf(stderr, "Failed to start FasterSwiper\n");
    return 1;
  }

  while (true) {
    const CFRunLoopRunResult run_result =
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    if (run_result == kCFRunLoopRunStopped) {
      break;
    }
  }

  if (!StopFasterSwiper(state)) {
    fprintf(stderr, "Failed to stop FasterSwiper\n");
    abort();
  }

  if (!DestroyFasterSwiper(state)) {
    fprintf(stderr, "Failed to destroy FasterSwiper\n");
    return 1;
  }

  return 0;
}
