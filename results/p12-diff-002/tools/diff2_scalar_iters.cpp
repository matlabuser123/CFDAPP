// P12-DIFF-002 W5, second clause: "thermal and species solver iterations likewise".
//
// The frozen committed-case instrument (diff2_cases.cpp) reports only SIMPLE's outer iterations,
// and every committed thermal/species case runs with non_orthogonal_corrections = 0, where the
// reconstruction never activates. This probe therefore measures the thermal and species solvers
// directly, with the correction OFF (the pre-DIFF-002 boundary treatment) and ON (the DIFF-002
// reconstruction), and reports the iteration counts side by side.
//
// Isolation: on an ORTHOGONAL mesh the P12-NUM-003 non-orthogonal correction is documented and
// verified to be BIT-IDENTICAL to the uncorrected operator, so on a Cartesian mesh the only
// difference between OFF and ON is the DIFF-002 boundary reconstruction -- a clean isolation of
// this phase's own convergence cost. The graded and distorted rows additionally include the
// internal-face correction, so they are an upper bound on the combined cost, not a DIFF-002-only
// measurement; they are labelled accordingly.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

discretization::NonOrthogonalCorrectionOptions options(bool enabled) {
  discretization::NonOrthogonalCorrectionOptions o;
  o.enabled = enabled;
  return o;
}

// Four Dirichlet walls: every boundary face is value-prescribing, so every one of them goes through
// the reconstruction when it is enabled.
boundary::BoundaryConditionSet thermalBoundaries(const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  Real value = 300.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(value));
    value += 25.0;
  }
  return bcs;
}

boundary::BoundaryConditionSet speciesBoundaries(const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  Real value = 1.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedValue>(value));
    value += 0.5;
  }
  return bcs;
}

void thermalRow(const std::string& label, const Mesh& mesh, bool orthogonal) {
  const auto bcs = thermalBoundaries(mesh);
  const fields::ScalarField initial(mesh.numberOfCells(), 300.0);
  const fields::SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const thermal::ThermalProperties properties{0.6, 4180.0};

  Index iters[2]{};
  Index linear[2]{};
  Real norm[2]{};
  for (int on = 0; on <= 1; ++on) {
    thermal::ThermalSolverSettings settings;
    settings.nonOrthogonal = options(on == 1);
    const auto r = thermal::ThermalSolver{settings}.solve(mesh, initial, massFlux, properties, bcs);
    iters[on] = r.iterations;
    linear[on] = r.linearIterations;
    Real sum = 0.0;
    Real volume = 0.0;
    for (const auto& c : mesh.cells()) {
      sum += r.temperature[c.id()] * r.temperature[c.id()] * c.volume();
      volume += c.volume();
    }
    norm[on] = std::sqrt(sum / volume);
    if (!r.converged()) std::printf("    (thermal %s on=%d DID NOT CONVERGE)\n", label.c_str(), on);
  }
  const Real ratio =
      (iters[0] > 0) ? (static_cast<Real>(iters[1]) / static_cast<Real>(iters[0])) : 1.0;
  std::printf(
      "THERMAL  %-30s outer %4zu -> %4zu (%.4fx, bound 1.2500) linear %4zu -> %4zu | "
      "|T|2 %.12e -> %.12e | %s | %s\n",
      label.c_str(), static_cast<std::size_t>(iters[0]), static_cast<std::size_t>(iters[1]), ratio,
      static_cast<std::size_t>(linear[0]), static_cast<std::size_t>(linear[1]), norm[0], norm[1],
      orthogonal ? "DIFF-002 ONLY" : "combined (incl. NUM-003)", (ratio <= 1.25) ? "PASS" : "FAIL");
}

