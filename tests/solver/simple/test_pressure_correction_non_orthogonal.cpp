// P12-NUM-003 continuation: the geometric pressure-correction equation
// (PressureCorrectionEquation.hpp -- pressureCorrectionFaceCoupling /
// assembleGeometricPressureCorrection), which replaced the axis-aligned-
// faces-only P0 formulation. Verification against hand-derived geometry
// and exact discrete properties, not a re-derivation of the code.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::assemblePressureCorrection;
using cfd::pressure_velocity::correctFaceMassFlux;
using cfd::pressure_velocity::PressureCorrectionAssembly;
using cfd::pressure_velocity::pressureCorrectionFaceCoupling;
using cfd::pressure_velocity::PressureCorrectionOptions;

namespace {

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

// Smooth, genuinely anisotropic response coefficients d_u != d_v.
std::pair<ScalarField, ScalarField> responseFields(const Mesh& mesh) {
  ScalarField du(mesh.numberOfCells());
  ScalarField dv(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Vector2 c = cell.centroid();
    du[cell.id()] = 0.02 + (0.01 * c.x) + (0.005 * c.y);
    dv[cell.id()] = 0.03 - (0.01 * c.y) + (0.004 * c.x);
  }
  return {du, dv};
}

// A predictor mass flux with a genuine, non-uniform per-cell imbalance.
SurfaceField predictorFlux(const Mesh& mesh) {
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Vector2 c = face.centroid();
    const Vector2 u{std::sin(3.0 * c.y) + (0.3 * c.x * c.x), std::cos(2.0 * c.x) * c.y};
    flux[face.id()] = cfd::dot(u, face.areaVector());
  }
  return flux;
}

ScalarField solve(const PressureCorrectionAssembly& assembly, Index n) {
  cfd::algebra::LinearSolverSettings settings;
  settings.maxIterations = 20000;
  settings.absoluteTolerance = 1e-12;
  settings.relativeTolerance = 1e-11;
  settings.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  const cfd::algebra::BiCGSTAB solver(settings);
  const auto result = solver.solve(assembly.system);
  EXPECT_TRUE(result.converged());
  ScalarField p(n);
  for (Index i = 0; i < n; ++i) p[i] = result.solution[i];
  return p;
}

cfd::mesh::Mesh twoCellMesh(Vector2 owner, Vector2 neighbor, Vector2 faceCentroid, Vector2 sf) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, owner, 1.0);
  cells.emplace_back(1, neighbor, 1.0);
  std::vector<cfd::mesh::Face> faces;
  faces.emplace_back(0, 0, 1, faceCentroid, sf);
  cells[0].addFace(0);
  cells[1].addFace(0);
  return Mesh(std::move(cells), std::move(faces), {});
}

}  // namespace

// CartesianCompatibility: on createCartesian2D every face coefficient is
// EXACTLY the pre-P12-NUM-003 formula rho*|Sf|*d_comp/|d| (d_u on x-normal
// faces, d_v on y-normal faces; boundary faces with the owner value) --
// hand-coded here as an independent reference -- for both the two-point
// and the over-relaxed option, and the assembled systems are identical.
TEST(PressureCorrectionNonOrthogonalTest, CartesianCompatibility) {
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 1.0, 0.8);
  auto [du, dv] = responseFields(mesh);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(0.0));
  const SurfaceField flux = predictorFlux(mesh);
  const Real rho = 1.37;

  const auto twoPoint = assemblePressureCorrection(mesh, flux, du, dv, rho, 0, boundaries);
  PressureCorrectionOptions corrected;
  corrected.nonOrthogonal = true;
  const auto overRelaxed =
      assemblePressureCorrection(mesh, flux, du, dv, rho, 0, boundaries, corrected);

  std::vector<bool> dirichlet(mesh.numberOfFaces(), false);
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == "right" || patch.name() == "top") {
      for (const Index faceId : patch.faceIds()) dirichlet[faceId] = true;
    }
  }
  for (const auto& face : mesh.faces()) {
    const Vector2& sf = face.areaVector();
    const bool xNormal = sf.y == 0.0;
    Real legacy = 0.0;
    if (face.isBoundary()) {
      if (dirichlet[face.id()]) {
        const Real hP = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
        legacy = rho * face.area() * (xNormal ? du : dv)[face.owner()] / hP;
      }
    } else {
      const Real dFace =
          cfd::discretization::interpolateInternalFace(mesh, face, xNormal ? du : dv);
      legacy = rho * face.area() * dFace / MeshGeometry::ownerNeighborDistance(mesh, face);
    }
    EXPECT_EQ(twoPoint.faceCoefficient[face.id()], legacy) << "face " << face.id();
    EXPECT_EQ(overRelaxed.faceCoefficient[face.id()], legacy) << "face " << face.id();
  }
  ASSERT_EQ(twoPoint.system.matrix().nonZeros(), overRelaxed.system.matrix().nonZeros());
  for (Index k = 0; k < twoPoint.system.matrix().nonZeros(); ++k) {
    EXPECT_EQ(twoPoint.system.matrix().valuesData()[k],
              overRelaxed.system.matrix().valuesData()[k]);
  }
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(twoPoint.system.rhs()[i], overRelaxed.system.rhs()[i]);
  }

  // An explicit pass on a Cartesian mesh adds exactly nothing (T == 0).
  const ScalarField previous(mesh.numberOfCells(), 0.7);
  corrected.previousPressureCorrection = &previous;
  const auto explicitPass =
      assemblePressureCorrection(mesh, flux, du, dv, rho, 0, boundaries, corrected);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    EXPECT_EQ(explicitPass.explicitFaceFlux[f], 0.0);
  }
}

