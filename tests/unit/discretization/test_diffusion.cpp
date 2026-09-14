#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Laplacian.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TEST(DiffusionTest, EqualsGammaTimesLaplacianForConstantGamma) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const Real gamma = 2.0;
  const auto diff = cfd::discretization::diffusion(mesh, field, gamma, boundaries);
  const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], gamma * lap[cell.id()], 1e-9);
  }
}

TEST(DiffusionTest, QuadraticFieldWithGammaHalf) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto diff = cfd::discretization::diffusion(mesh, field, 0.5, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], 2.0, 1e-9);  // 0.5 * 4
  }
}

TEST(DiffusionTest, QuadraticFieldWithGammaTwo) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto diff = cfd::discretization::diffusion(mesh, field, 2.0, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], 8.0, 1e-9);
  }
}

TEST(DiffusionTest, ZeroFluxBoundaryConservesGlobally) {
  // Constant field -> zero gradient everywhere -> internal fluxes cancel
  // pairwise (face-once conservation) and there is no boundary flux, so
  // the volume-weighted sum must be exactly zero.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 5.0; });
  const ScalarField field(mesh.numberOfCells(), 5.0);

  const auto diff = cfd::discretization::diffusion(mesh, field, 3.0, boundaries);
  Real sum = 0.0;
  for (const auto& cell : mesh.cells()) {
    sum += diff[cell.id()] * cell.volume();
  }
  EXPECT_NEAR(sum, 0.0, 1e-10);
}

// ===========================================================================
// P12-NUM-003: non-orthogonal correction (applyNonOrthogonalCorrection).
// ===========================================================================

namespace {

// A cell with at least one boundary face has its diffusion value dominated
// by this codebase's pre-existing, unchanged, orthogonal-mesh-specific
// boundary flux formula (see Diffusion.hpp's own header comment) --
// distinguishing interior from boundary-adjacent cells is exactly how
// P12-NUM-001/002/003 all separate "genuine correction accuracy" from
// "unrelated, out-of-scope boundary formula error" (see
// results/p12-num-003/summary.md for the full explanation).
bool isInteriorCell(const cfd::mesh::Mesh& mesh, const cfd::mesh::Cell& cell) {
  for (const cfd::Index faceId : cell.faceIds()) {
    if (mesh.face(faceId).isBoundary()) {
      return false;
    }
  }
  return true;
}

}  // namespace

// OrthogonalCompatibility: on a Cartesian mesh S_nonorth is exactly {0,0}
// on every internal face (MeshGeometry::decomposeFaceArea's own
// documented guarantee), so turning the correction on must reproduce the
// uncorrected result bit-for-bit (not just "close") -- the corrected
// internal-face formula reduces algebraically to the uncorrected one.
TEST(DiffusionTest, NonOrthogonalCorrectionMatchesBaselineOnCartesianMesh) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.5, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.5, boundaries, true,
                                                        GradientScheme::GreenGauss);

  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(uncorrected[cell.id()], corrected[cell.id()]) << "cell " << cell.id();
  }
}

// The default parameters (no flag passed at all) must be byte-identical
// to an explicit applyNonOrthogonalCorrection=false call -- confirming
// every pre-existing call site (which never passes the new parameters)
// is completely unaffected by this task.
TEST(DiffusionTest, DefaultParametersPreserveUncorrectedBehavior) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.05);
  const auto meshWithBoundaries = cfd::test::perFaceBoundaryMesh(mesh);
  const auto boundaries =
      cfd::test::makeExactBoundaries(meshWithBoundaries, cfd::test::phiQuadratic);

  ScalarField field(meshWithBoundaries.numberOfCells());
  for (const auto& cell : meshWithBoundaries.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto withDefaults =
      cfd::discretization::diffusion(meshWithBoundaries, field, 1.0, boundaries);
  const auto explicitFalse =
      cfd::discretization::diffusion(meshWithBoundaries, field, 1.0, boundaries, false);

  for (const auto& cell : meshWithBoundaries.cells()) {
    EXPECT_EQ(withDefaults[cell.id()], explicitFalse[cell.id()]);
  }
}

// LinearFieldCartesian: div(grad(linear)) == 0 exactly, on a Cartesian
// mesh, regardless of the correction flag (S_nonorth == 0 there anyway).
TEST(DiffusionTest, LinearFieldGivesZeroDiffusionOnCartesianRegardlessOfCorrection) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(corrected[cell.id()], 0.0, 1e-10);
  }
}

