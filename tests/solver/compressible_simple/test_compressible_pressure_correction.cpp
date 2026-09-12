// P12-COMP-002: assembleCompressiblePressureCorrection -- generalizes
// cfd::pressure_velocity::assemblePressureCorrection with (a) a per-face
// density (not one global constant) in the D_f coefficient, and (b) a
// new diagonal compressibility term via IdealGasEOS::dDensityDPressure.
// The mandatory reduction check: with faceDensity uniform and
// dDensityDPressure negligible (a very large gas constant -- the
// physical near-incompressible-gas limit, not a test-only bypass), this
// function's result closely matches assemblePressureCorrection's own.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/compressible/CompressiblePressureCorrection.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::compressible::assembleCompressiblePressureCorrection;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::PressureCorrectionAssembly;

namespace {

Mesh makeCavityMesh() { return MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0); }

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BoundaryConditionSet makeOpenChannelPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

}  // namespace

TEST(CompressiblePressureCorrectionTest, NegligibleCompressibilityMatchesIncompressibleEquation) {
  const Mesh mesh = makeCavityMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const Real rho = 1.2;

  SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) predictorFlux[face.id()] = 0.05 * static_cast<Real>(face.id());
  }
  ScalarField uResponse(n, 0.8);
  ScalarField vResponse(n, 0.9);

  const PressureCorrectionAssembly reference =
      assemblePressureCorrection(mesh, predictorFlux, uResponse, vResponse, rho,
                                 /*referenceCell=*/0, pressureBoundaries);

  // Very large gas constant -> dDensityDPressure = 1/(R*T) is negligible
  // -- the physical near-incompressible-gas limit, applied to the
  // function under test itself, not a bypass of it.
  const ThermodynamicProperties thermodynamics(/*gasConstant=*/1.0e12, /*cp=*/2.0e12);
  const Real temperature = 300.0;
  const Real referencePressure = rho * thermodynamics.gasConstant() * temperature;
  const ScalarField pressureAbsolute(n, referencePressure);
  const ScalarField temperatureField(n, temperature);
  const SurfaceField faceDensity(mesh.numberOfFaces(), rho);
  const Real pseudoTimeStep = 1.0;

  const PressureCorrectionAssembly compressible = assembleCompressiblePressureCorrection(
      mesh, predictorFlux, faceDensity, uResponse, vResponse, pressureAbsolute, temperatureField,
      thermodynamics, pseudoTimeStep, /*referenceCell=*/0, pressureBoundaries);

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(compressible.system.matrix().diagonal(row), reference.system.matrix().diagonal(row),
               1e-6 * std::abs(reference.system.matrix().diagonal(row)) + 1e-9)
        << "row " << row;
    EXPECT_NEAR(compressible.system.rhs()[row], reference.system.rhs()[row], 1e-9) << "row " << row;
  }
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    EXPECT_NEAR(compressible.faceCoefficient[faceId], reference.faceCoefficient[faceId], 1e-9)
        << "face " << faceId;
  }
}

TEST(CompressiblePressureCorrectionTest, CompressibilityDiagonalMatchesHandFormula) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume = 1 each.
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();

  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zeroResponse(n, 0.0);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.0);
  const Real gasConstant = 287.05;
  const ThermodynamicProperties thermodynamics(gasConstant, /*cp=*/1005.0);
  const Real temperature = 300.0;
  const Real pressureAbs = 101325.0;
  const ScalarField pressureAbsolute(n, pressureAbs);
  const ScalarField temperatureField(n, temperature);
  const Real pseudoTimeStep = 0.5;

  const PressureCorrectionAssembly assembly = assembleCompressiblePressureCorrection(
      mesh, predictorFlux, faceDensity, zeroResponse, zeroResponse, pressureAbsolute,
      temperatureField, thermodynamics, pseudoTimeStep, /*referenceCell=*/0, pressureBoundaries);

  // With zero response coefficients, every D_f-derived contribution is
  // zero, so the only diagonal contribution left is the new
  // compressibility term, V_P/pseudoTimeStep * dDensityDPressure(p,T) =
  // V_P/pseudoTimeStep * 1/(R*T) -- plus, for cell 0 (the reference
  // cell), the row is overwritten to the identity equation (1.0) instead.
  const Real dRhoDp = 1.0 / (gasConstant * temperature);
  const Real expectedDiagonalCell1 = 1.0 /*volume*/ / pseudoTimeStep * dRhoDp;
  EXPECT_DOUBLE_EQ(assembly.system.matrix().diagonal(0), 1.0);  // referenceCell pin.
  EXPECT_NEAR(assembly.system.matrix().diagonal(1), expectedDiagonalCell1, 1e-12);
}

