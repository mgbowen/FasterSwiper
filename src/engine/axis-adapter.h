#pragma once

#include "src/macos-private.h"
#include "src/space-state.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <absl/status/statusor.h>

namespace fasterswiper {

enum class Axis {
  kHorizontal = kCGGestureMotionHorizontal,
  kVertical = kCGGestureMotionVertical,
};

class AxisAdapter {
public:
  AxisAdapter() = default;

  AxisAdapter(const AxisAdapter &) = default;
  AxisAdapter(AxisAdapter &&) = default;
  AxisAdapter &operator=(const AxisAdapter &) = default;
  AxisAdapter &operator=(AxisAdapter &&) = default;

  virtual ~AxisAdapter() = default;

  [[nodiscard]] virtual constexpr absl::string_view debug_name() const = 0;

  [[nodiscard]] virtual Axis movement_direction() const = 0;

  [[nodiscard]] virtual double
  NanoswipesToProgress(int64_t nanoswipes) const = 0;

  [[nodiscard]] virtual int64_t ProgressToNanoswipes(double progress) const = 0;

  [[nodiscard]] virtual bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const;

  [[nodiscard]] virtual absl::StatusOr<int64_t> committed_position() const = 0;

  [[nodiscard]] virtual std::pair<int64_t, int64_t>
  position_soft_limits() const = 0;
};

class HorizontalAxisAdapter : public AxisAdapter {
public:
  explicit HorizontalAxisAdapter(SpaceState space_state);

  HorizontalAxisAdapter(const HorizontalAxisAdapter &) = default;
  HorizontalAxisAdapter(HorizontalAxisAdapter &&) = default;
  HorizontalAxisAdapter &operator=(const HorizontalAxisAdapter &) = default;
  HorizontalAxisAdapter &operator=(HorizontalAxisAdapter &&) = default;

  ~HorizontalAxisAdapter() override = default;

  constexpr absl::string_view debug_name() const override {
    return "HorizontalAxisAdapter";
  }

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kHorizontal;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;

private:
  SpaceState space_state_;
};

class VerticalAxisAdapter : public AxisAdapter {
public:
  explicit VerticalAxisAdapter(SpaceState space_state);

  VerticalAxisAdapter(const VerticalAxisAdapter &) = default;
  VerticalAxisAdapter(VerticalAxisAdapter &&) = default;
  VerticalAxisAdapter &operator=(const VerticalAxisAdapter &) = default;
  VerticalAxisAdapter &operator=(VerticalAxisAdapter &&) = default;

  ~VerticalAxisAdapter() override = default;

  constexpr absl::string_view debug_name() const override {
    return "VerticalAxisAdapter";
  }

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kVertical;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;

private:
  SpaceState space_state_;
};

class AppExposeHorizontalAxisAdapter_MacOS26 : public AxisAdapter {
public:
  explicit AppExposeHorizontalAxisAdapter_MacOS26(SpaceState space_state);

  AppExposeHorizontalAxisAdapter_MacOS26(
      const AppExposeHorizontalAxisAdapter_MacOS26 &) = default;
  AppExposeHorizontalAxisAdapter_MacOS26(
      AppExposeHorizontalAxisAdapter_MacOS26 &&) = default;
  AppExposeHorizontalAxisAdapter_MacOS26 &
  operator=(const AppExposeHorizontalAxisAdapter_MacOS26 &) = default;
  AppExposeHorizontalAxisAdapter_MacOS26 &
  operator=(AppExposeHorizontalAxisAdapter_MacOS26 &&) = default;

  ~AppExposeHorizontalAxisAdapter_MacOS26() override = default;

  constexpr absl::string_view debug_name() const override {
    return "AppExposeHorizontalAxisAdapter_MacOS26";
  }

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kHorizontal;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;

private:
  SpaceState space_state_;
};

class AppExposeHorizontalAxisAdapter : public AxisAdapter {
public:
  AppExposeHorizontalAxisAdapter() = default;

  AppExposeHorizontalAxisAdapter(const AppExposeHorizontalAxisAdapter &) =
      default;
  AppExposeHorizontalAxisAdapter(AppExposeHorizontalAxisAdapter &&) = default;
  AppExposeHorizontalAxisAdapter &
  operator=(const AppExposeHorizontalAxisAdapter &) = default;
  AppExposeHorizontalAxisAdapter &
  operator=(AppExposeHorizontalAxisAdapter &&) = default;

  ~AppExposeHorizontalAxisAdapter() override = default;

  constexpr absl::string_view debug_name() const override {
    return "AppExposeHorizontalAxisAdapter";
  }

  [[nodiscard]] Axis movement_direction() const override {
    return Axis::kHorizontal;
  }

  [[nodiscard]] double NanoswipesToProgress(int64_t nanoswipes) const override;

  [[nodiscard]] int64_t ProgressToNanoswipes(double progress) const override;

  [[nodiscard]] bool
  WaitForCommittedPositionChanged(int64_t original_position,
                                  absl::Duration deadline) const override;

  [[nodiscard]] absl::StatusOr<int64_t> committed_position() const override;

  [[nodiscard]] std::pair<int64_t, int64_t>
  position_soft_limits() const override;
};

absl::StatusOr<std::unique_ptr<AxisAdapter>>
CreateAppExposeHorizontalAxisAdapter();

} // namespace fasterswiper
