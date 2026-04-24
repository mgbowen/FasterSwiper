#pragma once

#include <string>

#include "absl/strings/str_cat.h"

namespace fasterswiper {

inline std::string StatusOrToString(const auto &statusor) {
  if (statusor.ok()) {
    return absl::StrCat(*statusor);
  }

  return absl::StrCat("(", statusor.status(), ")");
}

inline std::string OptionalToString(const auto &opt) {
  if (opt) {
    return absl::StrCat(*opt);
  }

  return "(nullopt)";
}

} // namespace fasterswiper
