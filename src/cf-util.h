#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGEventTypes.h>

#include <memory>
#include <string>
#include <type_traits>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"

namespace fasterswiper {

template <typename RefT> struct CFDeleter {
  void operator()(const RefT ptr) { CFRelease(ptr); }
};

template <typename RefT>
using CFUniquePtr =
    std::unique_ptr<std::remove_pointer_t<RefT>, CFDeleter<RefT>>;

template <typename T> CFUniquePtr<T> WrapCFUnique(T ptr) {
  return CFUniquePtr<T>(ptr);
}

template <typename RefT>
using CFSharedPtr = std::shared_ptr<std::remove_pointer_t<RefT>>;

template <typename RefT> CFSharedPtr<RefT> WrapCFShared(RefT ptr) {
  return CFSharedPtr<RefT>(ptr, CFDeleter<RefT>());
}

absl::StatusOr<std::string>
StringFromCFStringRef(CFStringRef absl_nonnull cf_string);

} // namespace fasterswiper