// LinearFieldDistorted: div(grad(linear)) is not exactly reproduced at
// boundary-adjacent cells (their value is dominated by the unchanged,
// orthogonal-mesh-specific boundary flux formula -- out of scope for this
// task, see Diffusion.hpp), but at INTERIOR cells the corrected internal
// flux must reproduce the exact zero Laplacian to near machine precision
// -- a direct exactness check, distinct from (and a sanity check
// alongside) the grid-refinement convergence-order study.
TEST(DiffusionTest, LinearFieldNearZeroInInteriorOnDistortedMeshWhenCorrected) {
  const Mesh base = cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.3 * 0.1);
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);

  Index interiorCount = 0;
  for (const auto& cell : mesh.cells()) {
    if (!isInteriorCell(mesh, cell)) {
      continue;
    }
    ++interiorCount;
    EXPECT_NEAR(corrected[cell.id()], 0.0, 1e-8) << "interior cell " << cell.id();
  }
  ASSERT_GT(interiorCount, 0);
}

// NonOrthogonalCorrection (accuracy demonstration): for a quadratic field
// (exact Laplacian = 4 everywhere for gamma=1), the corrected scheme's
// INTERIOR-cell error on a distorted mesh must be substantially smaller
// than the uncorrected scheme's -- a single, easy-to-audit snapshot
// alongside the full grid-refinement convergence-order study below. Not
// exactly zero even in the interior: the correction's own face-gradient
// interpolation is only exact for a LINEAR field (see
// LinearFieldNearZeroInInteriorOnDistortedMeshWhenCorrected above); for a
// genuinely curved (quadratic) field it leaves a real, expected O(h^2)
// truncation residual, just a much smaller one than uncorrected.
TEST(DiffusionTest, CorrectionReducesInteriorErrorForQuadraticFieldOnDistortedMesh) {
  const Mesh base = cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.3 * 0.1);
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);

  Real maxErrorUncorrected = 0.0;
  Real maxErrorCorrected = 0.0;
  Index interiorCount = 0;
  for (const auto& cell : mesh.cells()) {
    if (!isInteriorCell(mesh, cell)) {
      continue;
    }
    ++interiorCount;
    maxErrorUncorrected = std::max(maxErrorUncorrected, std::abs(uncorrected[cell.id()] - 4.0));
    maxErrorCorrected = std::max(maxErrorCorrected, std::abs(corrected[cell.id()] - 4.0));
  }
  ASSERT_GT(interiorCount, 0);
  EXPECT_LT(maxErrorCorrected, 0.1 * maxErrorUncorrected);
}

// InternalFaceConservation: a constant field has an exactly-zero gradient
// under both gradient schemes (P12-NUM-002's own verified exactness
// property), so the non-orthogonal correction term (S_nonorth . grad_f)
// is exactly zero on every internal face even though S_nonorth itself is
// genuinely nonzero on this distorted mesh -- the correction introduces
// no artificial source/sink, and face-once accumulation (this codebase's
// existing conservation architecture, see Diffusion.hpp's own header
// comment) still makes the volume-weighted global sum exactly zero.
TEST(DiffusionTest, ConstantFieldConservesGloballyOnDistortedMeshWithCorrection) {
  const Mesh base = cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.05);
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 5.0; });
  const ScalarField field(mesh.numberOfCells(), 5.0);

  const auto corrected = cfd::discretization::diffusion(mesh, field, 3.0, boundaries, true,
                                                        GradientScheme::LeastSquares);
  Real sum = 0.0;
  for (const auto& cell : mesh.cells()) {
    sum += corrected[cell.id()] * cell.volume();
  }
  EXPECT_NEAR(sum, 0.0, 1e-9);
}

// StrongDistortionFinite: even a strongly distorted mesh (well below the
// self-intersection threshold, but with substantial non-orthogonality per
// MeshQuality) must produce entirely finite corrected diffusion values --
// never NaN/Inf -- exercising decomposeFaceArea's degeneracy guard end to
// end through the production diffusion() path.
TEST(DiffusionTest, StrongDistortionProducesFiniteResults) {
  const Mesh base = cfd::test::createDistortedQuad2D(8, 8, 1.0, 1.0, 0.45 * (1.0 / 8.0));
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(base);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }

  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);
  for (const auto& cell : mesh.cells()) {
    EXPECT_TRUE(std::isfinite(corrected[cell.id()])) << "cell " << cell.id();
  }
}

