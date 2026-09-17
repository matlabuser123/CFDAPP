// P12-DIFF-002 A2 diagnostic: is the residual N0-vs-N1 difference that diff2_activation reports on
// a DISTORTED mesh a production difference in the Dirichlet wall scheme, or contamination of that
// instrument's observable?
//
// The observable differences the assembled rhs against the prescribed boundary value V. That picks
// up boundaryValueCoefficient, but ALSO any other V-sensitive term -- and the explicit
// non-orthogonal flux depends on grad(phi), which itself depends on the boundary values. The
// INTERNAL-face part of that explicit flux exists only when the correction is enabled (N > 0), by
// design and legitimately.
//
// Decisive separation: boundaryValueCoefficient is exactly zero for a row whose cell touches no
// boundary face. So if the N0-vs-N1 difference also appears on INTERIOR-ONLY rows, it cannot be the
// wall scheme -- it is the internal-face correction, i.e. instrument contamination. This probe
// splits the same difference into boundary-adjacent rows and interior-only rows and reports each.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

discretization::NonOrthogonalCorrectionOptions optionsFor(Index n) {
  pressure_velocity::SIMPLESettings settings;
  settings.nonOrthogonalCorrections = n;
  return pressure_velocity::nonOrthogonalOptions(settings);
}

algebra::Vector thermalRhs(const Mesh& mesh, Index n, Real wallValue) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(wallValue));
  }
  const fields::ScalarField temperature(mesh.numberOfCells(), 300.0);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  thermal::assembleThermalDiffusionContribution(mesh, 0.6, temperature, bcs, builder, rhs,
                                                optionsFor(n));
  return rhs;
}

algebra::Vector sensitivity(const Mesh& mesh, Index n) {
  const algebra::Vector lo = thermalRhs(mesh, n, 2.0);
  const algebra::Vector hi = thermalRhs(mesh, n, 3.0);
  algebra::Vector out(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) out[i] = hi[i] - lo[i];
  return out;
}

void report(const std::string& label, const Mesh& mesh) {
  std::vector<bool> touchesBoundary(mesh.numberOfCells(), false);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) touchesBoundary[face.owner()] = true;
  }
  const algebra::Vector s0 = sensitivity(mesh, 0);
  const algebra::Vector s1 = sensitivity(mesh, 1);

  Real worstBoundary = 0.0;
  Real worstInterior = 0.0;
  std::size_t interiorRows = 0;
  std::size_t interiorRowsDiffering = 0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const Real d = std::abs(s1[i] - s0[i]);
    if (touchesBoundary[i]) {
      worstBoundary = std::max(worstBoundary, d);
    } else {
      ++interiorRows;
      worstInterior = std::max(worstInterior, d);
      if (d != 0.0) ++interiorRowsDiffering;
    }
  }
  std::printf(
      "%-34s boundary-adjacent rows max|d| %.6e | INTERIOR-ONLY rows max|d| %.6e "
      "(%zu of %zu differ) -> %s\n",
      label.c_str(), worstBoundary, worstInterior, interiorRowsDiffering, interiorRows,
      (interiorRowsDiffering > 0)
          ? "difference reaches rows with NO boundary face => internal-face correction, "
            "NOT the wall scheme"
          : "confined to boundary rows");
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
      "# A2 diagnostic: where does the residual N0-vs-N1 rhs-sensitivity difference live?\n");
  std::printf(
      "# boundaryValueCoefficient is exactly 0 on a row touching no boundary face, so any\n");
  std::printf(
      "# difference on such a row is the internal-face correction, not the wall scheme.\n\n");
  report("orthogonal Cartesian 16x16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  report("DISTORTED 16x16 shear 0.45", distortedMesh(16, 0.45));
  report("DISTORTED 32x32 shear 0.45", distortedMesh(32, 0.45));
  return 0;
}
