// P15 -- the convective (Robin) thermal boundary condition.
//
// Checked against hand calculations and against its two physical limits, which
// is what makes the formula verifiable rather than merely self-consistent:
//   h_c -> 0        insulated: no heat crosses, wall sits at the cell value
//   h_c -> infinity Dirichlet: the wall is pinned to T_inf
#include <gtest/gtest.h>

#include <cmath>

#include <memory>
#include <string>
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/ConvectiveHeatTransfer.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionType;
using cfd::boundary::ConvectiveHeatTransfer;

TEST(ConvectiveBoundary, WallTemperatureMatchesHandCalculation) {
  // k = 10, d = 0.5 -> k/d = 20.  h_c = 5.  T_P = 400, T_inf = 300.
  // T_w = (20*400 + 5*300) / (20 + 5) = (8000 + 1500)/25 = 380.
  const ConvectiveHeatTransfer bc(5.0, 300.0);
  EXPECT_DOUBLE_EQ(bc.wallTemperature(400.0, 10.0, 0.5), 380.0);
}

TEST(ConvectiveBoundary, ConductanceIsTheSeriesResistance) {
  // 1 / (d/k + 1/h_c) = 1 / (0.5/10 + 1/5) = 1 / (0.05 + 0.2) = 4.
  const ConvectiveHeatTransfer bc(5.0, 300.0);
  EXPECT_DOUBLE_EQ(bc.conductancePerArea(10.0, 0.5), 4.0);
}

TEST(ConvectiveBoundary, InsulatedLimitTransfersNoHeat) {
  // h_c = 0: the adiabatic limit. Conductance 0, wall at the cell temperature.
  const ConvectiveHeatTransfer bc(0.0, 300.0);
  EXPECT_DOUBLE_EQ(bc.conductancePerArea(10.0, 0.5), 0.0);
  EXPECT_DOUBLE_EQ(bc.wallTemperature(400.0, 10.0, 0.5), 400.0);
}

TEST(ConvectiveBoundary, LargeCoefficientApproachesDirichlet) {
  // As h_c grows the wall is pinned to T_inf, and the conductance approaches
  // the pure wall-conduction value k/d = 20.
  const Real tInf = 300.0;
  Real previousGap = 1e30;
  for (const Real h : {1.0e2, 1.0e4, 1.0e6, 1.0e8}) {
    const ConvectiveHeatTransfer bc(h, tInf);
    const Real gap = std::abs(bc.wallTemperature(400.0, 10.0, 0.5) - tInf);
    EXPECT_LT(gap, previousGap) << "h_c = " << h;
    previousGap = gap;
  }
  const ConvectiveHeatTransfer huge(1.0e12, tInf);
  EXPECT_NEAR(huge.wallTemperature(400.0, 10.0, 0.5), tInf, 1e-6);
  EXPECT_NEAR(huge.conductancePerArea(10.0, 0.5), 20.0, 1e-3) << "should approach k/d";
}

TEST(ConvectiveBoundary, BoundaryValueIsTheZeroGradientEquivalent) {
  // This THREW in the first implementation, on the argument that a plausible
  // wrong value would be consumed silently by a generic assembler. The
  // argument was right about the generic ASSEMBLY path and wrong about the
  // gradient: discretization::gradient() evaluates boundaryValue() on every
  // boundary face when building the correction gradient, so throwing here
  // killed every convective case before any physics ran.
  //
  // It now returns the owner value, which is sound precisely because of what
  // consumes it: the correction gradient feeds ONLY the non-orthogonal
  // correction, which is identically zero on an orthogonal face. The refusal
  // the generic path needs did not disappear -- it moved to cfd::transport,
  // and ConvectiveBoundaryIsRefusedByTheGenericTransportPath below is what
  // holds it there.
  const ConvectiveHeatTransfer bc(5.0, 300.0);
  Real value = 0.0;
  EXPECT_NO_THROW(value = bc.boundaryValue(400.0, 0.5));
  EXPECT_EQ(value, 400.0) << "must be exactly the owner value, not a rounded version of it";

  // Independent of the coefficients, including the insulated limit: the face
  // value is not a property of the condition alone in any of these cases.
  for (const Real hc : {0.0, 1.0, 1.0e9}) {
    const ConvectiveHeatTransfer other(hc, 300.0);
    EXPECT_EQ(other.boundaryValue(273.15, 0.25), 273.15) << "h_c = " << hc;
  }
}

// The guarantee the old throwing test existed to provide, pinned where it now
// lives. Without this, the reversal above would have quietly removed a real
// protection: a generic scalar solve would treat a convective wall as
// adiabatic and report convergence on the wrong problem.
TEST(ConvectiveBoundary, ConvectiveBoundaryIsRefusedByTheGenericTransportPath) {
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(400.0));
  boundaries.set(mesh, "right", std::make_unique<ConvectiveHeatTransfer>(25.0, 300.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedTemperature>(350.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedTemperature>(350.0));

  const cfd::fields::ScalarField phi(mesh.numberOfCells(), 350.0);
  const cfd::fields::ScalarField gamma(mesh.numberOfCells(), 0.6);
  cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  cfd::algebra::Vector rhs(mesh.numberOfCells(), 0.0);

  try {
    cfd::transport::assembleScalarDiffusionContribution(mesh, gamma, phi, boundaries, builder, rhs);
    FAIL() << "the generic scalar transport path accepted a convective wall";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    // The message must say WHY, not merely that it refused -- the cause is
    // the runtime diffusivity the coefficient needs, and a user who is told
    // that knows to use the thermal path instead.
    EXPECT_NE(message.find("ConvectiveHeatTransfer"), std::string::npos) << message;
    EXPECT_NE(message.find("diffusivity"), std::string::npos) << message;
  }
}

TEST(ConvectiveBoundary, RejectsInvalidConstruction) {
  EXPECT_THROW(ConvectiveHeatTransfer(-1.0, 300.0), InvalidArgumentError);
  EXPECT_THROW(ConvectiveHeatTransfer(std::nan(""), 300.0), InvalidArgumentError);
  EXPECT_THROW(ConvectiveHeatTransfer(5.0, std::nan("")), InvalidArgumentError);
  EXPECT_NO_THROW(ConvectiveHeatTransfer(0.0, 300.0)) << "h_c = 0 is the insulated limit";
}

TEST(ConvectiveBoundary, RejectsInvalidGeometryOrProperty) {
  const ConvectiveHeatTransfer bc(5.0, 300.0);
  EXPECT_THROW((void)bc.conductancePerArea(0.0, 0.5), InvalidArgumentError);
  EXPECT_THROW((void)bc.conductancePerArea(10.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)bc.wallTemperature(400.0, -1.0, 0.5), InvalidArgumentError);
}

TEST(ConvectiveBoundary, HasItsOwnTypeAndName) {
  const ConvectiveHeatTransfer bc(5.0, 300.0);
  EXPECT_EQ(bc.type(), BoundaryConditionType::ConvectiveHeatTransfer);
  EXPECT_EQ(bc.name(), "ConvectiveHeatTransfer");
}