// NonOrthogonalInternalFace, hand-derived: d = (1,0), Sf at 30 degrees,
// d_u = 2, d_v = 1 (uniform, so the face interpolation is exact), rho = 1.5.
//   S_D = (2 cos30, sin30); E = (S_D.S_D / d.S_D) d; T = S_D - E
//   over-relaxed: coefficient = rho |E| / |d|; explicit vector = rho T
//   two-point:    coefficient = rho |S_D| / |d|, no explicit part
TEST(PressureCorrectionNonOrthogonalTest, NonOrthogonalInternalFace) {
  const Real theta = cfd::constants::pi / 6.0;
  const Vector2 sf{std::cos(theta), std::sin(theta)};
  const Mesh mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, sf);
  const ScalarField du(2, 2.0);
  const ScalarField dv(2, 1.0);
  const Real rho = 1.5;

  const Vector2 sd{2.0 * sf.x, 1.0 * sf.y};
  const Real eMagnitude = cfd::dot(sd, sd) / sd.x;  // (S_D.S_D / d.S_D) * |d|, d = (1,0)
  const Vector2 t{sd.x - eMagnitude, sd.y};

  const auto overRelaxed = pressureCorrectionFaceCoupling(mesh, mesh.face(0), rho, du, dv, true);
  EXPECT_NEAR(overRelaxed.coefficient, rho * eMagnitude, 1e-14);
  EXPECT_NEAR(overRelaxed.nonOrthogonal.x, rho * t.x, 1e-14);
  EXPECT_NEAR(overRelaxed.nonOrthogonal.y, rho * t.y, 1e-14);
  EXPECT_GT(overRelaxed.coefficient, 0.0);

  const auto twoPoint = pressureCorrectionFaceCoupling(mesh, mesh.face(0), rho, du, dv, false);
  EXPECT_NEAR(twoPoint.coefficient, rho * cfd::magnitude(sd), 1e-14);
  EXPECT_EQ(twoPoint.nonOrthogonal.x, 0.0);
  EXPECT_EQ(twoPoint.nonOrthogonal.y, 0.0);
  // Over-relaxed is never a weaker implicit coefficient.
  EXPECT_GE(overRelaxed.coefficient, twoPoint.coefficient);
}

// NonOrthogonalConservation: on a distorted mesh, after solving an explicit
// pass (previous p' supplied), correctFaceMassFlux WITH the assembly's own
// explicit face terms satisfies the discrete continuity equation in every
// non-reference cell to solver precision; leaving the explicit terms out
// does not -- the flux correction must use the same face terms as the
// matrix/RHS (one coupling representation).
TEST(PressureCorrectionNonOrthogonalTest, NonOrthogonalConservation) {
  const Index n = 9;
  const Mesh mesh = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.45 / static_cast<Real>(n));
  const auto boundaries = zeroGradientPressure(mesh);
  auto [du, dv] = responseFields(mesh);
  const SurfaceField flux = predictorFlux(mesh);

  PressureCorrectionOptions options;
  options.nonOrthogonal = true;
  options.gradientScheme = cfd::discretization::GradientScheme::LeastSquares;
  const auto pass1 = assemblePressureCorrection(mesh, flux, du, dv, 1.0, 0, boundaries, options);
  const ScalarField p1 = solve(pass1, mesh.numberOfCells());
  options.previousPressureCorrection = &p1;
  const auto pass2 = assemblePressureCorrection(mesh, flux, du, dv, 1.0, 0, boundaries, options);
  const ScalarField p2 = solve(pass2, mesh.numberOfCells());

  Real maxExplicit = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    maxExplicit = std::max(maxExplicit, std::abs(pass2.explicitFaceFlux[f]));
  }
  EXPECT_GT(maxExplicit, 1e-6) << "the explicit non-orthogonal term should be active";

  const auto consistent = cfd::physics::evaluateContinuity(
      mesh, correctFaceMassFlux(mesh, flux, pass2.faceCoefficient, p2, &pass2.explicitFaceFlux));
  const auto inconsistent = cfd::physics::evaluateContinuity(
      mesh, correctFaceMassFlux(mesh, flux, pass2.faceCoefficient, p2));
  Real maxConsistent = 0.0;
  Real maxInconsistent = 0.0;
  for (Index i = 1; i < mesh.numberOfCells(); ++i) {  // cell 0 is the pinned reference cell
    maxConsistent = std::max(maxConsistent, std::abs(consistent.cellImbalance[i]));
    maxInconsistent = std::max(maxInconsistent, std::abs(inconsistent.cellImbalance[i]));
  }
  std::printf(
      "\nPressure correction pass 2, distorted 9x9 0.45h: max |explicit face flux| %.3g;"
      " max cell continuity with explicit terms in the flux update %.3g, without %.3g\n",
      maxExplicit, maxConsistent, maxInconsistent);
  EXPECT_LT(maxConsistent, 1e-10);
  EXPECT_GT(maxInconsistent, 1e2 * maxConsistent);
  // Closed domain: the corrected flux has zero net boundary flux.
  EXPECT_NEAR(consistent.globalNetFlux, 0.0, 1e-14);
}

