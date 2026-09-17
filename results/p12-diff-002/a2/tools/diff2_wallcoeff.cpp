// P12-DIFF-002 A2 -- DECISIVE isolation of the Dirichlet wall coefficient from the assembled
// matrix, valid on a distorted mesh, with no contamination and nothing re-implemented.
//
// Why this is needed. The rhs-differencing observable in diff2_activation.cpp is contaminated on a
// non-orthogonal mesh: the explicit non-orthogonal INTERNAL-face flux depends on grad(phi), which
// depends on the boundary values, and that internal term legitimately exists only when the
// correction is enabled. So a raw rhs difference across N mixes the wall scheme with a term that is
// supposed to change. diff2_activation_rows.cpp showed the difference reaches interior-only rows,
// but boundary rows carry internal faces too, so that alone does not prove the wall coefficient is
// invariant.
//
// The clean observable. For row P the assembled diagonal is exactly
//     A(P,P) = sum over P's INTERNAL faces of internalFaceDiffusionTerms(...).coefficient
//            + (for a value-prescribing boundary face of P) the boundary terms.coefficient
// and the internal-face *coefficient* is Gamma |S_orth| / dPN when corrected, Gamma |Sf| / dPN when
// not -- a purely GEOMETRIC quantity with no gradient in it at all. So it can be computed exactly
// here, from the same public production function the assembler itself calls, and subtracted:
//     wallCoefficient(P, N) = A(P,P) - sum_internal internalFaceDiffusionTerms(..., gradFor(N))
// This leaves the Dirichlet wall coefficient alone, on any geometry. Requiring it to be bitwise
// identical for N = 0, 1, 2 is exactly A2-1's "same reconstruction coefficients / same boundary
// coefficient", with the contamination removed rather than argued away.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using namespace cfd;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kConductivity = 0.6;
constexpr Real kWallValue = 2.0;

discretization::NonOrthogonalCorrectionOptions optionsFor(Index n) {
  pressure_velocity::SIMPLESettings settings;
  settings.nonOrthogonalCorrections = n;
  return pressure_velocity::nonOrthogonalOptions(settings);
}

boundary::BoundaryConditionSet allDirichlet(const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(kWallValue));
  }
  return bcs;
}

Real storedEntry(const algebra::SparseMatrix& m, Index row, Index column) {
  const Index* offsets = m.rowOffsetsData();
  for (Index k = offsets[row]; k < offsets[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == column) return m.valuesData()[k];
  }
  return 0.0;
}

// The Dirichlet wall coefficient of every boundary-adjacent row, isolated as described above.
std::vector<Real> wallCoefficients(const Mesh& mesh, Index n) {
  const auto bcs = allDirichlet(mesh);
  const fields::ScalarField temperature(mesh.numberOfCells(), 300.0);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  thermal::assembleThermalDiffusionContribution(mesh, kConductivity, temperature, bcs, builder, rhs,
                                                optionsFor(n));
  const algebra::SparseMatrix matrix = builder.build();

  // The internal-face coefficient the assembler used, recomputed from the same public function with
  // the same pointer convention: corrected (non-null) only when the correction is enabled.
  const auto options = optionsFor(n);
  const fields::VectorField grad =
      discretization::gradient(mesh, temperature, bcs, options.gradientScheme);
  const fields::VectorField* internalGrad = options.enabled ? &grad : nullptr;

  std::vector<Real> internalSum(mesh.numberOfCells(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real c =
        discretization::internalFaceDiffusionTerms(mesh, face, kConductivity, dPN, internalGrad)
            .coefficient;
    internalSum[face.owner()] += c;
    internalSum[*face.neighbor()] += c;
  }

  std::vector<Real> out(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    out[cell.id()] = storedEntry(matrix, cell.id(), cell.id()) - internalSum[cell.id()];
  }
  return out;
}

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

void report(const std::string& label, const Mesh& mesh) {
  std::vector<bool> touchesBoundary(mesh.numberOfCells(), false);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) touchesBoundary[face.owner()] = true;
  }
  const std::vector<Real> w0 = wallCoefficients(mesh, 0);
  const std::vector<Real> w1 = wallCoefficients(mesh, 1);
  const std::vector<Real> w2 = wallCoefficients(mesh, 2);

  std::size_t rows = 0;
  std::size_t differ01 = 0;
  std::size_t differ12 = 0;
  Real worst01 = 0.0;
  Real worst12 = 0.0;
  Real scale = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!touchesBoundary[i]) continue;
    ++rows;
    scale = std::max(scale, std::abs(w1[i]));
    if (!sameBits(w0[i], w1[i])) ++differ01;
    if (!sameBits(w1[i], w2[i])) ++differ12;
    worst01 = std::max(worst01, std::abs(w1[i] - w0[i]));
    worst12 = std::max(worst12, std::abs(w2[i] - w1[i]));
  }
  std::printf(
      "%-34s wall rows %4zu scale %.6e | N0!=N1 on %3zu rows (max|d| %.3e) | "
      "N1!=N2 on %3zu rows (max|d| %.3e) | %s\n",
      label.c_str(), rows, scale, differ01, worst01, differ12, worst12,
      (differ01 == 0 && differ12 == 0) ? "BITWISE-INVARIANT" : "DIFFERS");
}

Mesh distortedMesh(Index n, Real shear) {
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? shear * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? shear * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

}  // namespace

int main() {
  std::printf(
      "# A2-1 decisive: the Dirichlet wall coefficient isolated from the assembled diagonal\n");
  std::printf(
      "# by subtracting the exactly-recomputed internal-face coefficients. Must be bitwise\n");
  std::printf("# identical for non_orthogonal_corrections = 0, 1, 2.\n\n");
  report("orthogonal Cartesian 16x16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  report("orthogonal Cartesian 3D 8x8x8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  report("orthogonal graded 16x16 r=1.2",
         MeshGeometry::createGraded2D(
             16, 16, 1.0, 1.0,
             mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
             mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both}));
  report("DISTORTED 16x16 shear 0.20", distortedMesh(16, 0.20));
  report("DISTORTED 16x16 shear 0.45", distortedMesh(16, 0.45));
  report("DISTORTED 32x32 shear 0.45", distortedMesh(32, 0.45));
  return 0;
}