// Deterministic: no RNG anywhere in the correction path -- two identical
// calls (or two independently-built but geometrically identical meshes)
// must produce bit-identical results.
TEST(DiffusionTest, NonOrthogonalCorrectionIsDeterministic) {
  const Mesh baseA = cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.08);
  const Mesh baseB = cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.08);
  const Mesh meshA = cfd::test::perFaceBoundaryMesh(baseA);
  const Mesh meshB = cfd::test::perFaceBoundaryMesh(baseB);
  const auto boundariesA = cfd::test::makeExactBoundaries(meshA, cfd::test::phiSmooth);
  const auto boundariesB = cfd::test::makeExactBoundaries(meshB, cfd::test::phiSmooth);

  ScalarField fieldA(meshA.numberOfCells());
  ScalarField fieldB(meshB.numberOfCells());
  for (const auto& cell : meshA.cells()) {
    fieldA[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }
  for (const auto& cell : meshB.cells()) {
    fieldB[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }

  const auto resultA = cfd::discretization::diffusion(meshA, fieldA, 1.0, boundariesA, true,
                                                      GradientScheme::LeastSquares);
  const auto resultB = cfd::discretization::diffusion(meshB, fieldB, 1.0, boundariesB, true,
                                                      GradientScheme::LeastSquares);
  const auto resultA2 = cfd::discretization::diffusion(meshA, fieldA, 1.0, boundariesA, true,
                                                       GradientScheme::LeastSquares);

  for (Index i = 0; i < meshA.numberOfCells(); ++i) {
    EXPECT_EQ(resultA[i], resultB[i]);
    EXPECT_EQ(resultA[i], resultA2[i]);
  }
}

namespace {

// Same geometry as `base`, but every INTERNAL face's owner/neighbor
// roles are swapped and its area vector negated (so it still points
// owner -> neighbor under the new labelling). Boundary faces, cells, and
// patches are untouched. The physical problem is identical; only the
// face-orientation bookkeeping differs.
cfd::mesh::Mesh withInternalFacesReversed(const cfd::mesh::Mesh& base) {
  std::vector<cfd::mesh::Cell> cells = base.cells();
  std::vector<cfd::mesh::Face> faces;
  faces.reserve(base.numberOfFaces());
  for (const auto& face : base.faces()) {
    if (face.isBoundary()) {
      faces.push_back(face);
    } else {
      faces.emplace_back(face.id(), *face.neighbor(), face.owner(), face.centroid(),
                         face.areaVector() * -1.0);
    }
  }
  std::vector<cfd::mesh::BoundaryPatch> patches = base.boundaryPatches();
  return cfd::mesh::Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace

// InternalFaceConservation (face level, F_owner = -F_neighbor): diffusion()
// evaluates each internal face's corrected flux ONCE, owner-oriented, and
// adds it to the owner / subtracts it from the neighbor. That is only a
// conservative discretization if the flux formula itself is antisymmetric
// under swapping which cell is called the owner -- in particular the
// non-orthogonal term S_nonorth . grad_f must flip sign exactly when Sf
// does. Swapping every internal face's owner/neighbor (and negating Sf)
// describes the SAME physical problem, so every cell's net diffusion must
// be unchanged; any orientation-dependent term (e.g. a sign error in the
// correction) would show up here directly as a per-cell difference.
TEST(DiffusionTest, CorrectedFluxIsAntisymmetricUnderFaceReversal) {
  const Mesh mesh =
      cfd::test::perFaceBoundaryMesh(cfd::test::createDistortedQuad2D(8, 8, 1.0, 1.0, 0.3 / 8.0));
  const Mesh reversed = withInternalFacesReversed(mesh);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
  const auto boundariesReversed = cfd::test::makeExactBoundaries(reversed, cfd::test::phiSmooth);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }

  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    const auto original =
        cfd::discretization::diffusion(mesh, field, 1.3, boundaries, true, scheme);
    const auto flipped =
        cfd::discretization::diffusion(reversed, field, 1.3, boundariesReversed, true, scheme);
    for (const auto& cell : mesh.cells()) {
      EXPECT_NEAR(original[cell.id()], flipped[cell.id()],
                  1e-10 * (1.0 + std::abs(original[cell.id()])))
          << "cell " << cell.id();
    }
  }
}

// Global balance on a CLOSED problem (zero-gradient/adiabatic on every
// boundary patch, non-uniform field, distorted mesh). Internal fluxes
// cancel exactly (face-once), so sum(diff*V) equals the net flux through
// the boundary alone. MEASURED (and disclosed, not hidden): that net
// boundary flux is NOT exactly zero even though the prescribed flux is --
// the pre-existing, unchanged boundary branch of Diffusion.cpp
// reconstructs dphi/dn at a boundary face from a cubic fit through the
// boundary value phiB (= phiP for a zero-gradient condition) and interior
// values, which does not reproduce a Neumann zero exactly on a non-
// uniform field (true on Cartesian meshes as well, printed below for
// comparison). It is out of P12-NUM-003's scope to change. The gate THIS
// task owns is that the non-orthogonal correction adds no global source/
// sink: the global sum with the correction must equal the uncorrected
// one to round-off.
TEST(DiffusionTest, ClosedDomainGlobalBalanceIsUnchangedByCorrection) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.4 / 10.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid()) + (0.3 * cell.centroid().x);
  }

  const auto globalSum = [](const Mesh& m, const ScalarField& diff) {
    Real sum = 0.0;
    for (const auto& cell : m.cells()) {
      sum += diff[cell.id()] * cell.volume();
    }
    return sum;
  };

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 2.0, boundaries, false);
  Real sumAbs = 0.0;
  for (const auto& cell : mesh.cells()) {
    sumAbs += std::abs(uncorrected[cell.id()]) * cell.volume();
  }
  EXPECT_GT(sumAbs, 1e-2) << "field should produce nontrivial local fluxes";
  const Real sumUncorrected = globalSum(mesh, uncorrected);

  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    const auto corrected =
        cfd::discretization::diffusion(mesh, field, 2.0, boundaries, true, scheme);
    const Real sumCorrected = globalSum(mesh, corrected);
    EXPECT_NEAR(sumCorrected, sumUncorrected, 1e-12)
        << cfd::discretization::gradientSchemeName(scheme);
    std::printf(
        "\nClosed-domain global balance (distorted 10x10, 0.4h, %s): sum(diff*V) "
        "uncorrected %.17g, corrected %.17g, sum|diff*V| %.6g\n",
        cfd::discretization::gradientSchemeName(scheme).data(), sumUncorrected, sumCorrected,
        sumAbs);
  }

  // Same field and boundary conditions on the Cartesian mesh, uncorrected:
  // evidence that the nonzero net boundary flux is a property of the
  // pre-existing boundary treatment, not of mesh distortion or P12-NUM-003.
  const Mesh cartesian = cfd::mesh::MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet cartesianBoundaries;
  for (const auto& patch : cartesian.boundaryPatches()) {
    cartesianBoundaries.set(cartesian, patch.name(), std::make_unique<cfd::boundary::Adiabatic>());
  }
  ScalarField cartesianField(cartesian.numberOfCells());
  for (const auto& cell : cartesian.cells()) {
    cartesianField[cell.id()] = cfd::test::phiSmooth(cell.centroid()) + (0.3 * cell.centroid().x);
  }
  const Real cartesianSum = globalSum(
      cartesian,
      cfd::discretization::diffusion(cartesian, cartesianField, 2.0, cartesianBoundaries));
  std::printf(
      "Closed-domain global balance (Cartesian 10x10, uncorrected, pre-existing "
      "operator): sum(diff*V) %.17g\n",
      cartesianSum);
}

