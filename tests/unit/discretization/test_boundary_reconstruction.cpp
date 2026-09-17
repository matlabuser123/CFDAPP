// P12-DIFF-002 W4 -- hand-derived verification of the second-order one-sided Dirichlet boundary
// reconstruction and of the matrix it assembles.
//
// Every expected number below is derived independently, on paper, from the mesh geometry -- not by
// re-running the implementation's own expression. The derivation (results/p12-diff-002/
// architecture.md) is, with h1, h2 the wall-NORMAL distances from the face to the owner and to the
// far cell:
//     cP = h2 / (h1 (h2 - h1)),  cF = h1 / (h2 (h2 - h1)),  cB = 1/h1 + 1/h2,  cP - cF = cB
//     A(P,P) += Gamma |S| cP ;  A(P,F) -= Gamma |S| cF ;  rhs += Gamma |S| cB phi_b
//     rhs += Gamma |S| ( cP grad_P . deltaP - cF grad_F . deltaF )
// On a uniform grid (h1 = h/2, h2 = 3h/2) that reduces to dphi/ds = [9 phi_P - phi_F - 8
// phi_b]/(3h), which is also the formula src/discretization/Diffusion.cpp's explicit operator has
// used since P0
// -- cross-checked directly below.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;
using cfd::discretization::boundaryFaceDiffusionTerms;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

// The "bottom" boundary face of a uniform Cartesian mesh, and its owner.
const Face& bottomFace(const Mesh& mesh) {
  return mesh.face(mesh.boundaryPatch("bottom").faceIds().front());
}

// The stored column indices of one CSR row, in the matrix's own ascending order.
std::vector<Index> columnsOf(const cfd::algebra::SparseMatrix& matrix, Index row) {
  const Index* offsets = matrix.rowOffsetsData();
  return std::vector<Index>(matrix.columnIndicesData() + offsets[row],
                            matrix.columnIndicesData() + offsets[row + 1]);
}

// A(row, column), or exactly 0 if the entry is not stored.
Real entryOf(const cfd::algebra::SparseMatrix& matrix, Index row, Index column) {
  const Index* offsets = matrix.rowOffsetsData();
  for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
    if (matrix.columnIndicesData()[k] == column) return matrix.valuesData()[k];
  }
  return 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The stencil geometry itself.
// ---------------------------------------------------------------------------

TEST(BoundaryInwardStencil, UniformCartesianGivesHalfAndThreeHalfCellHeights) {
  // 4x4 on the unit square: h = 0.25, so the owner centre sits h/2 = 0.125 from the wall and the
  // cell above it 3h/2 = 0.375. Both tangential offsets are exactly zero on an orthogonal face.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, bottomFace(mesh));
  ASSERT_TRUE(stencil.valid);
  EXPECT_DOUBLE_EQ(stencil.h1, 0.125);
  EXPECT_DOUBLE_EQ(stencil.h2, 0.375);
  EXPECT_EQ(stencil.deltaP, Vector3{});
  EXPECT_EQ(stencil.deltaF, Vector3{});
  // The far cell is the one directly above the owner: id = j * nx + i with j = 1, i = 0.
  EXPECT_EQ(stencil.farCell, 4u);
}

TEST(BoundaryInwardStencil, GradedMeshUsesActualCellHeights) {
  // Rows graded by 2: heights 1/15, 2/15, 4/15, 8/15 from the bottom. The owner centre is at
  // (1/15)/2 = 1/30 and the next cell centre at 1/15 + (2/15)/2 = 2/15.
  const Mesh mesh = MeshGeometry::createGraded2D(
      2, 4, 1.0, 1.0, cfd::mesh::AxisGrading{cfd::mesh::GradingType::Uniform, 1.0},
      cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 2.0,
                             cfd::mesh::GradingCluster::Start});
  const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, bottomFace(mesh));
  ASSERT_TRUE(stencil.valid);
  EXPECT_NEAR(stencil.h1, 1.0 / 30.0, 1e-15);
  EXPECT_NEAR(stencil.h2, 2.0 / 15.0, 1e-15);
  EXPECT_GT(stencil.h2, stencil.h1);
}

