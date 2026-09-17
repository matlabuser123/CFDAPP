// P12-DIFF-002-INV-002 / INV-F8: global conservation identity audit of the DIFF-002 assembled
// boundary stencil. Investigation only -- reads production, changes nothing.
//
// The identity under test. For a scalar diffusion equation the assembled row of cell P holds
// -flux_into_P. Internal faces are assembled face-once as +a to (P,P), -a to (P,N), +a to (N,N),
// -a to (N,P), so summing the rows of P and N cancels them EXACTLY: a(phi_P - phi_N) +
// a(phi_N - phi_P) = 0. A DIFF-002 boundary face contributes, to row P ONLY,
//     + Gamma|S| cP phi_P  - Gamma|S| cF phi_F  - Gamma|S| cB phi_b  - explicitFlux
// and contributes NOTHING to the far cell F's own row. So summing every row must leave exactly the
// boundary-flux terms and the sources -- the far-cell coefficient is part of the boundary FLUX
// EXPRESSION, not an extra internal coupling, and cannot double-count. This probe verifies that
// numerically rather than trusting the argument.
//
// Two independent checks per equation and mesh:
//   C1  constant-field exactness. With phi = c everywhere AND every Dirichlet value = c, the net
//       flux of every cell must be exactly zero: (A phi - b)_P = 0 for all P. This exercises the
//       consistency identity cP - cF = cB globally, including on fallback faces.
//   C2  telescoping. For an ARBITRARY field, sum(A phi - b) over all cells must equal the
//       independently assembled total boundary flux (computed here face by face from the same
//       production terms, summed separately). Any unintended non-conservative contribution from the
//       far-cell stencil would show up as a residual difference between the two totals.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using namespace cfd;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kGamma = 0.6;

discretization::NonOrthogonalCorrectionOptions options(bool enabled) {
  discretization::NonOrthogonalCorrectionOptions o;
  o.enabled = enabled;
  return o;
}

boundary::BoundaryConditionSet dirichlet(const Mesh& mesh, Real value, bool temperature) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (temperature) {
      bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(value));
    } else {
      bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedValue>(value));
    }
  }
  return bcs;
}

struct System {
  algebra::SparseMatrix matrix;
  algebra::Vector rhs;
};

System assembleThermal(const Mesh& mesh, const fields::ScalarField& phi,
                       const boundary::BoundaryConditionSet& bcs, bool corrected) {
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  thermal::assembleThermalDiffusionContribution(mesh, kGamma, phi, bcs, builder, rhs,
                                                options(corrected));
  return {builder.build(), rhs};
}

System assembleSpecies(const Mesh& mesh, const fields::ScalarField& phi,
                       const boundary::BoundaryConditionSet& bcs, bool corrected) {
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  species::assembleSpeciesDiffusionContribution(mesh, kGamma, phi, bcs, builder, rhs,
                                                options(corrected));
  return {builder.build(), rhs};
}

// The total boundary flux INTO the domain, assembled independently from the same production terms.
Real independentBoundaryFlux(const Mesh& mesh, const fields::ScalarField& phi,
                             const boundary::BoundaryConditionSet& bcs, Real boundaryValue) {
  const fields::VectorField grad =
      discretization::gradient(mesh, phi, bcs, discretization::GradientScheme::GreenGauss);
  Real total = 0.0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    const Real distance =
        MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    const auto terms =
        discretization::boundaryFaceDiffusionTerms(mesh, face, kGamma, distance, &grad, true);
    total += -((terms.coefficient * phi[face.owner()]) -
               (terms.farCellCoefficient * phi[terms.farCell])) +
             (terms.boundaryValueCoefficient * boundaryValue) + terms.explicitFlux;
  }
  return total;
}

Real maxAbs(const algebra::Vector& v) {
  Real m = 0.0;
  for (Index i = 0; i < v.size(); ++i) m = std::max(m, std::abs(v[i]));
  return m;
}

