// P12-DIFF-002-INV-002 / INV-F3: species conservation analysis. Investigation only.
//
// The failing assertion (tests/integration/species/test_species_conservation.cpp:274) computes its
// own global balance. Its boundary DIFFUSIVE term is written inline as
//     diffusiveOut = rho D |S| / d * (Y_P - Y_b)
// i.e. the pre-A2 TWO-POINT wall flux, while the solver assembled the DIFF-002 three-point wall
// flux. So the test's balance mixes two different flux definitions -- the same instrument mismatch
// A3-2 found in the sector test and A4 catalogued as sub-class B.
//
// This probe reproduces the test's case exactly and computes the SAME balance three ways:
//   (1) TEST-ESTIMATOR  -- the inline two-point diffusive flux the test uses;
//   (2) CONSISTENT      -- the boundary flux actually assembled, from the production terms via the
//                          documented convention -(cP Y_P - cF Y_F) + cB Y_b + explicitFlux;
//   (3) ASSEMBLED-ROW   -- an entirely independent route: sum the assembled residual A Y - b over
//                          all cells, which by the telescoping identity equals the total boundary
//                          flux plus sources, with no per-face flux reconstruction at all.
// Convective terms and the source are identical in all three, so any difference is the boundary
// diffusive definition alone.
//
// Then the same balance is measured on a systematically refined mesh family, to distinguish a
// coarse-grid miss that converges from a conservation defect that plateaus or grows.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/species/SpeciesSolver.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Exactly the test's configuration.
constexpr Real kLength = 1.0;
constexpr Real kHeight = 0.2;
constexpr Real kU = 1.0;
constexpr Real kDiffusivity = 0.02;
constexpr Real kSource = 0.5;

struct Balance {
  Real netOutTest{};
  Real netOutConsistent{};
  Real rowSumResidual{};
  Real totalSource{};
  Real convectiveOut{};
  Index iterations{};
  bool converged{};
  Real solverResidual{};
  bool finite{true};
};