TEST(BoundaryInwardStencil, OneCellThickMeshHasNoStencilAcrossTheThinDirection) {
  // 8x1: the bottom/top faces have no interior face across the cell, so no stencil. The left/right
  // faces do (8 cells along x), so they are valid -- the fallback is per face, not per mesh, and a
  // valid one-cell-thick mesh is never rejected.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0);
  EXPECT_FALSE(MeshGeometry::boundaryInwardStencil(mesh, bottomFace(mesh)).valid);
  const Face& left = mesh.face(mesh.boundaryPatch("left").faceIds().front());
  EXPECT_TRUE(MeshGeometry::boundaryInwardStencil(mesh, left).valid);
}

TEST(BoundaryInwardStencil, SingleCellMeshHasNoStencilAnywhere) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    EXPECT_FALSE(MeshGeometry::boundaryInwardStencil(mesh, face).valid);
  }
}

TEST(BoundaryInwardStencil, RejectsInternalFace) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    EXPECT_THROW(static_cast<void>(MeshGeometry::boundaryInwardStencil(mesh, face)),
                 cfd::InvalidArgumentError);
    return;
  }
}

// ---------------------------------------------------------------------------
// The coefficients, against the hand-derived values.
// ---------------------------------------------------------------------------

TEST(BoundaryReconstruction, CoefficientsMatchHandDerivedUniformValues) {
  // 4x4 unit square, Gamma = 2, face area |S| = 0.25 (unit depth). h1 = 0.125, h2 = 0.375.
  //   cP = h2/(h1 (h2-h1)) = 0.375/(0.125*0.25)  = 12
  //   cF = h1/(h2 (h2-h1)) = 0.125/(0.375*0.25)  = 4/3
  //   cB = 1/h1 + 1/h2     = 8 + 8/3             = 32/3
  //   check cP - cF = 12 - 4/3 = 32/3 = cB
  // Gamma |S| = 2 * 0.25 = 0.5, so
  //   coefficient = 6,  farCellCoefficient = 2/3,  boundaryValueCoefficient = 16/3.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Face& face = bottomFace(mesh);
  const VectorField zeroGradient(mesh.numberOfCells(), Vector3{});
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());

  const auto terms = boundaryFaceDiffusionTerms(mesh, face, 2.0, distance, &zeroGradient, true);
  ASSERT_TRUE(terms.higherOrder);
  EXPECT_DOUBLE_EQ(terms.coefficient, 6.0);
  EXPECT_DOUBLE_EQ(terms.farCellCoefficient, 2.0 / 3.0);
  EXPECT_DOUBLE_EQ(terms.boundaryValueCoefficient, 16.0 / 3.0);
  EXPECT_EQ(terms.farCell, 4u);
  // Zero gradient -> the tangential transfer contributes exactly nothing.
  EXPECT_DOUBLE_EQ(terms.explicitFlux, 0.0);
  // The consistency identity, in assembled form: a constant field must give zero flux.
  EXPECT_DOUBLE_EQ(terms.coefficient - terms.farCellCoefficient, terms.boundaryValueCoefficient);
}

TEST(BoundaryReconstruction, ReproducesTheUniformThreePointFormula) {
  // The uniform-grid limit dphi/ds = [9 phi_P - phi_F - 8 phi_b] / (3h), h = 0.25, and the flux
  // into the owner is Gamma |S| dphi/dn = -Gamma |S| dphi/ds. Assembled row holds -flux, so
  //     -flux = coefficient phi_P - farCellCoefficient phi_F - boundaryValueCoefficient phi_b.
  // With Gamma = 1 and |S| = 0.25: Gamma|S|/(3h) = 0.25/0.75 = 1/3, so the expected row is
  //     (1/3) (9 phi_P - phi_F - 8 phi_b) = 3 phi_P - (1/3) phi_F - (8/3) phi_b.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Face& face = bottomFace(mesh);
  const VectorField zeroGradient(mesh.numberOfCells(), Vector3{});
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const auto terms = boundaryFaceDiffusionTerms(mesh, face, 1.0, distance, &zeroGradient, true);
  EXPECT_DOUBLE_EQ(terms.coefficient, 3.0);
  EXPECT_DOUBLE_EQ(terms.farCellCoefficient, 1.0 / 3.0);
  EXPECT_DOUBLE_EQ(terms.boundaryValueCoefficient, 8.0 / 3.0);
}

