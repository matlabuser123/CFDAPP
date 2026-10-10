// P14 -- Generic Transport Foundation.
//
// P14's whole
// risk is introducing a SECOND finite-volume convention into a codebase that
// already has nine scalar-transport assemblers and whose standard is bitwise
// equivalence. The generic layer was promoted from
// cfd::turbulence::assembleScalarTransportEquation rather than written afresh.
// That implementation has since been DELETED (P14 migration): all six production
// turbulence equations now route through this assembler, so the equivalence tests
// that compared the two retired with it. What replaced them is
// test_transport_characterization.cpp, which freezes the output those tests last
// proved identical,
// so the two must agree BIT FOR BIT -- not within a tolerance.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/TimeDerivative.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::transport::ScalarSourceFields;
using cfd::transport::TransportTimeState;

namespace {

BoundaryConditionSet zeroGradient(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BoundaryConditionSet fixedValueEverywhere(const Mesh& mesh, Real value) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(value));
  }
  return boundaries;
}

// A deliberately non-uniform state: a constant field would let a sign or
// indexing error cancel itself.
ScalarField varyingField(const Mesh& mesh, Real base, Real slope) {
  ScalarField field(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    field[i] = base + slope * static_cast<Real>(i);
  }
  return field;
}

SurfaceField varyingFlux(const Mesh& mesh, Real magnitude) {
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    // Alternating sign so BOTH upwind branches are exercised.
    flux[f] = (f % 3 == 0) ? -magnitude : magnitude;
  }
  return flux;
}

// Bitwise comparison of two assembled systems, structure included.
void expectBitwiseIdentical(const cfd::algebra::LinearSystem& a,
                            const cfd::algebra::LinearSystem& b) {
  const auto& A = a.matrix();
  const auto& B = b.matrix();
  ASSERT_EQ(A.rows(), B.rows());
  ASSERT_EQ(A.nonZeros(), B.nonZeros()) << "sparsity structure differs";
  // CSR compared directly: row offsets, column indices and values. Comparing
  // the raw arrays is what makes this a STRUCTURE check as well as a value
  // check -- two matrices can agree entry-by-entry and still differ in which
  // entries are stored at all.
  for (Index row = 0; row <= A.rows(); ++row) {
    ASSERT_EQ(A.rowOffsetsData()[row], B.rowOffsetsData()[row]) << "row offset " << row;
  }
  for (Index k = 0; k < A.nonZeros(); ++k) {
    EXPECT_EQ(A.columnIndicesData()[k], B.columnIndicesData()[k]) << "column index " << k;
    // EXPECT_EQ on doubles, deliberately: this is the bitwise gate.
    EXPECT_EQ(A.valuesData()[k], B.valuesData()[k]) << "value " << k;
  }
  ASSERT_EQ(a.rhs().size(), b.rhs().size());
  for (Index i = 0; i < a.rhs().size(); ++i) {
    EXPECT_EQ(a.rhs()[i], b.rhs()[i]) << "rhs " << i;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// THE GOLDEN GATE
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TRANSIENT (Phase B3/C)
// ---------------------------------------------------------------------------

TEST(TransportTransient, SteadyModeContributesNothing) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField phi = varyingField(mesh, 1.0, 0.1);
  const ScalarField gamma(mesh.numberOfCells(), 0.5);
  const SurfaceField flux = varyingFlux(mesh, 0.1);
  const ScalarField Su(mesh.numberOfCells(), 0.0);
  const ScalarField Sp(mesh.numberOfCells(), 0.0);

  const auto steady = cfd::transport::assembleScalarTransportEquation(mesh, phi, flux, gamma,
                                                                      zeroGradient(mesh), Su, Sp);
  // An explicitly steady TransportTimeState must be bitwise the same, not
  // merely close: a steady solve executes no transient code at all.
  const auto explicitlySteady = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, flux, gamma, zeroGradient(mesh), Su, Sp, {}, TransportTimeState{});

  expectBitwiseIdentical(steady.system, explicitlySteady.system);
}