// LinearFieldCartesian / LinearFieldDistorted with phi = 2x + 3y + 5 (the
// task's own suggested field): analytical Laplacian is exactly zero.
// Cartesian: zero everywhere with or without the correction. Distorted
// (INTERIOR cells -- boundary-adjacent cells are dominated by the
// unchanged boundary branch): the uncorrected operator's error is O(1)
// (measured ~2.9 here). With the correction, the result depends on the
// gradient scheme feeding it, because the correction is only as exact as
// grad(phi)_f: LeastSquares is exact for a linear field on any mesh
// (P12-NUM-002), so the corrected operator is exact to round-off; PLAIN
// Green-Gauss is not (measured 0.018 here before P12-NUM-003's skewness
// correction), but the production, skewness-corrected Green-Gauss brings
// it to ~1e-11. Both measured values are printed and gated.
TEST(DiffusionTest, LinearField2x3y5IsConsistentOnCartesianAndDistortedMeshes) {
  const auto phi = [](const cfd::Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };

  {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      field[cell.id()] = phi(cell.centroid());
    }
    for (const bool correct : {false, true}) {
      const auto diff = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, correct);
      for (const auto& cell : mesh.cells()) {
        EXPECT_NEAR(diff[cell.id()], 0.0, 1e-10) << "cartesian, correct=" << correct;
      }
    }
  }

  {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(12, 12, 1.0, 1.0, 0.3 / 12.0));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      field[cell.id()] = phi(cell.centroid());
    }
    const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
    for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
      const auto corrected =
          cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true, scheme);
      Real maxInteriorCorrected = 0.0;
      Real maxInteriorUncorrected = 0.0;
      for (const auto& cell : mesh.cells()) {
        if (!isInteriorCell(mesh, cell)) {
          continue;
        }
        maxInteriorCorrected = std::max(maxInteriorCorrected, std::abs(corrected[cell.id()]));
        maxInteriorUncorrected = std::max(maxInteriorUncorrected, std::abs(uncorrected[cell.id()]));
      }
      std::printf(
          "\nphi=2x+3y+5, distorted 12x12 (0.3h), %s gradient: max interior |lap|: "
          "uncorrected %.6g, corrected %.3g\n",
          cfd::discretization::gradientSchemeName(scheme).data(), maxInteriorUncorrected,
          maxInteriorCorrected);
      EXPECT_GT(maxInteriorUncorrected, 1.0);
      // Both schemes now make the corrected operator (near-)exact:
      // LeastSquares exactly; GreenGauss because its face values are
      // skewness-corrected (P12-NUM-003 continuation; measured 9.3e-12 --
      // it was 0.018 with plain Green-Gauss).
      EXPECT_LT(maxInteriorCorrected, 1e-9);
    }
  }
}