TEST(BoundaryReconstruction, IsExactForALinearFieldAndForAQuadraticOne) {
  // Assemble the row by hand from the returned terms and compare against the analytic flux
  // Gamma grad(phi) . S_out. Exact for both, on a uniform mesh, to round-off.
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  const Real gamma = 0.7;
  for (const int power : {1, 2}) {
    const auto value = [power](Real y) { return power == 1 ? (0.3 + (2.0 * y)) : (y * y); };
    const auto derivative = [power](Real y) { return power == 1 ? 2.0 : (2.0 * y); };
    VectorField gradient(mesh.numberOfCells(), Vector3{});
    for (const auto& cell : mesh.cells()) {
      gradient[cell.id()] = Vector3{0.0, derivative(cell.centroid().y), 0.0};
    }
    for (const Index faceId : mesh.boundaryPatch("bottom").faceIds()) {
      const Face& face = mesh.face(faceId);
      const auto& owner = mesh.cell(face.owner());
      const Real distance = MeshGeometry::distance(owner.centroid(), face.centroid());
      const auto terms = boundaryFaceDiffusionTerms(mesh, face, gamma, distance, &gradient, true);
      ASSERT_TRUE(terms.higherOrder);
      const Real flux =
          -((terms.coefficient * value(owner.centroid().y)) -
            (terms.farCellCoefficient * value(mesh.cell(terms.farCell).centroid().y))) +
          (terms.boundaryValueCoefficient * value(face.centroid().y)) + terms.explicitFlux;
      const Real exact =
          gamma * dot(Vector3{0.0, derivative(face.centroid().y), 0.0}, face.areaVector());
      EXPECT_NEAR(flux, exact, 1e-13) << "power " << power << " face " << faceId;
    }
  }
}

TEST(BoundaryReconstruction, FallsBackToTheTwoPointFormWithoutAStencil) {
  // A one-cell-thick mesh's wall faces: the returned terms must be exactly the pre-DIFF-002
  // two-point ones -- Gamma |S_orth| / |d| on the diagonal, the same value multiplying the
  // boundary value, no far-cell coupling.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0);
  const Face& face = bottomFace(mesh);
  const auto& owner = mesh.cell(face.owner());
  const Real distance = MeshGeometry::distance(owner.centroid(), face.centroid());
  const VectorField gradient(mesh.numberOfCells(), Vector3{1.0, 2.0, 0.0});
  const auto terms = boundaryFaceDiffusionTerms(mesh, face, 3.0, distance, &gradient, true);
  EXPECT_FALSE(terms.higherOrder);
  EXPECT_DOUBLE_EQ(terms.farCellCoefficient, 0.0);
  const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
  ASSERT_TRUE(decomposition.valid);
  EXPECT_DOUBLE_EQ(terms.coefficient, 3.0 * magnitude(decomposition.orthogonal) / distance);
  EXPECT_DOUBLE_EQ(terms.boundaryValueCoefficient, terms.coefficient);
}

TEST(BoundaryReconstruction, NeumannAndUncorrectedCallsAreUnchanged) {
  // prescribedValue = false (a flux-prescribing face) and gradPhi = nullptr must both give exactly
  // the pre-existing two-point coefficient with no far-cell coupling.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Face& face = bottomFace(mesh);
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const VectorField gradient(mesh.numberOfCells(), Vector3{1.0, 1.0, 0.0});
  const Real expected = 5.0 * face.area() / distance;
  for (const auto& terms : {boundaryFaceDiffusionTerms(mesh, face, 5.0, distance, &gradient, false),
                            boundaryFaceDiffusionTerms(mesh, face, 5.0, distance, nullptr, true)}) {
    EXPECT_FALSE(terms.higherOrder);
    EXPECT_DOUBLE_EQ(terms.coefficient, expected);
    EXPECT_DOUBLE_EQ(terms.boundaryValueCoefficient, expected);
    EXPECT_DOUBLE_EQ(terms.farCellCoefficient, 0.0);
    EXPECT_DOUBLE_EQ(terms.explicitFlux, 0.0);
  }
}

