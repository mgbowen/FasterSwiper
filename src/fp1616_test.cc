#include "src/fp1616.h"

#include <array>
#include <limits>

#include <gtest/gtest.h>

namespace fasterswiper {
namespace {

TEST(FP1616Test, ClampsOverflowAndHandlesNonFiniteInputs) {
  constexpr FixedFP1616 kExpectedMax = static_cast<FixedFP1616>(32767) * 65536;
  constexpr FixedFP1616 kExpectedMin = -kExpectedMax;

  EXPECT_EQ(DoubleToFixedFP1616(0.0), 0);
  EXPECT_EQ(DoubleToFixedFP1616(-0.0), 0);
  EXPECT_EQ(DoubleToFixedFP1616(1e-10), 1);
  EXPECT_EQ(DoubleToFixedFP1616(-1e-10), -1);
  EXPECT_EQ(DoubleToFixedFP1616(32000.0), 32000 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(-32000.0), -32000 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(32767.0), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-32767.0), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(32768.0), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-32768.0), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(1e9), kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-1e9), kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(std::numeric_limits<double>::infinity()),
            kExpectedMax);
  EXPECT_EQ(DoubleToFixedFP1616(-std::numeric_limits<double>::infinity()),
            kExpectedMin);
  EXPECT_EQ(DoubleToFixedFP1616(std::numeric_limits<double>::quiet_NaN()), 0);
}

TEST(FP1616Test, ConvertsExactValuesAndRoundTripsQuantization) {
  EXPECT_EQ(DoubleToFixedFP1616(kFixedFP1616Lsb), 1);
  EXPECT_EQ(DoubleToFixedFP1616(-kFixedFP1616Lsb), -1);
  EXPECT_EQ(DoubleToFixedFP1616(0.25), 16384);
  EXPECT_EQ(DoubleToFixedFP1616(-0.5), -32768);
  EXPECT_EQ(DoubleToFixedFP1616(1.0), 65536);
  EXPECT_EQ(DoubleToFixedFP1616(-1.0), -65536);
  EXPECT_EQ(DoubleToFixedFP1616(6.0), 6 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(-6.0), -6 * 65536);
  EXPECT_EQ(DoubleToFixedFP1616(500.0), 500 * 65536);

  EXPECT_DOUBLE_EQ(QuantizeFP1616(0.0), 0.0);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(-0.0), 0.0);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(1e-10), kFixedFP1616Lsb);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(-1e-10), -kFixedFP1616Lsb);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(std::numeric_limits<double>::infinity()),
                   kMaxFixedFP1616);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(-std::numeric_limits<double>::infinity()),
                   -kMaxFixedFP1616);
  EXPECT_DOUBLE_EQ(QuantizeFP1616(std::numeric_limits<double>::quiet_NaN()),
                   0.0);

  constexpr std::array<double, 15> kTestValues = {
      0.0,
      1e-10,
      -1e-10,
      0.000016,
      -0.000016,
      kFixedFP1616Lsb,
      -kFixedFP1616Lsb,
      0.123456,
      -0.5,
      0.99999,
      1.0,
      -1.0,
      6.0,
      -6.0,
      500.0,
  };

  for (double val : kTestValues) {
    const FixedFP1616 fixed = DoubleToFixedFP1616(val);
    const double quantized = QuantizeFP1616(val);
    EXPECT_DOUBLE_EQ(quantized, static_cast<double>(fixed) / kFixedFP1616Scale)
        << "val=" << val;
    EXPECT_EQ(DoubleToFixedFP1616(quantized), fixed) << "val=" << val;
    EXPECT_DOUBLE_EQ(QuantizeFP1616(quantized), quantized) << "val=" << val;
  }
}

}  // namespace
}  // namespace fasterswiper