TEST(TransportTransient, OneCellCoefficientMatchesHandCalculation) {
  // A single cell, so the coefficients are a hand calculation rather than an
  // assertion against the code's own output.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 2.0, 3.0);
  ASSERT_EQ(mesh.numberOfCells(), 1);
  const Real volume = mesh.cell(0).volume();
  EXPECT_DOUBLE_EQ(volume, 6.0);

  const ScalarField phiOld(1, 7.0);
  const ScalarField rhoOld(1, 2.0);
  const ScalarField rhoNew(1, 5.0);
  const Real dt = 0.25;

  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  TransportTimeState time;
  time.transient = true;
  time.dt = dt;
  time.phiOld = &phiOld;
  time.rhoOld = &rhoOld;
  time.rhoNew = &rhoNew;
  cfd::transport::applyTransientContribution(mesh, builder, rhs, time);

  // a_P = rho^{n+1} V / dt = 5 * 6 / 0.25 = 120
  // b_P = rho^n phi^n V / dt = 2 * 7 * 6 / 0.25 = 336
  EXPECT_DOUBLE_EQ(builder.build().diagonal(0), 120.0);
  EXPECT_DOUBLE_EQ(rhs[0], 336.0);
}

TEST(TransportTransient, OldTimeRhsUsesOldDensityNotNew) {
  // THE conservation trap: b_P must use rho^n, never rho^{n+1}. With
  // rho^n = 2 and rho^{n+1} = 5 the two differ by a factor of 2.5, so a wrong
  // implementation cannot pass by coincidence.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField phiOld(1, 3.0);
  const ScalarField rhoOld(1, 2.0);
  const ScalarField rhoNew(1, 5.0);

  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  TransportTimeState time;
  time.transient = true;
  time.dt = 1.0;
  time.phiOld = &phiOld;
  time.rhoOld = &rhoOld;
  time.rhoNew = &rhoNew;
  cfd::transport::applyTransientContribution(mesh, builder, rhs, time);

  EXPECT_DOUBLE_EQ(rhs[0], 2.0 * 3.0 * 1.0 / 1.0) << "b_P must use the OLD density";
  EXPECT_NE(rhs[0], 5.0 * 3.0 * 1.0 / 1.0) << "b_P used rho^{n+1}: not conservative";
}

TEST(TransportTransient, LargerTimestepWeakensTheTransientTerm) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField phiOld(mesh.numberOfCells(), 1.0);
  const ScalarField rho(mesh.numberOfCells(), 1.0);

  const auto diagonalFor = [&](Real dt) {
    SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
    Vector rhs(mesh.numberOfCells(), 0.0);
    TransportTimeState time;
    time.transient = true;
    time.dt = dt;
    time.phiOld = &phiOld;
    time.rhoOld = &rho;
    time.rhoNew = &rho;
    cfd::transport::applyTransientContribution(mesh, builder, rhs, time);
    return builder.build().diagonal(0);
  };

  EXPECT_GT(diagonalFor(0.1), diagonalFor(1.0));
  EXPECT_DOUBLE_EQ(diagonalFor(0.1) / diagonalFor(1.0), 10.0);
}