Balance run(Index nx, Index ny) {
  Balance b;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, kLength, kHeight);
  boundary::BoundaryConditionSet vel;
  vel.set(mesh, "left", std::make_unique<boundary::Inlet>(Vector3{kU, 0.0, 0.0}));
  vel.set(mesh, "right", std::make_unique<boundary::Outlet>());
  vel.set(mesh, "top", std::make_unique<boundary::Wall>());
  vel.set(mesh, "bottom", std::make_unique<boundary::Wall>());

  boundary::BoundaryConditionSet conc;
  conc.set(mesh, "left", std::make_unique<boundary::FixedValue>(0.0));
  conc.set(mesh, "right", std::make_unique<boundary::FixedGradient>(0.0));
  conc.set(mesh, "top", std::make_unique<boundary::FixedGradient>(0.0));
  conc.set(mesh, "bottom", std::make_unique<boundary::FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const fields::VectorField velocity(n, Vector3{kU, 0.0, 0.0});
  const physics::FluidProperties fluid(1.0, 1.0);
  const fields::SurfaceField massFlux = physics::calculateMassFlux(mesh, velocity, fluid, vel);
  const species::SpeciesProperties props("tracer", kDiffusivity);
  const auto result = species::SpeciesSolver{}.solve(mesh, fields::ScalarField(n, 0.0), massFlux,
                                                     fluid, props, conc, kSource);
  b.converged = result.status == species::SpeciesStatus::Converged;
  b.iterations = result.iterations;
  const auto& Y = result.concentration;
  for (Index i = 0; i < Y.size(); ++i) b.finite = b.finite && std::isfinite(Y[i]);

  Real volume = 0.0;
  for (const auto& cell : mesh.cells()) volume += cell.volume();
  b.totalSource = kSource * volume;

  const Real rhoD = fluid.density() * kDiffusivity;
  const fields::VectorField gradY =
      discretization::gradient(mesh, Y, conc, discretization::GradientScheme::GreenGauss);

  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    const Index owner = face.owner();
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), conc);
    const auto& sbc = dynamic_cast<const boundary::ScalarBoundaryCondition&>(bc);
    const Real d = MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
    const Real yB = sbc.boundaryValue(Y[owner], d);

    // (1) the test's inline two-point estimator
    b.netOutTest += rhoD * face.area() / d * (Y[owner] - yB);

    // (2) the boundary flux production actually assembled
    const bool prescribed = discretization::prescribesBoundaryValue(bc.type());
    const auto terms =
        discretization::boundaryFaceDiffusionTerms(mesh, face, rhoD, d, &gradY, prescribed);
    const Real intoOwner = -((terms.coefficient * Y[owner]) -
                             (terms.farCellCoefficient * Y[terms.farCell])) +
                           (terms.boundaryValueCoefficient * yB) + terms.explicitFlux;
    b.netOutConsistent += -intoOwner;  // out of the domain

    // convective term, identical in both (the test's own upwind branch)
    const Real m = massFlux[face.id()];
    const Real conv = m * ((m >= 0.0) ? Y[owner] : yB);
    b.convectiveOut += conv;
  }
  b.netOutTest += b.convectiveOut;
  b.netOutConsistent += b.convectiveOut;

  // (3) fully independent route: sum the assembled residual over all cells.
  algebra::SparseMatrixBuilder builder(n, n);
  algebra::Vector rhs(n);
  discretization::NonOrthogonalCorrectionOptions on;
  on.enabled = true;
  species::assembleSpeciesDiffusionContribution(mesh, rhoD, Y, conc, builder, rhs, on);
  species::assembleSpeciesConvectionContribution(mesh, massFlux, Y, conc, builder, rhs);
  species::assembleSpeciesSourceContribution(mesh, kSource, rhs);
  const algebra::SparseMatrix A = builder.build();
  algebra::Vector y(n);
  for (Index i = 0; i < n; ++i) y[i] = Y[i];
  const algebra::Vector Ay = A.multiply(y);
  Real res = 0.0;
  Real worst = 0.0;
  for (Index i = 0; i < n; ++i) {
    res += Ay[i] - rhs[i];
    worst = std::max(worst, std::abs(Ay[i] - rhs[i]));
  }
  b.rowSumResidual = res;
  b.solverResidual = worst;
  return b;
}

void report(Index nx, Index ny) {
  const Balance b = run(nx, ny);
  const Real scale = std::max(std::abs(b.totalSource), 1e-6);
  std::printf("INV-F3 %3zux%-3zu conv %d it %3zu finite %d | source %.10f | netOut TEST %.10f"
              " (rel err %.6e  bound 1e-3 %s) | netOut CONSISTENT %.10f (rel err %.6e %s)"
              " | sum(A Y - b) %.3e  max cell resid %.3e\n",
              static_cast<std::size_t>(nx), static_cast<std::size_t>(ny), int(b.converged),
              static_cast<std::size_t>(b.iterations), int(b.finite), b.totalSource, b.netOutTest,
              std::abs(b.netOutTest - b.totalSource) / scale,
              (std::abs(b.netOutTest - b.totalSource) / scale < 1e-3) ? "PASS" : "FAIL",
              b.netOutConsistent, std::abs(b.netOutConsistent - b.totalSource) / scale,
              (std::abs(b.netOutConsistent - b.totalSource) / scale < 1e-3) ? "PASS" : "FAIL",
              b.rowSumResidual, b.solverResidual);
}

}  // namespace

int main() {
  std::printf("# INV-F3 species conservation. The failing case is 60x4 (the test's own mesh).\n");
  std::printf("# TEST       = the test's inline two-point boundary diffusive flux\n");
  std::printf("# CONSISTENT = the boundary flux production actually assembled (DIFF-002)\n");
  std::printf("# sum(A Y - b) = an independent route with no per-face flux reconstruction\n\n");
  std::printf("## the failing configuration, reproduced (run 3x to establish determinism)\n");
  for (int i = 0; i < 3; ++i) report(60, 4);
  std::printf("\n## systematic refinement: does the imbalance converge, plateau, or worsen?\n");
  report(30, 2);
  report(60, 4);
  report(120, 8);
  report(240, 16);
  return 0;
}
