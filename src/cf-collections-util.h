#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGEventTypes.h>

#include <type_traits>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gutil/status.h"

namespace fasterswiper {

namespace cf_internal {

// Maps a CF reference type to its CFGetTypeID / expected TypeID.
template <typename T> struct CFTypeTraits;

template <> struct CFTypeTraits<CFDictionaryRef> {
  static CFTypeID ExpectedTypeID() { return CFDictionaryGetTypeID(); }
  static constexpr absl::string_view kName = "CFDictionary";
};

template <> struct CFTypeTraits<CFArrayRef> {
  static CFTypeID ExpectedTypeID() { return CFArrayGetTypeID(); }
  static constexpr absl::string_view kName = "CFArray";
};

template <> struct CFTypeTraits<CFNumberRef> {
  static CFTypeID ExpectedTypeID() { return CFNumberGetTypeID(); }
  static constexpr absl::string_view kName = "CFNumber";
};

template <> struct CFTypeTraits<CFBooleanRef> {
  static CFTypeID ExpectedTypeID() { return CFBooleanGetTypeID(); }
  static constexpr absl::string_view kName = "CFBoolean";
};

template <> struct CFTypeTraits<CFStringRef> {
  static CFTypeID ExpectedTypeID() { return CFStringGetTypeID(); }
  static constexpr absl::string_view kName = "CFString";
};

// A type T is a CFRef if CFTypeTraits<T> is specialized (i.e. has
// ExpectedTypeID).
template <typename T>
concept CFRef = requires {
  { CFTypeTraits<T>::ExpectedTypeID() } -> std::same_as<CFTypeID>;
};

namespace cf_internal {

// Helper to map C++ arithmetic types to the closest CFNumberType.
template <typename T> inline constexpr CFNumberType GetCFNumberType() {
  if constexpr (std::is_floating_point_v<T>) {
    if constexpr (sizeof(T) == 4) {
      return kCFNumberFloat32Type;
    } else if constexpr (sizeof(T) == 8) {
      return kCFNumberFloat64Type;
    } else {
      static_assert(false, "Unexpected floating point number size");
    }
  } else {
    if constexpr (sizeof(T) == 1) {
      return kCFNumberSInt8Type;
    } else if constexpr (sizeof(T) == 2) {
      return kCFNumberSInt16Type;
    } else if constexpr (sizeof(T) == 4) {
      return kCFNumberSInt32Type;
    } else if constexpr (sizeof(T) == 8) {
      return kCFNumberSInt64Type;
    } else {
      static_assert(false, "Unexpected integral number size");
    }
  }
}

} // namespace cf_internal

// Maps a C++ primitive type to its underlying CF reference type and provides
// an Extract method to convert the CF value.
template <typename T> struct CFPrimitiveTraits;

// Specialization for arithmetic types (int, float, etc.) using CFNumber.
template <typename T>
  requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
struct CFPrimitiveTraits<T> {
  using RefType = CFNumberRef;
  static absl::StatusOr<T> Extract(absl_nonnull RefType ref) {
    T result;
    if (!CFNumberGetValue(ref, cf_internal::GetCFNumberType<T>(), &result)) {
      return absl::InvalidArgumentError("failed to convert CFNumber");
    }
    return result;
  }
};

// Specialization for bool using CFBoolean.
template <> struct CFPrimitiveTraits<bool> {
  using RefType = CFBooleanRef;
  static absl::StatusOr<bool> Extract(absl_nonnull RefType ref) {
    return static_cast<bool>(CFBooleanGetValue(ref));
  }
};

// A type T is a CFPrimitive if CFPrimitiveTraits<T> is specialized.
template <typename T>
concept CFPrimitive = requires { typename CFPrimitiveTraits<T>::RefType; };

// Verify that a non-null CF reference has the expected CF type.
template <CFRef RefT> absl::Status CheckCFType(const void *absl_nonnull ref) {
  if (CFGetTypeID(ref) != CFTypeTraits<RefT>::ExpectedTypeID()) {
    return absl::InvalidArgumentError(
        absl::StrCat("expected ", CFTypeTraits<RefT>::kName));
  }
  return absl::OkStatus();
}

} // namespace cf_internal

template <cf_internal::CFRef RefT>
absl::StatusOr<absl_nonnull RefT> CFDictGetAs(absl_nonnull CFDictionaryRef dict,
                                              absl_nonnull CFStringRef key) {
  auto value = static_cast<RefT>(CFDictionaryGetValue(dict, key));
  if (value == nullptr) {
    return absl::NotFoundError("key not found in CFDictionary");
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return value;
}

template <cf_internal::CFRef RefT>
absl::StatusOr<absl_nullable RefT>
CFDictOptionalGetAs(absl_nonnull CFDictionaryRef dict,
                    absl_nonnull CFStringRef key) {
  auto value = static_cast<RefT>(CFDictionaryGetValue(dict, key));
  if (value == nullptr) {
    return static_cast<RefT>(nullptr);
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return value;
}

template <cf_internal::CFPrimitive T>
absl::StatusOr<T> CFDictGetAs(absl_nonnull CFDictionaryRef dict,
                              absl_nonnull CFStringRef key) {
  using RefT = typename cf_internal::CFPrimitiveTraits<T>::RefType;
  auto value = static_cast<RefT>(CFDictionaryGetValue(dict, key));
  if (value == nullptr) {
    return absl::NotFoundError("key not found in CFDictionary");
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return cf_internal::CFPrimitiveTraits<T>::Extract(value);
}

template <cf_internal::CFPrimitive T>
absl::StatusOr<std::optional<T>>
CFDictOptionalGetAs(absl_nonnull CFDictionaryRef dict,
                    absl_nonnull CFStringRef key) {
  using RefT = typename cf_internal::CFPrimitiveTraits<T>::RefType;
  auto value = static_cast<RefT>(CFDictionaryGetValue(dict, key));
  if (value == nullptr) {
    return std::nullopt;
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return cf_internal::CFPrimitiveTraits<T>::Extract(value);
}

template <cf_internal::CFRef RefT>
absl::StatusOr<absl_nonnull RefT> CFArrayGetAs(absl_nonnull CFArrayRef array,
                                               CFIndex idx) {
  auto value = static_cast<RefT>(CFArrayGetValueAtIndex(array, idx));
  if (value == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("index ", idx, " not found in CFArray"));
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return value;
}

template <cf_internal::CFRef RefT>
absl::StatusOr<absl_nullable RefT>
CFArrayOptionalGetAs(absl_nonnull CFArrayRef array, CFIndex idx) {
  auto value = static_cast<RefT>(CFArrayGetValueAtIndex(array, idx));
  if (value == nullptr) {
    return static_cast<RefT>(nullptr);
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return value;
}

template <cf_internal::CFPrimitive T>
absl::StatusOr<T> CFArrayGetAs(absl_nonnull CFArrayRef array, CFIndex idx) {
  using RefT = typename cf_internal::CFPrimitiveTraits<T>::RefType;
  auto value = static_cast<RefT>(CFArrayGetValueAtIndex(array, idx));
  if (value == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("index ", idx, " is null in CFArray"));
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return cf_internal::CFPrimitiveTraits<T>::Extract(value);
}

template <cf_internal::CFPrimitive T>
absl::StatusOr<std::optional<T>>
CFArrayOptionalGetAs(absl_nonnull CFArrayRef array, CFIndex idx) {
  using RefT = typename cf_internal::CFPrimitiveTraits<T>::RefType;
  auto value = static_cast<RefT>(CFArrayGetValueAtIndex(array, idx));
  if (value == nullptr) {
    return std::nullopt;
  }

  RETURN_IF_ERROR(cf_internal::CheckCFType<RefT>(value));
  return cf_internal::CFPrimitiveTraits<T>::Extract(value);
}

// Extract an int from a CFNumberRef.
inline absl::StatusOr<int> CFNumberToInt(absl_nonnull CFNumberRef ref) {
  int result;
  if (!CFNumberGetValue(ref, kCFNumberIntType, &result)) {
    return absl::InvalidArgumentError("failed to convert CFNumber to int");
  }

  return result;
}

} // namespace fasterswiper