TEST(TransportTransient, RejectsIncompleteOrInvalidState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField field(mesh.numberOfCells(), 1.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  TransportTimeState missingOld;
  missingOld.transient = true;
  missingOld.dt = 1.0;
  missingOld.rhoOld = &field;
  missingOld.rhoNew = &field;
  EXPECT_THROW(cfd::transport::applyTransientContribution(mesh, builder, rhs, missingOld),
               InvalidArgumentError);

  TransportTimeState badDt;
  badDt.transient = true;
  badDt.dt = 0.0;
  badDt.phiOld = &field;
  badDt.rhoOld = &field;
  badDt.rhoNew = &field;
  EXPECT_THROW(cfd::transport::applyTransientContribution(mesh, builder, rhs, badDt),
               InvalidArgumentError);
}

// The additive ScalarField-density overload of the production time derivative.
TEST(TimeDerivativeFieldDensity, ConstantFieldReproducesScalarOverload) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField phiOld = varyingField(mesh, 2.0, 0.5);
  const Real rho = 1.234;
  const Real dt = 0.05;
  const ScalarField rhoField(mesh.numberOfCells(), rho);

  const auto scalarVersion =
      cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, rho, dt);
  const auto fieldVersion =
      cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, rhoField, rhoField, dt);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    // Bitwise: the field overload uses the same (rho * V) / dt operand order.
    EXPECT_EQ(fieldVersion.diagonal[i], scalarVersion.diagonal[i]) << "diagonal " << i;
    EXPECT_EQ(fieldVersion.source[i], scalarVersion.source[i]) << "source " << i;
  }
}

TEST(TimeDerivativeFieldDensity, UsesOldDensityForTheSourceTerm) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField phiOld(1, 4.0);
  const ScalarField rhoOld(1, 3.0);
  const ScalarField rhoNew(1, 9.0);

  const auto terms =
      cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, rhoOld, rhoNew, 2.0);
  EXPECT_DOUBLE_EQ(terms.diagonal[0], 9.0 * 1.0 / 2.0);
  EXPECT_DOUBLE_EQ(terms.source[0], (3.0 * 1.0 / 2.0) * 4.0);
}

// ---------------------------------------------------------------------------
// SOURCES (Phase B5/D)
// ---------------------------------------------------------------------------

TEST(TransportSource, SignConventionIsPatankar) {
  // a_P -= Sp*V  and  b_P += Su*V. A sign slip in either is immediately visible
  // here rather than as a slightly wrong converged field.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 2.0, 2.0);
  const Real volume = mesh.cell(0).volume();
  const ScalarField Su(1, 7.0);
  const ScalarField Sp(1, -3.0);

  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  cfd::transport::applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);

  EXPECT_DOUBLE_EQ(builder.build().diagonal(0), 3.0 * volume) << "a_P must GAIN -Sp*V";
  EXPECT_DOUBLE_EQ(rhs[0], 7.0 * volume) << "b_P must GAIN Su*V";
}

TEST(TransportSource, ZeroSourceChangesNothing) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  cfd::transport::applyImplicitScalarSource(mesh, builder, rhs, zero, zero);

  // The builder does not store zero-valued entries, so a zero source leaves
  // the matrix genuinely EMPTY rather than populated with zeros. Asserting
  // `diagonal(i) == 0` would throw ("no diagonal entry stored at row 0") --
  // the first version of this test did exactly that, which is why the
  // assertion is on nonZeros() now.
  EXPECT_EQ(builder.build().nonZeros(), 0) << "a zero source stored matrix entries";
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(rhs[i], 0.0) << "cell " << i;
  }
}

TEST(TransportSource, CellDependentSourceScalesWithVolume) {
  // Two cells of different volume, same source density -> different b_P.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 4.0, 1.0);
  const ScalarField Su(mesh.numberOfCells(), 2.5);
  const ScalarField Sp(mesh.numberOfCells(), 0.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  cfd::transport::applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(rhs[i], 2.5 * mesh.cell(i).volume());
  }
}

