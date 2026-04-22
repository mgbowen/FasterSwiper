#include "src/cf-util.h"

#include "absl/log/check.h"

namespace fasterswiper {

absl::StatusOr<std::string>
StringFromCFStringRef(CFStringRef absl_nonnull cf_string) {
  CHECK(cf_string != nullptr);

  const char *maybe_c_string =
      CFStringGetCStringPtr(cf_string, kCFStringEncodingUTF8);
  if (maybe_c_string != nullptr) {
    return std::string(maybe_c_string);
  }

  constexpr auto encoding = kCFStringEncodingUTF8;

  const CFIndex num_utf16_chars = CFStringGetLength(cf_string);
  const CFIndex max_num_bytes =
      CFStringGetMaximumSizeForEncoding(num_utf16_chars, encoding) + 1;

  std::string result(max_num_bytes, '\0');
  if (!CFStringGetCString(cf_string, result.data(), max_num_bytes, encoding)) {
    return absl::InternalError("Failed to convert CFStringRef to C-string");
  }

  const size_t actual_length = strlen(result.data());
  result.resize(actual_length);

  return result;
}

} // namespace fasterswiper