void audit(const std::string& label, const Mesh& mesh, bool species_) {
  // ---- C1 constant field, matching Dirichlet values ----
  constexpr Real kConst = 7.25;
  const fields::ScalarField uniform(mesh.numberOfCells(), kConst);
  const auto bcsConst = dirichlet(mesh, kConst, !species_);
  const System sc =
      species_ ? assembleSpecies(mesh, uniform, bcsConst, true)
               : assembleThermal(mesh, uniform, bcsConst, true);
  algebra::Vector phiConst(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) phiConst[i] = kConst;
  algebra::Vector rc = sc.matrix.multiply(phiConst);
  for (Index i = 0; i < rc.size(); ++i) rc[i] -= sc.rhs[i];
  const Real scale = kGamma * kConst / std::pow(static_cast<Real>(mesh.numberOfCells()), 0.5);

  // ---- C2 arbitrary field, telescoping ----
  constexpr Real kBoundary = 1.5;
  fields::ScalarField varied(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    varied[cell.id()] = 1.0 + (2.0 * cell.centroid().x) - (0.7 * cell.centroid().y) +
                        (0.3 * cell.centroid().x * cell.centroid().y);
  }
  const auto bcsVar = dirichlet(mesh, kBoundary, !species_);
  const System sv = species_ ? assembleSpecies(mesh, varied, bcsVar, true)
                             : assembleThermal(mesh, varied, bcsVar, true);
  algebra::Vector phiVar(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) phiVar[i] = varied[i];
  const algebra::Vector av = sv.matrix.multiply(phiVar);
  Real rowSum = 0.0;
  for (Index i = 0; i < av.size(); ++i) rowSum += av[i] - sv.rhs[i];
  // sum(A phi - b) is -(total boundary flux in); compare with the independent face-by-face total.
  const Real independent = independentBoundaryFlux(mesh, varied, bcsVar, kBoundary);
  const Real mismatch = std::abs(rowSum + independent);
  const Real fluxScale = std::max(std::abs(independent), 1e-300);

  std::printf("INV-F8 %-11s %-26s cells %5zu | C1 constant-field max|A phi - b| %.3e (scale %.1e)"
              " | C2 sum(rows) %+.12e  independent boundary flux %+.12e  mismatch %.3e"
              " (rel %.3e) | %s\n",
              species_ ? "species" : "thermal", label.c_str(),
              static_cast<std::size_t>(mesh.numberOfCells()), maxAbs(rc), scale, rowSum,
              independent, mismatch, mismatch / fluxScale,
              (maxAbs(rc) <= 1e-10 * kGamma * kConst && mismatch <= 1e-10 * fluxScale)
                  ? "CONSERVATIVE"
                  : "NON-CONSERVATIVE");
}

Mesh sheared(Index n, Real s) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool in = (i > 0 && i < n && j > 0 && j < n);
      v.push_back(Vector3{xi + (in ? s * eta / static_cast<Real>(n) : 0.0),
                          eta + (in ? s * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, v);
}

}  // namespace

int main() {
  std::printf("# INV-F8 global conservation identity audit of the DIFF-002 boundary stencil\n");
  std::printf("# C1: constant field with matching Dirichlet values -> every cell's net flux must\n");
  std::printf("#     be exactly zero (tests cP - cF = cB globally, fallback faces included).\n");
  std::printf("# C2: arbitrary field -> sum over all rows must equal the independently summed\n");
  std::printf("#     boundary flux; any non-conservative far-cell contribution appears here.\n\n");

  // Tiny hand-derivable systems first.
  std::printf("## tiny hand-derived systems\n");
  audit("2x1 (hand-derived)", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0), false);
  audit("2x1 (hand-derived)", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0), true);
  audit("3x3", MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0), false);
  audit("1x1 (all fallback)", MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0), false);
  audit("8x1 (mixed fallback)", MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0), false);

  std::printf("\n## representative production-scale meshes\n");
  for (const Index n : {10u, 20u, 40u}) {
    audit("Cartesian " + std::to_string(n) + "x" + std::to_string(n),
          MeshGeometry::createCartesian2D(n, n, 1.0, 1.0), false);
    audit("Cartesian " + std::to_string(n) + "x" + std::to_string(n),
          MeshGeometry::createCartesian2D(n, n, 1.0, 1.0), true);
  }
  audit("sheared 20x20 s=0.45", sheared(20, 0.45), false);
  audit("sheared 20x20 s=0.45", sheared(20, 0.45), true);
  audit("Cartesian 3D 8x8x8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0), false);
  audit("graded 20x20 r=1.2",
        MeshGeometry::createGraded2D(
            20, 20, 1.0, 1.0,
            mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
            mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both}),
        false);
  return 0;
}
