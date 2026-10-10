// P14 migration -- FROZEN CHARACTERIZATION of the end-to-end turbulence
// scalar solve.
//
// WHAT HAPPENED, AND WHY THIS FILE LOOKS LIKE THIS NOW
// ----------------------------------------------------
// This file began as the migration's equivalence gate: it compared the
// production wrapper driven through `cfd::turbulence`'s own assembly loops
// against the same wrapper driven through `cfd::transport`, bitwise, with and
// without imposed cells and under non-orthogonal correction. Those tests did
// their job -- all six production turbulence equations moved onto
// `cfd::transport` with every comparison green -- and then RETIRED WITH THE
// IMPLEMENTATION THEY COMPARED AGAINST. There is one assembly now, so there is
// nothing left to compare it to.
//
// What replaces them is the last thing those tests proved, frozen: the solved
// field this path produced at the moment it was still provably identical to the
// implementation that has since been deleted.
//
// THESE ARE CHARACTERIZATION VALUES, NOT MATHEMATICAL TRUTH.
// They record what the proven-equivalent implementation did. If a future change
// turns them red, the question to answer is "was the discretisation meant to
// change?" -- and if it was, the response is to RE-DERIVE and justify new
// values, not to nudge these until the test passes. Red here means "the solve
// moved", not "the solver is wrong".
//
// WHAT THIS LAYER ADDS over the matrix/RHS characterization in
// tests/unit/transport/test_transport_characterization.cpp: everything the
// wrapper does AFTER assembly, which `cfd::transport` deliberately never took
// over -- Patankar under-relaxation, `fixRow` imposed cells applied after
// relaxation, the linear solve, the floor, and the re-assertion of imposed
// values over that floor. A matrix-only oracle would not notice if any of that
// changed.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"
#include "cfd/transport/ScalarTransport.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::ImposedCell;

namespace {

// Captured at %.17g, which round-trips double exactly. Cells 0, 7 and 11 are
// the imposed values (4, 9.25, 0.5) surviving the floor -- that they appear
// here verbatim is part of what this oracle protects.
constexpr Real kFrozenSolvedField[] = {
    4,
    3.2514012833490478,
    1.2388162075014308,
    0.51804699246246932,
    0.38528143352451999,
    0.38181541263602636,
    1.1442205621908434,
    9.25,
    1.5167840604261591,
    0.62046568467001317,
    0.49164991354199722,
    0.5,
    0.65308017916289884,
    2.3790699215445703,
    0.89061145988073886,
    0.62339984423843142,
    0.5584874655678731,
    0.59789050141905042,
    0.61920739880171394,
    1.0490451629190984,
    0.70688723496858408,
    0.62365833633446255,
    0.61990046910622898,
    0.70463986654062416,
    0.5904052793877641,
    0.64718332588411065,
    0.57539956473042364,
    0.56401229214380577,
    0.59222834244534373,
    0.67009469170360458,
};

struct Case {
  Mesh mesh;
  BoundaryConditionSet boundaries;
  ScalarField phi;
  ScalarField diffusivity;
  ScalarField su;
  ScalarField sp;
  SurfaceField massFlux;
};

// The frozen case. Changing ANY of this invalidates the values above.
Case makeCase() {
  Case c{MeshGeometry::createCartesian2D(6, 5, 3.0, 1.0), {}, {}, {}, {}, {}, {}};
  c.boundaries.set(c.mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.7));
  c.boundaries.set(c.mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.boundaries.set(c.mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.boundaries.set(c.mesh, "top", std::make_unique<cfd::boundary::FixedValue>(0.25));

  const Index n = c.mesh.numberOfCells();
  c.phi = ScalarField(n);
  c.diffusivity = ScalarField(n);
  c.su = ScalarField(n);
  c.sp = ScalarField(n);
  for (Index i = 0; i < n; ++i) {
    const Real t = static_cast<Real>(i) / static_cast<Real>(n);
    c.phi[i] = 0.3 + (0.9 * t);
    c.diffusivity[i] = 0.02 + (0.05 * t);  // variable, so face interpolation matters
    c.su[i] = 0.4 * (1.0 - t);
    c.sp[i] = -0.6 - (0.2 * t);  // Patankar-negative, as production uses
  }
  c.massFlux = SurfaceField(c.mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    c.massFlux[f] = 0.05 * std::sin(static_cast<Real>(f));
  }
  return c;
}

constexpr Real kAlpha = 0.7;
constexpr Real kFloor = 1e-10;

cfd::algebra::LinearSolverSettings solverSettings() {
  cfd::algebra::LinearSolverSettings s;
  s.absoluteTolerance = 1e-12;
  s.relativeTolerance = 1e-10;
  s.maxIterations = 500;
  return s;
}

const std::vector<ImposedCell>& frozenImposedCells() {
  static const std::vector<ImposedCell> cells = {{0, 4.0}, {7, 9.25}, {11, 0.5}};
  return cells;
}

// The wrapper's own sequence, reproduced so the non-vacuity control below can
// vary ONE thing (the face-diffusivity rule) and nothing else.
ScalarField solveWith(const Case& c, cfd::transport::FaceDiffusivityInterpolation interpolation) {
  const Index n = c.mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  cfd::transport::assembleScalarDiffusionContribution(c.mesh, c.diffusivity, c.phi, c.boundaries,
                                                      builder, rhs, {}, interpolation);
  cfd::transport::assembleScalarConvectionContribution(c.mesh, c.massFlux, c.phi, c.boundaries,
                                                       builder, rhs);
  cfd::transport::applyImplicitScalarSource(c.mesh, builder, rhs, c.su, c.sp);

  Vector phiOld(n, 0.0);
  for (Index i = 0; i < n; ++i) phiOld[i] = c.phi[i];
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, phiOld, kAlpha);
  for (const ImposedCell& cell : frozenImposedCells()) {
    builder.fixRow(cell.cell);
    rhs[cell.cell] = cell.value;
  }
  const cfd::algebra::BiCGSTAB solver(solverSettings());
  const auto result = solver.solve(cfd::algebra::LinearSystem(builder.build(), rhs), phiOld);
  ScalarField solved(n);
  for (Index i = 0; i < n; ++i) solved[i] = std::max(result.solution[i], kFloor);
  for (const ImposedCell& cell : frozenImposedCells()) solved[cell.cell] = cell.value;
  return solved;
}

}  // namespace

