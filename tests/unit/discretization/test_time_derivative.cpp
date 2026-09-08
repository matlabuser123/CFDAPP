#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "ManufacturedFields.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/TimeDerivative.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/solver/TimeController.hpp"

using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Cell;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();
constexpr Real kInf = std::numeric_limits<Real>::infinity();

// Rebuilds `base` with each cell's volume replaced by `volumes[cell.id()]`
// -- everything else (faces, topology, boundary patches) unchanged. Used
// only to test implicitEulerTimeDerivative's per-cell rho*V/dt scaling
// against genuinely different volumes; MeshGeometry::createCartesian2D
// itself only ever generates uniform cells (equal volume everywhere), so
// there is no other way to get a small, valid, heterogeneous-volume mesh
// for this test.
Mesh meshWithVolumes(const Mesh& base, const std::vector<Real>& volumes) {
  std::vector<Cell> cells;
  cells.reserve(base.numberOfCells());
  for (const auto& cell : base.cells()) {
    Cell replacement(cell.id(), cell.centroid(), volumes.at(cell.id()));
    for (const cfd::Index faceId : cell.faceIds()) {
      replacement.addFace(faceId);
    }
    cells.push_back(std::move(replacement));
  }
  return Mesh(std::move(cells), base.faces(), base.boundaryPatches());
}

}  // namespace

TEST(TimeDerivativeTest, ExactCoefficientsForSingleCell) {
  // rho=2, V=0.5, dt=0.1, phiOld=3 -> aP,time = rho*V/dt = 10,
  // b_time = aP,time * phiOld = 30.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 0.5, 1.0);  // V = 0.5*1.0 = 0.5
  const ScalarField phiOld(1, 3.0);

  const auto coeffs = cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 2.0, 0.1);

  ASSERT_EQ(coeffs.diagonal.size(), 1u);
  ASSERT_EQ(coeffs.source.size(), 1u);
  EXPECT_DOUBLE_EQ(coeffs.diagonal[0], 10.0);
  EXPECT_DOUBLE_EQ(coeffs.source[0], 30.0);
}

TEST(TimeDerivativeTest, DifferentVolumesScaleDiagonalAndSourceIndependently) {
  const Mesh base = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const Mesh mesh = meshWithVolumes(base, {1.0, 3.0});
  ScalarField phiOld(2);
  phiOld[0] = 4.0;
  phiOld[1] = 5.0;

  const auto coeffs = cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, 0.5);

  EXPECT_DOUBLE_EQ(coeffs.diagonal[0], 2.0);  // 1.0 * 1.0 / 0.5
  EXPECT_DOUBLE_EQ(coeffs.diagonal[1], 6.0);  // 1.0 * 3.0 / 0.5
  EXPECT_DOUBLE_EQ(coeffs.source[0], coeffs.diagonal[0] * 4.0);
  EXPECT_DOUBLE_EQ(coeffs.source[1], coeffs.diagonal[1] * 5.0);
}

TEST(TimeDerivativeTest, PreviousFieldIsNotMutated) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  ScalarField phiOld(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    phiOld[cell.id()] = static_cast<Real>(cell.id()) + 1.0;
  }
  const ScalarField phiOldCopy = phiOld;

  const auto coeffs = cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, 0.2);
  static_cast<void>(coeffs);

  for (const auto& cell : mesh.cells()) {
    EXPECT_DOUBLE_EQ(phiOld[cell.id()], phiOldCopy[cell.id()]);
  }
}

TEST(TimeDerivativeTest, RejectsFieldSizeMismatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField wrongSize(mesh.numberOfCells() - 1, 1.0);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, wrongSize, 1.0, 0.1),
               cfd::InvalidArgumentError);
}

TEST(TimeDerivativeTest, RejectsNonPositiveDeltaT) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField phiOld(1, 1.0);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, 0.0),
               cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, -0.1),
               cfd::InvalidArgumentError);
}

TEST(TimeDerivativeTest, RejectsNonFiniteOrNonPositiveDensity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField phiOld(1, 1.0);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, kNaN, 0.1),
               cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, kInf, 0.1),
               cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 0.0, 0.1),
               cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, -1.0, 0.1),
               cfd::InvalidArgumentError);
}

TEST(TimeDerivativeTest, RejectsNonFiniteDeltaT) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField phiOld(1, 1.0);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, kNaN),
               cfd::InvalidArgumentError);
  EXPECT_THROW(cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, kInf),
               cfd::InvalidArgumentError);
}

namespace {

// Advances phi(t) governed by dphi/dt = -lambda*phi from t=0 to
// t=finalTime using implicitEulerTimeDerivative on a trivial single-cell,
// unit-volume, unit-density mesh, stepping physical time via
// TimeController -- exercising TASK P2-001 and P2-002 together,
// deliberately without SIMPLE/PISO/TransientSolver (TODO.md P2 section
// 10: "prefer a reusable discretization component"). The reaction term
// -lambda*phi is added directly into the implicit equation each step:
//   aP,time*phi_new + lambda*V*phi_new = b_time
//   => phi_new = b_time / (aP,time + lambda*V)
// which for V=1 is algebraically identical to the closed-form implicit
// Euler update phi_new = phi_old / (1 + lambda*dt) -- this function does
// not special-case that; the equality is a consequence of
// implicitEulerTimeDerivative's own coefficients, not asserted separately.
Real solveDecayOde(Real lambda, Real phi0, Real dt, Real finalTime) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);  // V = 1
  ScalarField phi(1, phi0);
  cfd::solver::TimeController controller(0.0, finalTime, dt, 1'000'000);

  while (!controller.finished()) {
    const Real stepDt = controller.deltaT();
    const auto coeffs = cfd::discretization::implicitEulerTimeDerivative(mesh, phi, 1.0, stepDt);

    ScalarField phiNew(1);
    for (const auto& cell : mesh.cells()) {
      const Real aP = coeffs.diagonal[cell.id()] + (lambda * cell.volume());
      phiNew[cell.id()] = coeffs.source[cell.id()] / aP;
    }
    phi = phiNew;
    controller.advance();
  }
  return phi[0];
}

}  // namespace

TEST(TimeDerivativeTest, ImplicitEulerOdeConvergesAtFirstOrder) {
  const Real lambda = 1.0;
  const Real phi0 = 1.0;
  const Real finalTime = 1.0;
  const Real analytical = phi0 * std::exp(-lambda * finalTime);

  const std::vector<Real> deltaTs = {0.1, 0.05, 0.025, 0.0125};
  std::vector<Real> errors;
  errors.reserve(deltaTs.size());
  for (const Real dt : deltaTs) {
    const Real numerical = solveDecayOde(lambda, phi0, dt, finalTime);
    errors.push_back(std::abs(numerical - analytical));
  }

  for (std::size_t i = 1; i < errors.size(); ++i) {
    EXPECT_LT(errors[i], errors[i - 1]);
    const Real p = cfd::test::observedOrder(errors[i - 1], errors[i]);
    EXPECT_GT(p, 0.8) << "observed temporal order too low between deltaT=" << deltaTs[i - 1]
                      << " and " << deltaTs[i];
    EXPECT_LT(p, 1.3) << "observed temporal order suspiciously high (expected ~1) between deltaT="
                      << deltaTs[i - 1] << " and " << deltaTs[i];
  }
}