// Boundary treatment, wall-thermal conditions: FixedTemperature (Dirichlet
// wall) and HeatFlux (prescribed wall flux) alongside Adiabatic -- on a
// single-cell mesh (no internal faces at all, so nothing for the
// correction to touch) the corrected operator must be bit-identical to
// the uncorrected one; the prescribed boundary fluxes are never altered.
TEST(DiffusionTest, CorrectionLeavesWallThermalBoundaryFluxesUntouched) {
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(350.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::HeatFlux>(1200.0, 0.6));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedTemperature>(300.0));
  const ScalarField field(mesh.numberOfCells(), 320.0);

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 0.6, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 0.6, boundaries, true,
                                                        GradientScheme::LeastSquares);
  EXPECT_EQ(uncorrected[0], corrected[0]);
}

// Dirichlet boundary faces on a DISTORTED mesh ARE corrected (see
// Diffusion.cpp's isDirichletType / MeshGeometry::
// decomposeBoundaryFaceArea): the net flux through the boundary --
// sum(diff*V), since internal fluxes cancel exactly -- therefore changes,
// and must move TOWARD the exact value. Exact: div theorem, net flux =
// integral of lap(phi) = -2 pi^2 * integral of sin(pi x)cos(pi y) over
// the unit square = 0 (the y-integral of cos(pi y) over [0,1] vanishes).
TEST(DiffusionTest, NetDirichletBoundaryFluxIsMoreAccurateWithCorrectionOnDistortedMesh) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.3 / 10.0));
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }
  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);
  Real netUncorrected = 0.0;
  Real netCorrected = 0.0;
  Real maxCellDifference = 0.0;
  for (const auto& cell : mesh.cells()) {
    netUncorrected += uncorrected[cell.id()] * cell.volume();
    netCorrected += corrected[cell.id()] * cell.volume();
    maxCellDifference =
        std::max(maxCellDifference, std::abs(uncorrected[cell.id()] - corrected[cell.id()]));
  }
  std::printf(
      "\nNet Dirichlet boundary flux, distorted 10x10 (0.3h), exact 0: uncorrected %.6g, "
      "corrected %.6g\n",
      netUncorrected, netCorrected);
  EXPECT_GT(maxCellDifference, 1e-3) << "the correction should actually change interior values";
  EXPECT_LT(std::abs(netCorrected), 0.1 * std::abs(netUncorrected));
}