TEST(BoundaryReconstruction, TangentialTransferIsZeroOnAnOrthogonalFaceAndNonZeroOffIt) {
  // Orthogonal: both offsets are exactly {0,0}, so any gradient leaves explicitFlux exactly 0.
  const Mesh orthogonal = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const VectorField gradient(orthogonal.numberOfCells(), Vector3{3.0, -1.0, 0.0});
  const Face& face = bottomFace(orthogonal);
  const Real distance =
      MeshGeometry::distance(orthogonal.cell(face.owner()).centroid(), face.centroid());
  EXPECT_DOUBLE_EQ(
      boundaryFaceDiffusionTerms(orthogonal, face, 1.0, distance, &gradient, true).explicitFlux,
      0.0);

  // Sheared: the interior rows are displaced tangentially, so the owner centroid leaves the
  // wall-normal ray and the transfer term must be non-zero.
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= 4; ++j) {
    for (Index i = 0; i <= 4; ++i) {
      const Real x = (static_cast<Real>(i) / 4.0) + ((j == 0 || j == 4) ? 0.0 : 0.05);
      vertices.push_back(Vector2{x, static_cast<Real>(j) / 4.0});
    }
  }
  const Mesh sheared = MeshGeometry::createStructuredQuad2D(4, 4, vertices);
  const Face& shearedFace = bottomFace(sheared);
  const VectorField shearedGradient(sheared.numberOfCells(), Vector3{3.0, -1.0, 0.0});
  const Real shearedDistance =
      MeshGeometry::distance(sheared.cell(shearedFace.owner()).centroid(), shearedFace.centroid());
  const auto terms = boundaryFaceDiffusionTerms(sheared, shearedFace, 1.0, shearedDistance,
                                                &shearedGradient, true);
  ASSERT_TRUE(terms.higherOrder);
  EXPECT_NE(terms.explicitFlux, 0.0);
}

// ---------------------------------------------------------------------------
// The assembled system, against a small one derived independently on paper.
// ---------------------------------------------------------------------------