TEST(CompressiblePressureCorrectionTest, MismatchedPredictorMassFluxSizeThrows) {
  const Mesh mesh = makeCavityMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const SurfaceField predictorFlux(mesh.numberOfFaces() + 1, 0.0);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.0);
  const ScalarField response(n, 1.0);
  const ScalarField field(n, 300.0);
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);

  EXPECT_THROW((void)assembleCompressiblePressureCorrection(
                   mesh, predictorFlux, faceDensity, response, response, field, field,
                   thermodynamics, 1.0, 0, pressureBoundaries),
               InvalidArgumentError);
}

TEST(CompressiblePressureCorrectionTest, RejectsNonPositivePseudoTimeStep) {
  const Mesh mesh = makeCavityMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.0);
  const ScalarField response(n, 1.0);
  const ScalarField field(n, 300.0);
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);

  EXPECT_THROW((void)assembleCompressiblePressureCorrection(
                   mesh, predictorFlux, faceDensity, response, response, field, field,
                   thermodynamics, 0.0, 0, pressureBoundaries),
               InvalidArgumentError);
}

TEST(CompressiblePressureCorrectionTest, RejectsOutOfRangeReferenceCell) {
  const Mesh mesh = makeCavityMesh();
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.0);
  const ScalarField response(n, 1.0);
  const ScalarField field(n, 300.0);
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);

  EXPECT_THROW((void)assembleCompressiblePressureCorrection(
                   mesh, predictorFlux, faceDensity, response, response, field, field,
                   thermodynamics, 1.0, n + 1, pressureBoundaries),
               InvalidArgumentError);
}

TEST(CompressiblePressureCorrectionTest, OpenBoundaryDoesNotPinReferenceCell) {
  // Same "a Dirichlet pressure patch already removes the null space"
  // behavior as assemblePressureCorrection's own -- confirmed here by
  // checking the reference cell's row is NOT forced to the identity
  // equation when an open (FixedValue) boundary exists.
  const Mesh mesh = makeCavityMesh();
  const auto pressureBoundaries = makeOpenChannelPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const SurfaceField predictorFlux(mesh.numberOfFaces(), 0.0);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.0);
  const ScalarField response(n, 0.0);  // isolate: no D_f contribution from internal/other faces.
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);
  const ScalarField pressureAbsolute(n, 101325.0);
  const ScalarField temperatureField(n, 300.0);

  const PressureCorrectionAssembly assembly = assembleCompressiblePressureCorrection(
      mesh, predictorFlux, faceDensity, response, response, pressureAbsolute, temperatureField,
      thermodynamics, 1.0, /*referenceCell=*/0, pressureBoundaries);

  // Not pinned to 1.0 -- should instead be exactly the compressibility
  // term's own contribution alone (same formula as the isolated test
  // above), since every D_f contribution is zero here too.
  const Real dRhoDp = thermodynamics.equationOfState().dDensityDPressure(101325.0, 300.0);
  const Real expectedDiagonal = mesh.cell(0).volume() / 1.0 * dRhoDp;
  EXPECT_NEAR(assembly.system.matrix().diagonal(0), expectedDiagonal, 1e-12);
}
