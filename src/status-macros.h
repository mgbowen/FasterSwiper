//
// Based on code from
// https://github.com/protocolbuffers/protobuf/blob/main/src/google/protobuf/stubs/status_macros.h,
// with some improvements to allow ASSIGN_OR_RETURN to handle variable
// declarations.
//

#pragma once

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace fasterswiper {

// Run a command that returns a util::Status.  If the called code returns an
// error status, return that status up out of this method too.
//
// Example:
//   RETURN_IF_ERROR(DoThings(4));
#define RETURN_IF_ERROR(expr)                                                  \
  do {                                                                         \
    /* Using _status below to avoid capture problems if expr is "status". */   \
    const absl::Status _status = (expr);                                       \
    if (ABSL_PREDICT_FALSE(!_status.ok()))                                     \
      return _status;                                                          \
  } while (0)

// Internal helper for concatenating macro values.
#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y) STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr)                            \
  auto statusor = (rexpr);                                                     \
  if (ABSL_PREDICT_FALSE(!statusor.ok()))                                      \
    return statusor.status();                                                  \
  lhs = std::move(statusor).value()

// Executes an expression that returns a util::StatusOr, extracting its value
// into the variable defined by lhs (or returning on error).
//
// Example: Declaring a new variable
//   ASSIGN_OR_RETURN(int value, MaybeGetValue(arg));
//
// Example: Assigning to an existing value
//   ValueType value;
//   ASSIGN_OR_RETURN(value, MaybeGetValue(arg));
//
// WARNING: ASSIGN_OR_RETURN expands into multiple statements; it cannot be used
//  in a single statement (e.g. as the body of an if statement without {})!
#define ASSIGN_OR_RETURN(lhs, rexpr)                                           \
  ASSIGN_OR_RETURN_IMPL(                                                       \
      STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, rexpr)

} // namespace fasterswiper