TEST(P14Migration, FrozenSolvedFieldCharacterization) {
  const Case c = makeCase();
  const ScalarField solved = cfd::turbulence::solveRelaxedScalarTransport(
      c.mesh, c.phi, c.massFlux, c.diffusivity, c.boundaries, c.su, c.sp, kAlpha, kFloor,
      solverSettings(), {}, frozenImposedCells());

  ASSERT_EQ(solved.size(), std::size(kFrozenSolvedField));
  for (Index i = 0; i < solved.size(); ++i) {
    // EXPECT_EQ on doubles, deliberately: this is the bitwise oracle.
    EXPECT_EQ(solved[i], kFrozenSolvedField[i]) << "cell " << i;
  }
}

TEST(P14Migration, ImposedValuesSurviveTheFloorExactly) {
  // Stated separately because it is the one piece of the post-assembly sequence
  // a matrix oracle can never see: an imposed value is a boundary condition,
  // not a solver output, so the floor must not move it. A wall epsilon can
  // genuinely sit below the floor and would otherwise be silently replaced by
  // it, and the cell would stop satisfying the wall function that put it there.
  const Case c = makeCase();
  const ScalarField solved = cfd::turbulence::solveRelaxedScalarTransport(
      c.mesh, c.phi, c.massFlux, c.diffusivity, c.boundaries, c.su, c.sp, kAlpha, kFloor,
      solverSettings(), {}, frozenImposedCells());
  for (const ImposedCell& cell : frozenImposedCells()) {
    EXPECT_EQ(solved[cell.cell], cell.value) << "imposed cell " << cell.cell;
  }
}

TEST(P14Migration, TheFrozenSolvedFieldCanDetectAnAssemblyChange) {
  // NON-VACUITY CONTROL, carried over unchanged from the migration gate. The
  // test above compares against constants; constants that happened to be
  // insensitive would pass forever. Harmonic face diffusivity is a REAL
  // discretisation change on a case built with a variable diffusivity, and it
  // must reach the solved field.
  const Case c = makeCase();
  const ScalarField arithmetic =
      solveWith(c, cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
  const ScalarField harmonic = solveWith(c, cfd::transport::FaceDiffusivityInterpolation::Harmonic);

  // The reproduction must itself match the frozen values, or it is not the
  // production sequence and the control tests nothing relevant.
  ASSERT_EQ(arithmetic.size(), std::size(kFrozenSolvedField));
  for (Index i = 0; i < arithmetic.size(); ++i) {
    ASSERT_EQ(arithmetic[i], kFrozenSolvedField[i]) << "reproduction diverged at cell " << i;
  }

  bool anyDifference = false;
  for (Index i = 0; i < harmonic.size(); ++i) {
    if (harmonic[i] != kFrozenSolvedField[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference)
      << "harmonic face diffusivity reproduced the frozen arithmetic field, so this "
         "characterization cannot detect an assembly change and proves nothing";
}
