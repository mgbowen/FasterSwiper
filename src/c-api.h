#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "src/easing-functions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int64_t animation_duration_per_space_ns;

  enum EasingFunctionType easing_function_type;

  int64_t ticks_per_second;
} FasterSwiperOptions;

typedef struct FasterSwiper FasterSwiper;

// Initializes the given FasterSwiperOptions struct with default values.
void InitializeFasterSwiperOptions(FasterSwiperOptions *options);

// Creates a new FasterSwiper with the given options. Returns NULL on failure.
FasterSwiper *CreateFasterSwiper(const FasterSwiperOptions *options);

// Destroys the given FasterSwiper. Returns true on success, false otherwise.
bool DestroyFasterSwiper(FasterSwiper *state);

// Starts FasterSwiper. Returns true on success, false otherwise.
bool StartFasterSwiper(FasterSwiper *state);

// Stops FasterSwiper. Returns true on success, false otherwise.
bool StopFasterSwiper(FasterSwiper *state);

#ifdef __cplusplus
}
#endif