// Neumann-type boundary faces (FixedGradient with a NONZERO prescribed
// gradient, HeatFlux, Adiabatic) are never corrected: on a distorted mesh
// whose every boundary face is Neumann-type, the net boundary flux --
// sum(diff*V) -- must be bit-for-bit-equivalent (to round-off) with and
// without the correction, for either gradient scheme, even though every
// internal face (and so every cell value) changes.
TEST(DiffusionTest, NeumannBoundaryFluxesAreNeverCorrected) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.4 / 10.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.7));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::HeatFlux>(250.0, 0.5));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(-1.2));

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid()) + (0.3 * cell.centroid().y);
  }
  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 0.5, boundaries, false);
  Real netUncorrected = 0.0;
  for (const auto& cell : mesh.cells()) {
    netUncorrected += uncorrected[cell.id()] * cell.volume();
  }
  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    const auto corrected =
        cfd::discretization::diffusion(mesh, field, 0.5, boundaries, true, scheme);
    Real netCorrected = 0.0;
    Real maxCellDifference = 0.0;
    for (const auto& cell : mesh.cells()) {
      netCorrected += corrected[cell.id()] * cell.volume();
      maxCellDifference =
          std::max(maxCellDifference, std::abs(corrected[cell.id()] - uncorrected[cell.id()]));
    }
    EXPECT_GT(maxCellDifference, 1e-3);
    EXPECT_NEAR(netCorrected, netUncorrected, 1e-11 * (1.0 + std::abs(netUncorrected)));
  }
}

// Boundary treatment: a single-cell mesh has FOUR boundary faces and ZERO
// internal faces, so there is nothing for the non-orthogonal correction
// to touch at all -- confirming, for a mix of FixedValue/FixedGradient/
// Adiabatic (careful, non-blind coverage of the boundary-condition
// types this task's own spec calls out, including the wall-thermal-style
// zero-gradient case), that turning the correction on can never corrupt
// a prescribed boundary flux (Neumann-type faces are never corrected, and
// the Dirichlet-type correction is exactly zero on an orthogonal boundary
// face -- see Diffusion.hpp).
TEST(DiffusionTest, CorrectionNeverAlterableOnPureBoundaryMeshWithMixedConditions) {
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);

  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(1.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.5));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(2.0));

  const ScalarField field(mesh.numberOfCells(), 0.75);

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::LeastSquares);

  ASSERT_EQ(uncorrected.size(), corrected.size());
  for (Index i = 0; i < uncorrected.size(); ++i) {
    EXPECT_EQ(uncorrected[i], corrected[i]);
  }
}

// Degenerate geometry (extreme non-orthogonality): an internal face
// whose area vector is exactly PERPENDICULAR to its owner-neighbor line
// (d . Sf == 0, the "at or past 90 degrees" case MeshGeometry::
// decomposeFaceArea's own header comment documents as its `valid=false`
// trigger) must still produce a finite result -- the per-face fallback to
// the plain (still well-defined here: the owner-neighbor DISTANCE itself
// is nonzero and finite, only the decomposition's own denominator is
// degenerate) uncorrected formula, never NaN/Inf.
TEST(DiffusionTest, ExtremeNonOrthogonalityFallsBackSafely) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.0, 0.0}, 1.0);
  cells.emplace_back(1, cfd::Vector2{0.0, 1.0}, 1.0);

  std::vector<cfd::mesh::Face> faces;
  // Internal face: d = neighbor - owner = (0,1), but Sf = (1,0) is
  // exactly perpendicular to d -- d.Sf == 0, the exact degeneracy
  // decomposeFaceArea guards against.
  faces.emplace_back(0, 0, 1, cfd::Vector2{0.0, 0.5}, cfd::Vector2{1.0, 0.0});
  faces.emplace_back(1, 0, std::nullopt, cfd::Vector2{-0.5, 0.0}, cfd::Vector2{-1.0, 0.0});
  faces.emplace_back(2, 1, std::nullopt, cfd::Vector2{-0.5, 1.0}, cfd::Vector2{-1.0, 0.0});
  cells[0].addFace(0);
  cells[0].addFace(1);
  cells[1].addFace(0);
  cells[1].addFace(2);

  std::vector<cfd::mesh::BoundaryPatch> patches;
  patches.emplace_back("left", std::vector<Index>{1});
  patches.emplace_back("far", std::vector<Index>{2});
  const Mesh mesh(std::move(cells), std::move(faces), std::move(patches));

  // Sanity check: this face really does exercise decomposeFaceArea's
  // degeneracy guard, not some other unrelated failure mode.
  ASSERT_FALSE(cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, mesh.face(0)).valid);

  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.0));
  boundaries.set(mesh, "far", std::make_unique<cfd::boundary::FixedValue>(1.0));

  ScalarField field(mesh.numberOfCells());
  field[0] = 0.25;
  field[1] = 0.75;

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::GreenGauss);
  for (Index i = 0; i < corrected.size(); ++i) {
    EXPECT_TRUE(std::isfinite(corrected[i])) << "cell " << i;
    // The fallback reduces to exactly the uncorrected formula for this face.
    EXPECT_EQ(uncorrected[i], corrected[i]) << "cell " << i;
  }
}