TEST(TransportSource, PositiveSpIsRejectedWhenRequestedAndNeverClamped) {
  // Phase B5: validated and reported, never silently clamped.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField Su(mesh.numberOfCells(), 0.0);
  ScalarField Sp(mesh.numberOfCells(), 0.0);
  Sp[2] = 1.0;  // positive -> destroys diagonal dominance

  SparseMatrixBuilder strict(mesh.numberOfCells(), mesh.numberOfCells());
  Vector strictRhs(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(cfd::transport::applyImplicitScalarSource(mesh, strict, strictRhs, Su, Sp,
                                                         /*requireNonPositiveSp=*/true),
               InvalidArgumentError);

  // Default off, and NOT clamped: the positive Sp reaches the matrix as a
  // negative diagonal contribution, exactly as the pre-existing turbulence
  // path allowed.
  SparseMatrixBuilder permissive(mesh.numberOfCells(), mesh.numberOfCells());
  Vector permissiveRhs(mesh.numberOfCells(), 0.0);
  cfd::transport::applyImplicitScalarSource(mesh, permissive, permissiveRhs, Su, Sp);
  EXPECT_DOUBLE_EQ(permissive.build().diagonal(2), -1.0 * mesh.cell(2).volume());
}

// ---------------------------------------------------------------------------
// BOUNDARY CONDITIONS (Phase B7/F)
// ---------------------------------------------------------------------------

TEST(TransportBoundary, FixedValueAndZeroGradientProduceDifferentSystems) {
  // Non-vacuity for the BC path: if the assembler ignored the BC type, these
  // two would be identical.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField phi = varyingField(mesh, 1.0, 0.3);
  const ScalarField gamma(mesh.numberOfCells(), 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);

  const auto dirichlet = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 1.0), zero, zero);
  const auto neumann = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, zeroGradient(mesh), zero, zero);

  bool anyDifference = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (dirichlet.diagonal[i] != neumann.diagonal[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference) << "the BC type did not affect the assembled system";
}

TEST(TransportBoundary, UniformFieldIsAnExactSolutionUnderAllZeroGradientWalls) {
  // With zero-gradient walls everywhere, no flux and no source, a UNIFORM
  // field must satisfy the discrete equation exactly: A*phi - b == 0.
  //
  // The first version of this test asserted instead that every matrix row sums
  // to zero, and it failed -- correctly. The zero-gradient condition here is
  // applied in DEFERRED form: Gamma|S|/d lands on the diagonal and the equal
  // and opposite term is put on the RHS through boundaryValueCoefficient *
  // phi_owner. The row sum is therefore NOT zero, while the equation remains
  // consistent. That assertion was testing a property the discretisation never
  // claimed; this one tests the property that actually matters.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Real uniform = 1.0;
  const ScalarField phi(mesh.numberOfCells(), uniform);
  const ScalarField gamma(mesh.numberOfCells(), 2.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);

  const auto assembly = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, zeroGradient(mesh), zero, zero);

  const Vector residual = assembly.system.matrix().multiply(Vector(phi.size(), uniform));
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(residual[i] - assembly.system.rhs()[i], 0.0, 1e-12)
        << "cell " << i << ": a uniform field is not an exact solution";
  }
}

TEST(TransportBoundary, RejectsNonScalarBoundaryCondition) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField phi(mesh.numberOfCells(), 1.0);
  const ScalarField gamma(mesh.numberOfCells(), 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);
  BoundaryConditionSet empty;
  EXPECT_THROW((void)cfd::transport::assembleScalarTransportEquation(mesh, phi, flux, gamma, empty,
                                                                     zero, zero),
               cfd::Error);
}

// ---------------------------------------------------------------------------
// VARIABLE PROPERTIES (Phase B6/E)
// ---------------------------------------------------------------------------

TEST(TransportProperties, VariableDiffusivityChangesTheMatrix) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const ScalarField phi(mesh.numberOfCells(), 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);
  const ScalarField uniform(mesh.numberOfCells(), 1.0);
  const ScalarField varying = varyingField(mesh, 1.0, 0.2);

  const auto a = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, uniform, fixedValueEverywhere(mesh, 0.0), zero, zero);
  const auto b = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, varying, fixedValueEverywhere(mesh, 0.0), zero, zero);

  bool differs = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (a.diagonal[i] != b.diagonal[i]) differs = true;
  }
  EXPECT_TRUE(differs) << "a varying diffusivity produced the uniform matrix";
}