void speciesRow(const std::string& label, const Mesh& mesh, bool orthogonal) {
  const auto bcs = speciesBoundaries(mesh);
  const fields::ScalarField concentration(mesh.numberOfCells(), 1.0);
  const fields::SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const physics::FluidProperties fluid{998.0, 1.0e-3};
  const species::SpeciesProperties properties{"tracer", 1.0e-5};

  Index linear[2]{};
  Real norm[2]{};
  for (int on = 0; on <= 1; ++on) {
    const auto assembly = species::assembleSpeciesTransportEquation(
        mesh, concentration, massFlux, fluid, properties, bcs, 0.0, options(on == 1));
    algebra::LinearSolverSettings settings;
    const auto result = algebra::BiCGSTAB{settings}.solve(assembly.system);
    linear[on] = result.iterations;
    Real sum = 0.0;
    Real volume = 0.0;
    for (const auto& c : mesh.cells()) {
      sum += result.solution[c.id()] * result.solution[c.id()] * c.volume();
      volume += c.volume();
    }
    norm[on] = std::sqrt(sum / volume);
  }
  const Real ratio =
      (linear[0] > 0) ? (static_cast<Real>(linear[1]) / static_cast<Real>(linear[0])) : 1.0;
  std::printf(
      "SPECIES  %-30s linear %4zu -> %4zu (%.4fx, bound 1.2500) | |Y|2 %.12e -> %.12e | "
      "%s | %s\n",
      label.c_str(), static_cast<std::size_t>(linear[0]), static_cast<std::size_t>(linear[1]),
      ratio, norm[0], norm[1], orthogonal ? "DIFF-002 ONLY" : "combined (incl. NUM-003)",
      (ratio <= 1.25) ? "PASS" : "FAIL");
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002 W5 (thermal/species): iteration counts with the boundary\n");
  std::printf("# reconstruction OFF -> ON. Guard: <= 1.25x, the frozen W5 bound.\n");
  std::printf("# On an orthogonal mesh OFF->ON isolates DIFF-002 exactly (NUM-003 is\n");
  std::printf("# bit-identical there); graded/distorted rows are a combined upper bound.\n\n");

  for (const Index n : {20u, 40u, 80u}) {
    const std::string label = "Cartesian " + std::to_string(n) + "x" + std::to_string(n);
    const Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
    thermalRow(label, mesh, true);
    speciesRow(label, mesh, true);
  }
  {
    const Mesh mesh = MeshGeometry::createCartesian3D(16, 16, 16, 1.0, 1.0, 1.0);
    thermalRow("Cartesian 3D 16x16x16", mesh, true);
    speciesRow("Cartesian 3D 16x16x16", mesh, true);
  }
  {
    const Mesh mesh = MeshGeometry::createGraded2D(
        40, 40, 1.0, 1.0,
        mesh::AxisGrading{mesh::GradingType::Geometric, 1.15, mesh::GradingCluster::Both},
        mesh::AxisGrading{mesh::GradingType::Geometric, 1.15, mesh::GradingCluster::Both});
    thermalRow("graded 40x40 r=1.15", mesh, false);
    speciesRow("graded 40x40 r=1.15", mesh, false);
  }
  {
    // Interior nodes only are displaced, so the boundary patches stay exactly planar and the
    // prescribed boundary values stay exact (the GRAD-002 instrument lesson).
    const Index n = 40;
    std::vector<Vector3> vertices;
    for (Index j = 0; j <= n; ++j) {
      for (Index i = 0; i <= n; ++i) {
        const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
        const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
        const bool interior = (i > 0 && i < n && j > 0 && j < n);
        const Real dx = interior ? (0.45 * eta / static_cast<Real>(n)) : 0.0;
        const Real dy = interior ? (0.45 * xi / static_cast<Real>(n)) : 0.0;
        vertices.push_back(Vector3{xi + dx, eta + dy, 0.0});
      }
    }
    const Mesh mesh = MeshGeometry::createStructuredQuad2D(n, n, vertices);
    thermalRow("distorted 40x40 shear 0.45", mesh, false);
    speciesRow("distorted 40x40 shear 0.45", mesh, false);
  }
  return 0;
}
