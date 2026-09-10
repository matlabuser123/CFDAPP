// P3-PHYS-003: temperature-dependent viscosity mu(T), validated at the
// equation-assembly level. No new production code is needed for this --
// it reuses the existing field-based assembleDiffusionContribution
// overload (P2-TURB-003) directly, driven by a per-cell mu(T) field
// produced by cfd::physics::evaluatePropertyField. This file proves that
// pipeline end-to-end: hand-derived face-flux values, internal-face
// conservation with a genuinely temperature-varying field, a strong hot/
// cold contrast case, and constant-property equivalence.
//
// Deliberately NOT covered here (disclosed scope boundary, see this
// task's Final Report): wiring mu(T) into SIMPLE's own outer iteration.
// SIMPLE's effectiveViscosity is always sourced from the polymorphic
// cfd::turbulence::TurbulenceModel interface; routing a temperature-
// dependent viscosity through that interface would either misuse it for a
// non-turbulence concept or require a deeper SIMPLE refactor separating
// "effective-viscosity source" from "turbulence model" -- both judged out
// of scope for this task, the same class of disclosed deferral as PISO in
// P3-PHYS-001 and CLI orchestration in P3-PHYS-002.
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/physics/TemperatureProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::ConstantProperty;
using cfd::physics::evaluatePropertyField;
using cfd::physics::LinearProperty;
using cfd::physics::TabulatedProperty;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

}  // namespace

TEST(MomentumVariableViscosityTest, ConstantPropertyReproducesScalarOverloadExactly) {
  // Backward-equivalence (P3-PHYS-003 section 4): mu(T) = ConstantProperty
  // evaluated into a field must reproduce the plain scalar-mu overload's
  // assembled system, to the same tight tolerance already established by
  // MomentumDiffusionTest.UniformEffectiveViscosityFieldMatchesConstantOverload
  // for a uniform field (the interpolateInternalFace roundoff floor, not a
  // physics difference).
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  const Real mu0 = 0.0173;
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{3.0, -2.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.5});

  SparseMatrixBuilder scalarBuilder(n, n);
  Vector scalarRhs(n, 0.0);
  assembleDiffusionContribution(mesh, mu0, velocity, boundaries, VelocityComponent::U,
                                scalarBuilder, scalarRhs);
  const auto scalarMatrix = scalarBuilder.build();

  const ConstantProperty viscosityModel(mu0);
  const ScalarField temperature(n, 350.0);  // arbitrary -- ConstantProperty ignores it.
  const ScalarField muField = evaluatePropertyField(mesh, temperature, viscosityModel);

  SparseMatrixBuilder fieldBuilder(n, n);
  Vector fieldRhs(n, 0.0);
  assembleDiffusionContribution(mesh, muField, velocity, boundaries, VelocityComponent::U,
                                fieldBuilder, fieldRhs);
  const auto fieldMatrix = fieldBuilder.build();

  for (Index row = 0; row < n; ++row) {
    EXPECT_NEAR(fieldMatrix.diagonal(row), scalarMatrix.diagonal(row), 1e-12 * mu0)
        << "row " << row;
    EXPECT_NEAR(fieldRhs[row], scalarRhs[row], 1e-12 * std::max<Real>(1.0, std::abs(scalarRhs[row])))
        << "row " << row;
  }
}