// NonOrthogonalBoundary: a FixedValue (Dirichlet) pressure face on a
// distorted mesh gets the geometric coupling built from the OWNER's d_u,
// d_v and d = x_face - x_owner (checked against the shared formula with
// those inputs); a Neumann face gets exactly zero coupling and zero
// explicit flux.
TEST(PressureCorrectionNonOrthogonalTest, NonOrthogonalBoundary) {
  const Index n = 8;
  const Mesh mesh = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.45 / static_cast<Real>(n));
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  auto [du, dv] = responseFields(mesh);
  const SurfaceField flux = predictorFlux(mesh);
  const ScalarField previous = [&] {
    ScalarField p(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) p[cell.id()] = std::sin(2.0 * cell.centroid().y);
    return p;
  }();
  PressureCorrectionOptions options;
  options.nonOrthogonal = true;
  options.previousPressureCorrection = &previous;
  const auto assembly = assemblePressureCorrection(mesh, flux, du, dv, 1.2, 0, boundaries, options);

  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (patch.name() == "right") {
        const Vector2 d = face.centroid() - mesh.cell(face.owner()).centroid();
        const Vector2 sd{du[face.owner()] * face.areaVector().x,
                         dv[face.owner()] * face.areaVector().y};
        const auto decomposition = MeshGeometry::decomposeAreaVector(d, sd);
        ASSERT_TRUE(decomposition.valid);
        EXPECT_NEAR(assembly.faceCoefficient[faceId],
                    1.2 * cfd::magnitude(decomposition.orthogonal) / cfd::magnitude(d),
                    1e-13 * assembly.faceCoefficient[faceId]);
        EXPECT_GT(assembly.faceCoefficient[faceId], 0.0);
      } else {
        EXPECT_EQ(assembly.faceCoefficient[faceId], 0.0) << patch.name();
        EXPECT_EQ(assembly.explicitFaceFlux[faceId], 0.0) << patch.name();
      }
    }
  }
}

// DegenerateGeometryRejected: a face whose response vector is perpendicular
// to d cannot be split (decomposition invalid) -- it falls back to the
// finite two-point coupling, never NaN/Inf; coincident owner/neighbor
// centroids (|d| = 0) make the coupling non-finite, which the coupling
// detects and rejects with NumericalError (SIMPLE reports NonFiniteState).
TEST(PressureCorrectionNonOrthogonalTest, DegenerateGeometryRejected) {
  {
    const Mesh mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, {0.0, 1.0});
    const ScalarField d(2, 1.0);
    const auto coupling = pressureCorrectionFaceCoupling(mesh, mesh.face(0), 1.0, d, d, true);
    EXPECT_TRUE(std::isfinite(coupling.coefficient));
    EXPECT_NEAR(coupling.coefficient, 1.0, 1e-14);  // two-point |S_D|/|d| fallback
    EXPECT_EQ(coupling.nonOrthogonal.x, 0.0);
    EXPECT_EQ(coupling.nonOrthogonal.y, 0.0);
  }
  {
    std::vector<cfd::mesh::Cell> cells;
    cells.emplace_back(0, Vector2{0.5, 0.5}, 1.0);
    cells.emplace_back(1, Vector2{0.5, 0.5}, 1.0);
    std::vector<cfd::mesh::Face> faces;
    faces.emplace_back(0, 0, 1, Vector2{0.5, 0.5}, Vector2{0.3, 0.4});
    faces.emplace_back(1, 0, std::nullopt, Vector2{0.0, 0.5}, Vector2{-1.0, 0.0});
    faces.emplace_back(2, 1, std::nullopt, Vector2{1.0, 0.5}, Vector2{1.0, 0.0});
    cells[0].addFace(0);
    cells[0].addFace(1);
    cells[1].addFace(0);
    cells[1].addFace(2);
    std::vector<cfd::mesh::BoundaryPatch> patches;
    patches.emplace_back("left", std::vector<Index>{1});
    patches.emplace_back("right", std::vector<Index>{2});
    const Mesh mesh(std::move(cells), std::move(faces), std::move(patches));
    const auto boundaries = zeroGradientPressure(mesh);
    const ScalarField d(2, 1.0);
    const SurfaceField flux(mesh.numberOfFaces(), 0.0);
    EXPECT_THROW((void)assemblePressureCorrection(mesh, flux, d, d, 1.0, 0, boundaries),
                 cfd::NumericalError);
  }
}