// Degenerate geometry (zero owner-neighbor distance): two coincident cell
// centroids. This is a PRE-EXISTING division-by-zero exposure in the
// plain (unchanged, out of scope for this task) uncorrected diffusion
// formula itself (`(phiN-phiP)/dPN` with dPN==0) -- not something
// P12-NUM-003 introduces, and not something the non-orthogonal-
// correction fallback can paper over, since it deliberately falls back
// to that SAME pre-existing formula. Documented here (rather than
// silently ignored) as a known, out-of-scope limitation: the correction
// path is never WORSE than the uncorrected one for this degeneracy.
TEST(DiffusionTest, ZeroOwnerNeighborDistanceIsAPreexistingLimitationNotWorsenedByCorrection) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.5, 0.5}, 1.0);
  cells.emplace_back(1, cfd::Vector2{0.5, 0.5}, 1.0);  // Coincident centroid.

  std::vector<cfd::mesh::Face> faces;
  faces.emplace_back(0, 0, 1, cfd::Vector2{0.5, 0.5}, cfd::Vector2{1.0, 0.0});
  faces.emplace_back(1, 0, std::nullopt, cfd::Vector2{0.0, 0.5}, cfd::Vector2{-1.0, 0.0});
  faces.emplace_back(2, 1, std::nullopt, cfd::Vector2{1.0, 0.5}, cfd::Vector2{1.0, 0.0});
  cells[0].addFace(0);
  cells[0].addFace(1);
  cells[1].addFace(0);
  cells[1].addFace(2);

  std::vector<cfd::mesh::BoundaryPatch> patches;
  patches.emplace_back("left", std::vector<Index>{1});
  patches.emplace_back("right", std::vector<Index>{2});
  const Mesh mesh(std::move(cells), std::move(faces), std::move(patches));

  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(1.0));

  ScalarField field(mesh.numberOfCells());
  field[0] = 0.25;
  field[1] = 0.75;

  const auto uncorrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, false);
  const auto corrected = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                        GradientScheme::GreenGauss);
  for (Index i = 0; i < corrected.size(); ++i) {
    // Both are non-finite here (pre-existing, out-of-scope division by
    // zero) -- the point of this test is that correction produces
    // EXACTLY the same (non-)result as the uncorrected baseline, i.e.
    // introduces no NEW failure mode of its own.
    EXPECT_EQ(std::isfinite(uncorrected[i]), std::isfinite(corrected[i])) << "cell " << i;
  }
}

// P12-NUM-003: which boundary conditions the shared diffusion correction
// treats as value-prescribing (corrected) vs flux-prescribing (never
// corrected) -- every BoundaryConditionType, explicitly.
TEST(DiffusionTest, BoundaryCorrectionClassificationCoversEveryConditionType) {
  using cfd::boundary::BoundaryConditionType;
  using cfd::discretization::prescribesBoundaryValue;
  for (const auto type :
       {BoundaryConditionType::FixedValue, BoundaryConditionType::FixedTemperature,
        BoundaryConditionType::WallOmega, BoundaryConditionType::Wall,
        BoundaryConditionType::MovingWall, BoundaryConditionType::Inlet}) {
    EXPECT_TRUE(prescribesBoundaryValue(type));
  }
  for (const auto type : {BoundaryConditionType::FixedGradient, BoundaryConditionType::HeatFlux,
                          BoundaryConditionType::Adiabatic, BoundaryConditionType::Outlet,
                          BoundaryConditionType::Symmetry}) {
    EXPECT_FALSE(prescribesBoundaryValue(type));
  }
}