TEST(BoundaryReconstruction, AssembledThermalSystemMatchesTheHandDerivedOne) {
  // 3x3 cells on a 3x3 square: h = 1 in both directions, every face area = 1 (unit depth),
  // k = 1, so Gamma |S| = 1 and every internal-face coefficient k |S| / dPN = 1.
  // h1 = 0.5, h2 = 1.5, so cP = 1.5/(0.5*1) = 3, cF = 0.5/(1.5*1) = 1/3, cB = 2 + 2/3 = 8/3.
  // Cell ids are j * nx + i, so cell 0 = (0,0), 1 = (1,0), 2 = (2,0), 3 = (0,1), 4 = (1,1).
  // bottom and left prescribe T (3 and 1.5); right and top are adiabatic (zero prescribed flux,
  // never reconstructed) and touch none of the three rows checked below.
  //
  // Row 1, cell (1,0) -- one Dirichlet face (bottom), three internal faces (to 0, 2 and 4):
  //   A(1,1) = 3 * 1 (internal) + cP = 3 + 3 = 6
  //   A(1,0) = A(1,2) = -1
  //   A(1,4) = -1 (internal) - cF = -1 - 1/3 = -4/3   <- the far cell IS the top neighbour
  //   rhs(1) = cB * 3 = 8
  // Row 0, cell (0,0) -- two Dirichlet faces (bottom, far cell 3; left, far cell 1) and two
  // internal faces (to 1 and 3):
  //   A(0,0) = 2 * 1 + 2 * cP = 2 + 6 = 8
  //   A(0,1) = -1 - cF = -4/3,  A(0,3) = -1 - cF = -4/3
  //   rhs(0) = cB * 3 + cB * 1.5 = 8 + 4 = 12
  // Row 4, cell (1,1) -- interior, four internal faces: A(4,4) = 4, A(4,1) = -1.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedTemperature>(3.0));
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedTemperature>(1.5));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::Adiabatic>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::Adiabatic>());

  // A deliberately non-uniform temperature: the reconstruction's coefficients must not depend on
  // it, and on this orthogonal mesh its gradient must leave the RHS transfer term exactly zero.
  cfd::fields::ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = 2.0 + cell.centroid().x - (0.4 * cell.centroid().y);
  }

  const auto assemble = [&](bool corrected) {
    cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
    cfd::algebra::Vector rhs(mesh.numberOfCells());
    cfd::thermal::assembleThermalDiffusionContribution(
        mesh, 1.0, temperature, boundaries, builder, rhs,
        cfd::discretization::NonOrthogonalCorrectionOptions{corrected});
    return std::pair{builder.build(), rhs};
  };
  const auto [matrix, rhs] = assemble(true);

  EXPECT_EQ(columnsOf(matrix, 1), (std::vector<Index>{0, 1, 2, 4}));
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 1), 6.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 0), -1.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 2), -1.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 4), -4.0 / 3.0);
  EXPECT_DOUBLE_EQ(rhs[1], 8.0);

  EXPECT_EQ(columnsOf(matrix, 0), (std::vector<Index>{0, 1, 3}));
  EXPECT_DOUBLE_EQ(entryOf(matrix, 0, 0), 8.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 0, 1), -4.0 / 3.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 0, 3), -4.0 / 3.0);
  EXPECT_DOUBLE_EQ(rhs[0], 12.0);

  EXPECT_DOUBLE_EQ(entryOf(matrix, 4, 4), 4.0);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 4, 1), -1.0);
  // The far-cell coupling is one-sided by construction: it enters the boundary cell's row only.
  // That asymmetry is why every equation using this treatment is solved with BiCGSTAB, never CG
  // (results/p12-diff-002/architecture.md section 2).
  EXPECT_NE(entryOf(matrix, 1, 4), entryOf(matrix, 4, 1));

  // The far cell is always reached through a face OF THE OWNER, so its entry lands on a column the
  // owner's row already had: the sparsity pattern is byte-for-byte the pre-DIFF-002 one, which is
  // why no new matrix mechanism was needed (architecture.md section 1).
  const auto [baseline, baselineRhs] = assemble(false);
  EXPECT_EQ(matrix.nonZeros(), baseline.nonZeros());
  for (Index row = 0; row < mesh.numberOfCells(); ++row) {
    EXPECT_EQ(columnsOf(matrix, row), columnsOf(baseline, row)) << "row " << row;
  }
  // P12-DIFF-002 A5, entry 1 (validation-migration/acceptance_gate_A5.md). This control used to
  // read the two-point system out of `assemble(false)` and assert 5.0 / -1.0 / 6.0. P12-DIFF-002 A2
  // deliberately made the Dirichlet wall reconstruction UNCONDITIONAL -- which wall-flux scheme is
  // used must not depend on an iterative control (a2/activation_architecture.md) -- so
  // `NonOrthogonalCorrectionOptions{false}` no longer selects a two-point wall and that expectation
  // is obsolete. The two systems are now identical here, which is itself worth pinning:
  EXPECT_DOUBLE_EQ(entryOf(baseline, 1, 1), entryOf(matrix, 1, 1));
  EXPECT_DOUBLE_EQ(entryOf(baseline, 1, 4), entryOf(matrix, 1, 4));
  EXPECT_DOUBLE_EQ(baselineRhs[1], rhs[1]);

  // The POWER the old control provided is kept, and stated directly instead of via the toggle: the
  // assembled row must not be the historical two-point system, and must differ from it by exactly
  // the reconstruction's own coefficient changes. Hand-derived for this block (h = 1, |S| = 1,
  // k = 1, h1 = 0.5, h2 = 1.5):
  //   historical   cP_hist = cB_hist = |S|/h1 = 2,      cF_hist = 0
  //   DIFF-002     cP = 3,  cF = 1/3,  cB = 8/3
  const Real cHist = 2.0;
  const Real cP = 3.0;
  const Real cF = 1.0 / 3.0;
  const Real cB = 8.0 / 3.0;
  ASSERT_DOUBLE_EQ(cP - cF, cB);
  const Real internal = 1.0;
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 1) - (3.0 * internal + cHist), cP - cHist);
  EXPECT_DOUBLE_EQ(entryOf(matrix, 1, 4) - (-internal), -cF);
  EXPECT_DOUBLE_EQ(rhs[1] - cHist * 3.0, (cB - cHist) * 3.0);
  // ... so an implementation still on the two-point wall flux cannot satisfy the assertions above.
  EXPECT_NE(entryOf(matrix, 1, 1), 3.0 * internal + cHist);
  EXPECT_NE(entryOf(matrix, 1, 4), -internal);
  EXPECT_NE(rhs[1], cHist * 3.0);
}

