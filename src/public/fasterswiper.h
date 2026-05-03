#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum FS_EasingFunctionType {
  kEasingFunctionLinear = 0,
  kEasingFunctionEaseOutQuadratic = 1,
  kEasingFunctionEaseOutQuintic = 2,
  kEasingFunctionBezier = 3,
};

typedef struct {
  double p1x;
  double p1y;
  double p2x;
  double p2y;
} FS_BezierParameters;

typedef struct {
  int64_t animation_duration_per_space_ns;

  enum FS_EasingFunctionType easing_function_type;

  // Only used if `easing_function_type` is set to `kEasingFunctionBezier`.
  FS_BezierParameters easing_bezier_params;

  int64_t ticks_per_second;

  bool handle_keyboard_events;
} FS_Options;

typedef struct FS_Daemon FS_Daemon;

// Initializes the given FasterSwiperOptions struct with default values.
void FS_InitOptions(FS_Options *options);

// Creates a new FasterSwiper with the given options. Returns NULL on failure.
FS_Daemon *FS_Create(const FS_Options *options);

// Destroys the given FasterSwiper. Returns true on success, false otherwise.
bool FS_Destroy(FS_Daemon *state);

// Starts FasterSwiper. Returns true on success, false otherwise.
bool FS_Start(FS_Daemon *state);

// Stops FasterSwiper. Returns true on success, false otherwise.
bool FS_Stop(FS_Daemon *state);

// Parses command line flags from the given argc and argv.
void FS_ParseCommandLine(int argc, char **argv);

typedef struct {
  const char *version;
  const char *git_hash;
  bool is_dirty;
} FS_VersionInfo;

// Gets the version information for FasterSwiper.
void FS_GetVersionInfo(FS_VersionInfo *info);

#ifdef __cplusplus
}
#endif