// P12-NUM-003 (boundary accuracy diagnosis): WHERE the corrected operator's
// first-order boundary-ring truncation error comes from, measured per face
// type on distorted 0.15h meshes with exact per-face Dirichlet values and
// the least-squares correction gradient. For each boundary-adjacent (ring-0)
// cell the operator residual is split into the numeric flux through its
// internal faces and through its boundary faces, each compared with the
// exact flux grad(phi)(x_f) . Sf:
//   - ring-0 truncation error with the production operator,
//   - ring-0 truncation error if the boundary-face flux were EXACT (what
//     remains is the internal faces' share),
//   - max |boundary flux error| / V and max |internal flux error| / V,
// against the interior cells' truncation error. Measured values are printed;
// the gates pin what the evidence establishes -- interior second order,
// ring-0 at least first order (the solution itself converges at second
// order: ThermalNonOrthogonalTest.NonOrthogonalDiffusion), see
// results/p12-num-003/summary.md "Boundary accuracy".
TEST(DiffusionTest, BoundaryRingTruncationIsFirstOrderAndLocalized) {
  const Index grids[3] = {16, 32, 64};
  Real ring0[3];
  Real ring0ExactBoundary[3];
  Real interior[3];
  for (int g = 0; g < 3; ++g) {
    const Index n = grids[g];
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.15 / static_cast<Real>(n)));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
    const auto grad =
        cfd::discretization::gradient(mesh, field, boundaries, GradientScheme::LeastSquares);
    const auto diff = cfd::discretization::diffusion(mesh, field, 1.0, boundaries, true,
                                                     GradientScheme::LeastSquares);
    ring0[g] = 0.0;
    ring0ExactBoundary[g] = 0.0;
    interior[g] = 0.0;
    Real boundaryFluxError = 0.0;
    Real internalFluxError = 0.0;
    for (const auto& cell : mesh.cells()) {
      const Real exact = cfd::test::laplacianSmooth(cell.centroid());
      bool touchesBoundary = false;
      Real internalNumeric = 0.0;
      Real boundaryExact = 0.0;
      for (const Index faceId : cell.faceIds()) {
        const auto& face = mesh.face(faceId);
        const Real exactFlux = cfd::dot(cfd::test::gradSmooth(face.centroid()), face.areaVector());
        if (face.isBoundary()) {
          touchesBoundary = true;
          boundaryExact += exactFlux;
          continue;
        }
        const Real sign = (face.owner() == cell.id()) ? 1.0 : -1.0;
        const auto terms = cfd::discretization::internalFaceDiffusionTerms(
            mesh, face, 1.0, cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face), &grad);
        const Real flux = (terms.coefficient * (field[*face.neighbor()] - field[face.owner()])) +
                          terms.explicitFlux;
        internalNumeric += sign * flux;
      }
      if (!touchesBoundary) {
        interior[g] = std::max(interior[g], std::abs(diff[cell.id()] - exact));
        continue;
      }
      for (const Index faceId : cell.faceIds()) {
        const auto& face = mesh.face(faceId);
        if (face.isBoundary()) continue;
        const auto terms = cfd::discretization::internalFaceDiffusionTerms(
            mesh, face, 1.0, cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face), &grad);
        const Real flux = (terms.coefficient * (field[*face.neighbor()] - field[face.owner()])) +
                          terms.explicitFlux;
        const Real exactFlux = cfd::dot(cfd::test::gradSmooth(face.centroid()), face.areaVector());
        internalFluxError = std::max(internalFluxError, std::abs(flux - exactFlux) / cell.volume());
      }
      const Real boundaryNumeric = (diff[cell.id()] * cell.volume()) - internalNumeric;
      boundaryFluxError =
          std::max(boundaryFluxError, std::abs(boundaryNumeric - boundaryExact) / cell.volume());
      ring0[g] = std::max(ring0[g], std::abs(diff[cell.id()] - exact));
      ring0ExactBoundary[g] =
          std::max(ring0ExactBoundary[g],
                   std::abs(((internalNumeric + boundaryExact) / cell.volume()) - exact));
    }
    std::printf(
        "\nBoundary ring, distorted 0.15h n=%llu, corrected(LS): max truncation error ring-0 %.4g,"
        " ring-0 with exact boundary flux %.4g, interior %.4g | max|boundary flux err|/V %.4g,"
        " max|internal flux err|/V (ring-0 cells) %.4g",
        static_cast<unsigned long long>(n), ring0[g], ring0ExactBoundary[g], interior[g],
        boundaryFluxError, internalFluxError);
  }
  std::printf(
      "\n  orders (16->32, 32->64): ring-0 %.3f %.3f | ring-0 exact-boundary %.3f %.3f | interior"
      " %.3f %.3f\n",
      std::log2(ring0[0] / ring0[1]), std::log2(ring0[1] / ring0[2]),
      std::log2(ring0ExactBoundary[0] / ring0ExactBoundary[1]),
      std::log2(ring0ExactBoundary[1] / ring0ExactBoundary[2]),
      std::log2(interior[0] / interior[1]), std::log2(interior[1] / interior[2]));
  EXPECT_GT(std::log2(interior[1] / interior[2]), 1.8);
  EXPECT_GT(std::log2(ring0[1] / ring0[2]), 0.8);
}