TEST(MomentumVariableViscosityTest, InternalFaceMatchesHandDerivedLinearMuValue) {
  // Two-cell (1x2) mesh: cell 0 at T=300, cell 1 at T=320, mu(T) = 0.01 +
  // 0.0001*(T-300) (LinearProperty). mu(300)=0.01, mu(320)=0.012. The
  // shared internal face's distance-weighted interpolation
  // (interpolateInternalFace) on a uniform Cartesian mesh with equal cell
  // sizes weights both cells equally (dPf=dNf), so
  // muFace = 0.5*(0.01+0.012) = 0.011 exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cells 0 (left), 1 (right).
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  ASSERT_EQ(n, 2);
  const VectorField velocity(n, Vector2{0.0, 0.0});

  ScalarField temperature(n);
  temperature[0] = 300.0;
  temperature[1] = 320.0;
  const LinearProperty viscosityModel(0.01, 300.0, 0.0001);
  const ScalarField muField = evaluatePropertyField(mesh, temperature, viscosityModel);
  ASSERT_DOUBLE_EQ(muField[0], 0.01);
  ASSERT_DOUBLE_EQ(muField[1], 0.012);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleDiffusionContribution(mesh, muField, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  // Cell width 1.0 each (mesh width 2.0 / 2 cells), height 1.0 -> shared
  // face area 1.0, owner-neighbor distance 1.0.
  const Real muFaceExpected = 0.5 * (0.01 + 0.012);
  const Real internalCoefficientExpected = muFaceExpected * /*area=*/1.0 / /*dPN=*/1.0;

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];  // A(1,0), off-diagonal from the internal face.
  EXPECT_NEAR(-a10, internalCoefficientExpected, 1e-12);

  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real a01 = matrix.multiply(e1)[0];  // A(0,1)
  EXPECT_NEAR(-a01, internalCoefficientExpected, 1e-12);

  // Equal-and-opposite conservation: the internal face's contribution to
  // cell 0's diagonal and cell 1's diagonal must match the same
  // coefficient (each row's diagonal also includes its two boundary-face
  // contributions, so compare the isolated internal-face piece via the
  // off-diagonal entries already checked above, which are exactly
  // -internalCoefficientExpected on both sides -- symmetric by
  // construction).
  EXPECT_NEAR(a01, a10, 1e-12);
}

TEST(MomentumVariableViscosityTest, TabulatedMuGivesTheExpectedFaceValue) {
  // Same two-cell mesh, mu(T) now from a lookup table instead of a linear
  // law -- proves the pipeline is agnostic to which TemperatureProperty
  // subclass produced the field.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});

  ScalarField temperature(n);
  temperature[0] = 300.0;
  temperature[1] = 340.0;
  const TabulatedProperty viscosityModel({300.0, 320.0, 340.0}, {1.00e-2, 0.85e-2, 0.73e-2});
  const ScalarField muField = evaluatePropertyField(mesh, temperature, viscosityModel);
  ASSERT_DOUBLE_EQ(muField[0], 1.00e-2);
  ASSERT_DOUBLE_EQ(muField[1], 0.73e-2);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleDiffusionContribution(mesh, muField, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  const Real muFaceExpected = 0.5 * (1.00e-2 + 0.73e-2);
  const Real internalCoefficientExpected = muFaceExpected * 1.0 / 1.0;

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];
  EXPECT_NEAR(-a10, internalCoefficientExpected, 1e-12);
}

TEST(MomentumVariableViscosityTest, StrongTemperatureContrastIncreasesLocalDiffusionAsymmetrically) {
  // A strong hot spot at one cell drives mu(T) sharply up there via a
  // linear law -- the resulting diagonal/off-diagonal changes must follow
  // exactly the same "local, per-cell, not global" signature already
  // proven for a directly-specified effectiveViscosity field in
  // MomentumDiffusionTest.NonUniformEffectiveViscosityChangesDiffusionCoefficients,
  // now driven through mu(T) end-to-end instead of a hand-built field.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{0.0, 0.0});
  const LinearProperty viscosityModel(0.01, 300.0, 0.0002);  // mu grows with T.

  ScalarField uniformTemperature(n, 300.0);
  const ScalarField baselineMu = evaluatePropertyField(mesh, uniformTemperature, viscosityModel);
  SparseMatrixBuilder baselineBuilder(n, n);
  Vector baselineRhs(n, 0.0);
  assembleDiffusionContribution(mesh, baselineMu, velocity, boundaries, VelocityComponent::U,
                                baselineBuilder, baselineRhs);
  const auto baselineMatrix = baselineBuilder.build();

  ScalarField hotSpotTemperature(n, 300.0);
  hotSpotTemperature[0] = 2300.0;  // +2000K at cell 0 only -> mu[0] = 0.01+0.4 = 0.41.
  const ScalarField perturbedMu = evaluatePropertyField(mesh, hotSpotTemperature, viscosityModel);
  SparseMatrixBuilder perturbedBuilder(n, n);
  Vector perturbedRhs(n, 0.0);
  assembleDiffusionContribution(mesh, perturbedMu, velocity, boundaries, VelocityComponent::U,
                                perturbedBuilder, perturbedRhs);
  const auto perturbedMatrix = perturbedBuilder.build();

  EXPECT_GT(perturbedMatrix.diagonal(0), baselineMatrix.diagonal(0));
  // Cell 8, diagonally opposite and sharing no face with cell 0, is
  // completely unaffected.
  EXPECT_DOUBLE_EQ(perturbedMatrix.diagonal(8), baselineMatrix.diagonal(8));
}
