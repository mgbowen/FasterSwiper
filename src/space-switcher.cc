#include "src/space-switcher.h"

#include "src/cf-util.h"
#include "src/const.h"
#include "src/event.h"
#include "src/macos-private.h"
#include "src/mission-control.h"
#include "src/periodic-timer.h"
#include "src/space-state.h"
#include "src/string-util.h"

#include <cfloat>
#include <optional>
#include <thread>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CGEvent.h>

#include "absl/log/log.h"
#include "gutil/status.h"

namespace fasterswiper {

SpaceSwitchOperation::SpaceSwitchOperation(
    std::unique_ptr<AxisAdapter> axis_adapter,
    std::unique_ptr<MovementEngine> movement_engine)
    : axis_adapter_(std::move(axis_adapter)),
      movement_engine_(std::move(movement_engine)) {}

SpaceSwitchOperation::~SpaceSwitchOperation() {
  absl::MutexLock lock(mutex_);
  if (!is_committed_) {
    LOG(FATAL)
        << "SpaceSwitchOperation must be committed before being destroyed";
  }
}

int64_t SpaceSwitchOperation::position() const {
  absl::MutexLock lock(mutex_);
  return movement_engine_->position();
}

std::pair<int64_t, int64_t> SpaceSwitchOperation::position_soft_limits() const {
  absl::MutexLock lock(mutex_);
  return axis_adapter_->position_soft_limits();
}

void SpaceSwitchOperation::SetPosition(int64_t new_position) {
  absl::MutexLock lock(mutex_);

  VLOG(1) << "BEGIN SetPosition(new_position=" << new_position
          << "): current_position=" << movement_engine_->position();
  movement_engine_->SetPosition(new_position);
  VLOG(1) << "END SetPosition(" << new_position
          << "): current_position_=" << movement_engine_->position();
}

void SpaceSwitchOperation::Commit() {
  absl::MutexLock lock(mutex_);
  VLOG(1) << "BEGIN Commit()";

  if (is_committed_) {
    VLOG(1) << "Already committed";
  } else {
    movement_engine_->Commit();
    is_committed_ = true;
  }

  VLOG(1) << "END Commit()";
}

} // namespace fasterswiper
