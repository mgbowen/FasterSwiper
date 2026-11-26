#include "src/swift/display-link-timer.h"

#include "src/animation-timer.h"
#include "src/cf-util.h"
#include "src/periodic-timer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <CoreFoundation/CoreFoundation.h>
#include <absl/base/nullability.h>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/time/time.h>
#include <google/rpc/code.pb.h>
#include <google/rpc/status.pb.h>

namespace fasterswiper {
namespace {

using DisplayLinkTickFn = bool (*)(void* ctx, int64_t elapsed_ns);
using DisplayLinkStoppedFn = void (*)(void* ctx, bool finished);

extern "C" {
void* fs_display_link_start(uint32_t display_id, int64_t duration_ns,
                            bool fire_immediately, DisplayLinkTickFn on_tick,
                            DisplayLinkStoppedFn on_stopped, void* callback_ctx,
                            CFDataRef* out_error_status);
void fs_display_link_cancel_and_destroy(void* handle);
}  // extern "C"

absl::Status ParseDisplayLinkErrorStatus(CFDataRef error_status_data) {
  if (error_status_data == nullptr) {
    return absl::InternalError(
        "fs_display_link_start failed without reporting a status");
  }

  const CFIndex len = CFDataGetLength(error_status_data);
  const UInt8* bytes = CFDataGetBytePtr(error_status_data);
  if (len < 0 || len > static_cast<CFIndex>(std::numeric_limits<int>::max()) ||
      (len > 0 && bytes == nullptr)) {
    return absl::InternalError(
        "fs_display_link_start returned an invalid serialized Status buffer");
  }

  google::rpc::Status proto_status;
  if (!proto_status.ParseFromArray(bytes, static_cast<int>(len))) {
    return absl::InternalError(
        "Failed to parse google.rpc.Status from fs_display_link_start");
  }

  if (!google::rpc::Code_IsValid(proto_status.code()) ||
      proto_status.code() == google::rpc::OK) {
    return absl::InternalError(absl::StrCat(
        "fs_display_link_start returned unexpected google.rpc.Code ",
        proto_status.code(), ": ", proto_status.message()));
  }

  return absl::Status(static_cast<absl::StatusCode>(proto_status.code()),
                      proto_status.message());
}

struct DisplayLinkCallbackContext {
  absl::AnyInvocable<AnimationTickResult(int64_t)> tick_callback;
  absl::AnyInvocable<void(AnimationStopReason) &&> stopped_callback;
};

bool OnDisplayLinkTick(void* raw_ctx, int64_t elapsed_ns) {
  auto* ctx = static_cast<DisplayLinkCallbackContext*>(raw_ctx);
  return ctx->tick_callback(elapsed_ns) == AnimationTickResult::kContinue;
}

void OnDisplayLinkStopped(void* raw_ctx, bool finished) {
  auto* ctx = static_cast<DisplayLinkCallbackContext*>(raw_ctx);
  if (ctx->stopped_callback != nullptr) {
    std::move(ctx->stopped_callback)(finished
                                         ? AnimationStopReason::kFinished
                                         : AnimationStopReason::kCancelled);
  }
}

class DisplayLinkTimer final : public AnimationTimer {
public:
  DisplayLinkTimer(std::unique_ptr<DisplayLinkCallbackContext> ctx,
                   void* handle)
      : ctx_(std::move(ctx)), handle_(handle) {
    CHECK(ctx_ != nullptr);
    CHECK(handle_ != nullptr);
  }

  DisplayLinkTimer(const DisplayLinkTimer&) = delete;
  DisplayLinkTimer(DisplayLinkTimer&&) = delete;
  DisplayLinkTimer& operator=(const DisplayLinkTimer&) = delete;
  DisplayLinkTimer& operator=(DisplayLinkTimer&&) = delete;

  ~DisplayLinkTimer() override { CancelImpl(); }

  void Cancel() override { CancelImpl(); }

private:
  void CancelImpl() {
    if (handle_ != nullptr) {
      void* handle = std::exchange(handle_, nullptr);
      fs_display_link_cancel_and_destroy(handle);
    }
  }

  std::unique_ptr<DisplayLinkCallbackContext> ctx_;
  void* handle_ = nullptr;
};

}  // namespace

absl::StatusOr<absl_nonnull std::unique_ptr<AnimationTimer>>
CreateDisplayLinkTimer(AnimationTimerParameters& parameters) {
  if (parameters.tick_callback == nullptr) {
    return absl::InvalidArgumentError(
        "DisplayLinkTimer requires a non-null tick_callback");
  }

  auto ctx =
      std::make_unique<DisplayLinkCallbackContext>(DisplayLinkCallbackContext{
          .tick_callback = std::move(parameters.tick_callback),
          .stopped_callback = std::move(parameters.stopped_callback),
      });

  const uint32_t display_id = parameters.display_id.value_or(0);
  const int64_t duration_ns = absl::ToInt64Nanoseconds(parameters.duration);
  CFDataRef raw_error_status = nullptr;
  void* handle = fs_display_link_start(
      display_id, duration_ns, parameters.fire_immediately, &OnDisplayLinkTick,
      &OnDisplayLinkStopped, ctx.get(), &raw_error_status);
  CFUniquePtr<CFDataRef> error_status = WrapCFUnique(raw_error_status);
  if (handle == nullptr) {
    parameters.tick_callback = std::move(ctx->tick_callback);
    parameters.stopped_callback = std::move(ctx->stopped_callback);
    return ParseDisplayLinkErrorStatus(error_status.get());
  }

  return std::make_unique<DisplayLinkTimer>(std::move(ctx), handle);
}

AnimationTimerFactory MakeDisplayLinkTimerFactory() {
  return [](AnimationTimerParameters params)
             -> std::unique_ptr<AnimationTimer> {
    absl::StatusOr<absl_nonnull std::unique_ptr<AnimationTimer>> maybe_timer =
        CreateDisplayLinkTimer(params);
    if (maybe_timer.ok()) {
      return *std::move(maybe_timer);
    }
    VLOG(1) << "Falling back to PeriodicTimer: " << maybe_timer.status();
    return MakePeriodicTimerFactory()(std::move(params));
  };
}

}  // namespace fasterswiper