TEST(TransportProperties, RejectsNonPositiveOrNonFiniteDiffusivity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField phi(mesh.numberOfCells(), 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);

  ScalarField negative(mesh.numberOfCells(), 1.0);
  negative[1] = -1.0;
  EXPECT_THROW((void)cfd::transport::assembleScalarTransportEquation(
                   mesh, phi, flux, negative, zeroGradient(mesh), zero, zero),
               InvalidArgumentError);

  ScalarField nan(mesh.numberOfCells(), 1.0);
  nan[0] = std::nan("");
  EXPECT_THROW((void)cfd::transport::assembleScalarTransportEquation(
                   mesh, phi, flux, nan, zeroGradient(mesh), zero, zero),
               InvalidArgumentError);
}

TEST(TransportProperties, HarmonicFaceDiffusivityMatchesTheClosedForm) {
  // Gamma_f = d_PN / (d_Pf/Gamma_P + d_fN/Gamma_N), which at a midpoint face is
  // 2 GP GN / (GP + GN). On a uniform Cartesian mesh every internal face IS a
  // midpoint face, so the closed form is the exact expected value.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 1, 4.0, 1.0);
  ScalarField gamma(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) gamma[i] = 1.0 + 9.0 * static_cast<Real>(i);

  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Real gP = gamma[face.owner()];
    const Real gN = gamma[*face.neighbor()];
    const Real harmonic = cfd::transport::faceDiffusivity(
        mesh, face, gamma, cfd::transport::FaceDiffusivityInterpolation::Harmonic);
    const Real arithmetic = cfd::transport::faceDiffusivity(
        mesh, face, gamma, cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
    EXPECT_NEAR(harmonic, 2.0 * gP * gN / (gP + gN), 1e-12) << "face " << f;
    EXPECT_NEAR(arithmetic, 0.5 * (gP + gN), 1e-12) << "face " << f;
    // The two must genuinely differ for an unequal pair, or this proves nothing.
    EXPECT_LT(harmonic, arithmetic) << "harmonic mean is not below arithmetic at face " << f;
  }
}

TEST(TransportProperties, HarmonicIsNotTheDefault) {
  // The default must stay arithmetic: switching it would move every existing
  // variable-diffusivity result.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);
  ScalarField gamma(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) gamma[i] = 1.0 + 50.0 * static_cast<Real>(i);
  const ScalarField phi(mesh.numberOfCells(), 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const ScalarField zero(mesh.numberOfCells(), 0.0);

  const auto byDefault = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero);
  const auto explicitArithmetic = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero, {}, {},
      cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
  const auto harmonic = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, noFlux, gamma, fixedValueEverywhere(mesh, 0.0), zero, zero, {}, {},
      cfd::transport::FaceDiffusivityInterpolation::Harmonic);

  expectBitwiseIdentical(byDefault.system, explicitArithmetic.system);
  bool differs = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (byDefault.diagonal[i] != harmonic.diagonal[i]) differs = true;
  }
  EXPECT_TRUE(differs) << "harmonic produced the arithmetic matrix -- the option does nothing";
}

// ---------------------------------------------------------------------------
// DETERMINISM
// ---------------------------------------------------------------------------

TEST(TransportFoundation, RepeatedAssemblyIsBitwiseIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  const ScalarField phi = varyingField(mesh, 2.0, 0.4);
  const ScalarField gamma = varyingField(mesh, 0.9, 0.02);
  const SurfaceField flux = varyingFlux(mesh, 0.3);
  const ScalarField Su = varyingField(mesh, 1.0, 0.1);
  const ScalarField Sp = varyingField(mesh, -1.0, -0.05);

  const auto first = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, flux, gamma, fixedValueEverywhere(mesh, 1.0), Su, Sp);
  const auto second = cfd::transport::assembleScalarTransportEquation(
      mesh, phi, flux, gamma, fixedValueEverywhere(mesh, 1.0), Su, Sp);

  expectBitwiseIdentical(first.system, second.system);
}
