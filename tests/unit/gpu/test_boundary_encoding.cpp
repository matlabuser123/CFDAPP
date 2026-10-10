// P15 -- the device boundary-condition encoder, and the Robin robustness fix.
//
// WHY THIS FILE EXISTS
// --------------------
// `encodeBoundaryCondition` represents a scalar BC on the device by PROBING
// `boundaryValue()` at sample points and fitting constant / shift / affine. It
// returns false for anything it cannot represent, and every caller treats that
// as "this configuration is not GPU-supported" -- a graceful path.
//
// ConvectiveHeatTransfer::boundaryValue() THROWS by design: a Robin face value
// depends on the local conductivity, which the interface cannot supply, and
// returning a plausible wrong number would be consumed silently by every
// assembler. That is correct for the CPU, but it meant a GPU plan built on a
// mesh carrying a convective wall would throw out of the plan constructor
// instead of reporting itself unsupported -- a GPU-robustness regression
// introduced by adding the BC.
//
// These tests pin both halves: the graceful refusal for Robin, and that every
// previously-supported encoding is unchanged.
#include <gtest/gtest.h>

#include <cmath>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/ConvectiveHeatTransfer.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/gpu/BoundaryEncoding.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::Adiabatic;
using cfd::boundary::ConvectiveHeatTransfer;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::FixedValue;
using cfd::boundary::HeatFlux;

namespace {
constexpr Real kDistance = 0.05;
}

TEST(BoundaryEncoding, ConvectiveWallIsReportedUnsupportedNotThrown) {
  // THE regression this file exists for. Before the fix this call propagated
  // an InvalidArgumentError out of the encoder and aborted GPU plan
  // construction; the documented behaviour is a false return.
  const ConvectiveHeatTransfer bc(25.0, 300.0);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  bool supported = true;
  EXPECT_NO_THROW(supported = cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_FALSE(supported) << "a Robin wall must be reported unsupported for device encoding";
}

TEST(BoundaryEncoding, ConvectiveWallRefusalDoesNotDependOnItsCoefficients) {
  // Including the insulated limit h_c = 0, where boundaryValue() still throws
  // because the face value is still not a property of the condition alone.
  for (const Real hc : {0.0, 1e-6, 1.0, 1e3, 1e9}) {
    const ConvectiveHeatTransfer bc(hc, 300.0);
    Real a = 0.0;
    Real b = 0.0;
    Index kind = -1;
    bool supported = true;
    EXPECT_NO_THROW(supported = cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
    EXPECT_FALSE(supported) << "h_c = " << hc;
  }
}

// --- every previously-supported encoding must be unchanged -------------------

TEST(BoundaryEncoding, FixedValueEncodesAsConstant) {
  const FixedValue bc(370.0);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingConstant);
  EXPECT_DOUBLE_EQ(a, 370.0);
  EXPECT_DOUBLE_EQ(b, 0.0);
}

TEST(BoundaryEncoding, FixedTemperatureEncodesAsConstant) {
  const FixedTemperature bc(450.0);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingConstant);
  EXPECT_DOUBLE_EQ(a, 450.0);
}

TEST(BoundaryEncoding, ZeroGradientEncodesAsShift) {
  // T_b = T_P + g d with g = 0 is the identity: the "shift" form with a = 0.
  const FixedGradient bc(0.0);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingShift);
  EXPECT_DOUBLE_EQ(a, 0.0);
  EXPECT_DOUBLE_EQ(b, 1.0);
}

TEST(BoundaryEncoding, NonZeroGradientEncodesAsShiftWithOffset) {
  const Real gradient = -12.5;
  const FixedGradient bc(gradient);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingShift);
  EXPECT_DOUBLE_EQ(a, gradient * kDistance);
  EXPECT_DOUBLE_EQ(b, 1.0);
}

TEST(BoundaryEncoding, AdiabaticEncodesAsShift) {
  const Adiabatic bc;
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingShift);
  EXPECT_DOUBLE_EQ(a, 0.0);
}

TEST(BoundaryEncoding, HeatFluxEncodesAsShift) {
  // HeatFlux takes (heatFlux, conductivity): the gradient it implies is
  // -q/k, so the encoding is a shift like any other Neumann condition.
  const HeatFlux bc(-40.0, 8.0);
  Real a = 0.0;
  Real b = 0.0;
  Index kind = -1;
  ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(bc, kDistance, a, b, kind));
  EXPECT_EQ(kind, cfd::gpu::kBoundaryEncodingShift);
}

TEST(BoundaryEncoding, EncodingsReproduceTheConditionExactly) {
  // The encoder's own contract: the fitted form must reproduce
  // boundaryValue() BITWISE over a range of owner values, not approximately.
  // This is what makes a device BC equivalent to its host counterpart rather
  // than merely similar.
  const FixedValue fixedValue(370.0);
  const FixedGradient gradient(-3.25);
  const Adiabatic adiabatic;

  struct Case {
    const cfd::boundary::ScalarBoundaryCondition* bc;
    const char* name;
  };
  const Case cases[] = {
      {&fixedValue, "FixedValue"},
      {&gradient, "FixedGradient"},
      {&adiabatic, "Adiabatic"},
  };

  for (const auto& testCase : cases) {
    Real a = 0.0;
    Real b = 0.0;
    Index kind = -1;
    ASSERT_TRUE(cfd::gpu::encodeBoundaryCondition(*testCase.bc, kDistance, a, b, kind))
        << testCase.name;
    for (const Real owner : {0.0, 1.0, -1.0, 273.15, 1e3, -7.125}) {
      const Real expected = testCase.bc->boundaryValue(owner, kDistance);
      const Real encoded = a + b * owner;
      EXPECT_EQ(encoded, expected) << testCase.name << " at owner = " << owner;
    }
  }
}
