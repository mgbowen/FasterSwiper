#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FS_DaemonOptions FS_DaemonOptions;

bool FS_LoadDefaultDaemonOptions(FS_DaemonOptions **out_daemon_options);

bool FS_HydrateDaemonOptions(FS_DaemonOptions *daemon_options);

bool FS_LoadDaemonOptionsFromBinaryProto(const char *data, size_t len,
                                         FS_DaemonOptions **out_daemon_options);

bool FS_SaveDaemonOptionsToBinaryProto(const FS_DaemonOptions *daemon_options,
                                       char *out_data, size_t *out_len);

typedef struct FS_Daemon FS_Daemon;

// Creates a new FasterSwiper with the given options. On success, ownership of
// the passed FS_DaemonOptions is transferred to FS_Daemon. On failure, returns
// NULL.
FS_Daemon *FS_Create(FS_DaemonOptions *options);

// Destroys the given FasterSwiper. Returns true on success, false otherwise.
bool FS_Destroy(FS_Daemon *state);

bool FS_DestroyDaemonOptions(FS_DaemonOptions *options);

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
