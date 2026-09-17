// P12-DIFF-002 Step 3 finding: the CONJUGATE conduction path was left on the pre-A2 two-point
// Dirichlet wall flux while the single-material path was made unconditional DIFF-002, so the two
// production paths no longer agree for a single region. Investigation only.
//
// src/thermal/ThermalInterface.cpp:60-73 assembles a boundary face as
//     diffusionCoefficient = conductivity * face.area() / distance;          // two-point k|S|/d
//     boundaryDiffusionContribution(mesh, face, T, bcs, diffusionCoefficient)  // no boundaryValue-
//                                                                              // Coefficient, no
//                                                                              // far-cell entry
// and its own comment states "Same boundary treatment as the single-material assembly" -- which
// P12-DIFF-002 A2 made untrue when it made EnergyEquation's Dirichlet wall flux the DIFF-002
// three-point reconstruction regardless of non_orthogonal_corrections.
//
// This probe assembles both production paths for a SINGLE region (where they are required to be
// identical) and reports the difference, per row and in the solved fields.
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
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kConductivity = 2.5;

boundary::BoundaryConditionSet dirichlet(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  Real v = 300.0;
  for (const auto& p : mesh.boundaryPatches()) {
    b.set(mesh, p.name(), std::make_unique<boundary::FixedTemperature>(v));
    v += 20.0;
  }
  return b;
}

Real entry(const algebra::SparseMatrix& m, Index row, Index col) {
  const Index* off = m.rowOffsetsData();
  for (Index k = off[row]; k < off[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == col) return m.valuesData()[k];
  }
  return 0.0;
}

void report(const std::string& label, const Mesh& mesh) {
  const auto bcs = dirichlet(mesh);
  const fields::ScalarField T(mesh.numberOfCells(), 305.0);
  const fields::SurfaceField noFlow(mesh.numberOfFaces(), 0.0);

  // single-material production path
  algebra::SparseMatrixBuilder sb(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector sr(mesh.numberOfCells());
  discretization::NonOrthogonalCorrectionOptions off;  // enabled = false, the shipped default
  thermal::assembleThermalDiffusionContribution(mesh, kConductivity, T, bcs, sb, sr, off);
  const algebra::SparseMatrix single = sb.build();

  // conjugate production path, ONE region -- required to be identical
  std::vector<Index> cellRegion(mesh.numberOfCells(), 0);
  const thermal::ThermalRegionMap regions(
      {thermal::ThermalRegion{"a", thermal::ThermalRegionType::Solid,
                              thermal::ThermalProperties(kConductivity, 1.0)}},
      cellRegion);
  const auto conj = thermal::assembleConjugateConductionEquation(mesh, T, regions, bcs);

  Real worstDiag = 0.0, worstRhs = 0.0;
  std::size_t rowsDiffering = 0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const Real d = std::abs(entry(single, i, i) - entry(conj.system.matrix(), i, i));
    const Real r = std::abs(sr[i] - conj.system.rhs()[i]);
    if (d > 1e-12 || r > 1e-12) ++rowsDiffering;
    worstDiag = std::max(worstDiag, d);
    worstRhs = std::max(worstRhs, r);
  }

  // solved fields
  const auto sSolve = thermal::ThermalSolver{}.solve(
      mesh, T, noFlow, thermal::ThermalProperties(kConductivity, 1.0), bcs);
  const auto cSolve = thermal::ThermalSolver{}.solveConjugateConduction(mesh, T, regions, bcs);
  Real worstT = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    worstT = std::max(worstT, std::abs(sSolve.temperature[i] - cSolve.temperature[i]));
  }

  std::printf("%-26s cells %5zu | rows differing %4zu | max |d diag| %.6e  max |d rhs| %.6e |"
              " max |dT| solved %.6e | %s\n",
              label.c_str(), static_cast<std::size_t>(mesh.numberOfCells()), rowsDiffering,
              worstDiag, worstRhs, worstT,
              (rowsDiffering == 0) ? "PATHS AGREE" : "PATHS DISAGREE");
}

}  // namespace

int main() {
  std::printf("# Step 3 finding: single-material vs conjugate conduction, ONE region.\n");
  std::printf("# These two production paths are required to be identical (and a production test\n");
  std::printf("# asserts it). Both assembled with the shipped default non_orthogonal_corrections=0.\n\n");
  report("Cartesian 2x1", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0));
  report("Cartesian 10x10", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
  report("Cartesian 20x20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0));
  report("Cartesian 3D 8x8x8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  return 0;
}