TEST(BoundaryReconstruction, ReproducesTheDiffusionOperatorsThreePointFormula) {
  // architecture.md section 6a: src/discretization/Diffusion.cpp's explicit operator has fitted the
  // same quadratic since P0. Transcribed from src/discretization/Diffusion.cpp lines 167-169 with
  // its own two spacings -- h1 = |x_P - x_f| and h2 = ownerNeighborDistance(oppositeFace), i.e.
  // Diffusion.cpp's h2 is the P-to-F distance, one h1 shorter than this reconstruction's h2:
  //     a = (2 h1 + h2) / (h1 (h1 + h2)),  b = -(h1 + h2) / (h1 h2),  c = h1 / (h2 (h1 + h2))
  //     dphi/dn = a phi_b + b phi_P + c phi_F
  // Substituting h2_Diffusion = h2 - h1 gives a = cB, b = -cP, c = cF exactly. Checked on a UNIFORM
  // mesh and on a GEOMETRICALLY GRADED one (h1 = 1/30, h2 = 2/15, so the two spacings differ by a
  // factor of 3): the agreement is with the general geometric formula, not with a uniform-grid
  // special case.
  const Mesh uniform = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh graded = MeshGeometry::createGraded2D(
      2, 4, 1.0, 1.0, cfd::mesh::AxisGrading{cfd::mesh::GradingType::Uniform, 1.0},
      cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 2.0,
                             cfd::mesh::GradingCluster::Start});
  for (const Mesh* mesh : {&uniform, &graded}) {
    const Face& face = bottomFace(*mesh);
    const auto& owner = mesh->cell(face.owner());
    const Real gamma = 1.7;
    const Real dPB = MeshGeometry::distance(owner.centroid(), face.centroid());
    const VectorField zeroGradient(mesh->numberOfCells(), Vector3{});
    const auto terms = boundaryFaceDiffusionTerms(*mesh, face, gamma, dPB, &zeroGradient, true);
    ASSERT_TRUE(terms.higherOrder);

    const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(*mesh, owner, face);
    ASSERT_TRUE(oppositeFaceId.has_value());
    const Real h1 = dPB;
    const Real h2 = MeshGeometry::ownerNeighborDistance(*mesh, mesh->face(*oppositeFaceId));
    const Real a = (2.0 * h1 + h2) / (h1 * (h1 + h2));
    const Real b = -(h1 + h2) / (h1 * h2);
    const Real c = h1 / (h2 * (h1 + h2));

    const Real gammaArea = gamma * face.area();
    EXPECT_NEAR(terms.boundaryValueCoefficient, gammaArea * a, 1e-14 * gammaArea * a);
    EXPECT_NEAR(terms.coefficient, gammaArea * -b, 1e-14 * gammaArea * -b);
    EXPECT_NEAR(terms.farCellCoefficient, gammaArea * c, 1e-14 * gammaArea * c);
  }
}
