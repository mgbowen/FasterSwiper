#pragma once

#include <type_traits>

#include <absl/log/log.h>
#include <magic_enum/magic_enum.hpp>

namespace fasterswiper {

template <typename T> [[noreturn]] void AbortOnUnknownEnum(T unknown_enum) {
  LOG(FATAL) << "Unknown value for enum " << magic_enum::enum_type_name<T>()
             << ": " << static_cast<std::underlying_type_t<T>>(unknown_enum);
}

} // namespace fasterswiper
