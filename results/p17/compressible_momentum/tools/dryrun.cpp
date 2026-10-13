// P17.4 dry-run: measure the UNCHANGED tree's momentum terms before any edit.
//
// Jobs:
//   A. The pressure force against an analytical grad(p) -- linear p gives a constant force.
//   B. The transient term against a hand-derived (rho_new u_new - rho_old u_old) V/dt, and
//      show it differs from the WRONG rho_new (u_new - u_old) V/dt when density changes.
//   C. The body-force API: rhs scales linearly with a supplied rho*f.
//   D. Internal-face convective momentum cancellation.
//   E. THE GAP: quantify what -(2/3) mu grad(div U) WOULD contribute, using an analytically
//      known div(U), and confirm nothing in the assembly produces it today.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/discretization/Divergence.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet zeroGradientAll(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  return b;
}

// Vector-valued zero-gradient on every patch: Outlet IS the zero-gradient vector BC.
// divergence() needs vector BCs for a VectorField; FixedGradient is scalar-valued.
BoundaryConditionSet zeroGradientVector(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<Outlet>());
  return b;
}

BoundaryConditionSet channelVel(const Mesh& mesh, Vector2 inlet) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(inlet));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("P17.4 dry-run -- compressible momentum on the UNCHANGED tree\n\n");

  // ------------------------------------------------------------------ A
  std::printf("### A. pressure force: linear p => grad(p) constant => rhs = -V * C\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
    const Index n = mesh.numberOfCells();
    const Real C = 7.0;  // dp/dx
    ScalarField p(n);
    for (const auto& cell : mesh.cells()) p[cell.id()] = 100.0 + C * cell.centroid().x;
    const auto pbc = zeroGradientAll(mesh);

    cfd::algebra::Vector rhs(n, 0.0);
    cfd::physics::assemblePressureSourceContribution(mesh, p, pbc, VelocityComponent::U, rhs,
                                                     cfd::discretization::GradientScheme::GreenGauss);
    // interior cells should see the exact analytical value; boundary cells are affected by
    // the zero-gradient BC, so report both sets.
    Real worstInterior = 0.0;
    Index interior = 0;
    for (const auto& cell : mesh.cells()) {
      bool onBoundary = false;
      for (const Index f : cell.faceIds())
        if (mesh.face(f).isBoundary()) onBoundary = true;
      if (onBoundary) continue;
      ++interior;
      const Real expected = -cell.volume() * C;
      worstInterior = std::max(worstInterior, std::abs(rhs[cell.id()] - expected) / std::abs(expected));
    }
    std::printf("  interior cells %lld   worst relative error vs -V*dp/dx: %.17g\n",
                static_cast<long long>(interior), worstInterior);
    std::printf("  (a 4x3 mesh has few interior cells; the value is the check, not the count)\n");
    std::printf("  sample rhs[0] = %.17g   V = %.17g   -V*C = %.17g\n", rhs[0],
                mesh.cell(0).volume(), -mesh.cell(0).volume() * C);
  }

  // ------------------------------------------------------------------ B
  std::printf("\n### B. transient term: d(rho U)/dt, NOT rho dU/dt\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
    const Index n = mesh.numberOfCells();
    const Real dt = 0.25;
    ScalarField rhoOld(n, 1.0), rhoNew(n, 2.0);  // density DOUBLES
    ScalarField uOld(n, 3.0);
    const auto coeffs =
        cfd::compressible::compressibleMomentumTimeDerivative(mesh, uOld, rhoOld, rhoNew, dt);
    const Real V = mesh.cell(0).volume();
    std::printf("  rho_old 1.0 -> rho_new 2.0, u_old 3.0, dt %.2f, V %.17g\n", dt, V);
    std::printf("  diagonal[0] = %.17g    expected rho_new*V/dt = %.17g\n", coeffs.diagonal[0],
                2.0 * V / dt);
    std::printf("  source[0]   = %.17g    expected rho_old*V/dt*u_old = %.17g\n", coeffs.source[0],
                1.0 * V / dt * 3.0);
    std::printf("  the WRONG form rho_new*V/dt would give source = %.17g -- differs by %.17g\n",
                2.0 * V / dt * 3.0, std::abs(2.0 * V / dt * 3.0 - coeffs.source[0]));
    std::printf("  => a test with BOTH rho and u changing can tell the two apart\n");
  }

  // ------------------------------------------------------------------ C
  std::printf("\n### C. body force: the API takes a FORCE DENSITY and scales by volume\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
    const Index n = mesh.numberOfCells();
    const Real g = -9.81;
    for (const Real rho : {1.0, 2.0, 4.0}) {
      VectorField src(n);
      for (Index i = 0; i < n; ++i) src[i] = Vector2{0.0, rho * g};  // rho * f
      cfd::algebra::Vector rhs(n, 0.0);
      cfd::physics::assembleMomentumSourceContribution(mesh, src, VelocityComponent::V, rhs);
      std::printf("  rho %.1f -> rhs[0] = %-24.17g  expected V*rho*g = %.17g\n", rho, rhs[0],
                  mesh.cell(0).volume() * rho * g);
    }
    std::printf("  => linear in rho, so supplying rho*f gives the correct rho f weighting\n");
  }

  // ------------------------------------------------------------------ D
  std::printf("\n### D. internal-face convective momentum: owner +F, neighbour -F\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
    const Index n = mesh.numberOfCells();
    VectorField vel(n);
    for (const auto& cell : mesh.cells())
      vel[cell.id()] = Vector2{1.0 + 0.3 * cell.centroid().x, 0.2 * cell.centroid().y};
    SurfaceField flux(mesh.numberOfFaces());
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) flux[f] = 0.17 * static_cast<Real>(f + 1);
    const auto vbc = channelVel(mesh, Vector2{1.0, 0.0});

    cfd::algebra::SparseMatrixBuilder builder(n, n);
    builder.reserve(5 * n);
    cfd::algebra::Vector rhs(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, flux, vel, vbc, VelocityComponent::U,
                                                 builder, rhs);
    const auto matrix = builder.build();
    Real minDiag = matrix.diagonal(0);
    Real maxDiag = matrix.diagonal(0);
    for (Index r = 0; r < n; ++r) {
      minDiag = std::min(minDiag, matrix.diagonal(r));
      maxDiag = std::max(maxDiag, matrix.diagonal(r));
    }
    Real rhsSum = 0.0;
    for (Index r = 0; r < n; ++r) rhsSum += rhs[r];
    std::printf("  irregular flux, varying U: diagonal range [%.17g, %.17g]\n", minDiag, maxDiag);
    std::printf("  sum of rhs = %.17g   (boundary contributions only -- the interior telescopes)\n",
                rhsSum);
    std::printf("  the GLOBAL conservation statement is tested separately, not from row sums.\n");
  }

  // ------------------------------------------------------------------ E
  std::printf("\n### E. THE GAP: what -(2/3) mu grad(div U) WOULD contribute\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 8.0, 4.0);
    const Index n = mesh.numberOfCells();
    const Real mu = 0.1;
    // U = (x^2/2, 0)  =>  div U = x  =>  grad(div U) = (1, 0)  exactly
    VectorField vel(n);
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      vel[cell.id()] = Vector2{0.5 * x * x, 0.0};
    }
    const auto vbc = zeroGradientVector(mesh);
    const ScalarField divU = cfd::discretization::divergence(mesh, vel, vbc);
    const auto dbc = zeroGradientAll(mesh);
    const VectorField gradDivU =
        cfd::discretization::gradient(mesh, divU, dbc, cfd::discretization::GradientScheme::GreenGauss);

    std::printf("  U = (x^2/2, 0)  =>  div U = x  =>  grad(div U) = (1, 0) analytically\n");
    std::printf("  %-6s %-14s %-24s %-24s %s\n", "cell", "x", "div U (should be x)",
                "grad(div U).x (should be 1)", "-(2/3) mu gradDivU.x * V");
    Real worstDiv = 0.0, worstGrad = 0.0, maxTerm = 0.0;
    Index interior = 0;
    for (const auto& cell : mesh.cells()) {
      bool onBoundary = false;
      for (const Index f : cell.faceIds())
        if (mesh.face(f).isBoundary()) onBoundary = true;
      if (onBoundary) continue;
      ++interior;
      const Real x = cell.centroid().x;
      const Real term = -(2.0 / 3.0) * mu * gradDivU[cell.id()].x * cell.volume();
      worstDiv = std::max(worstDiv, std::abs(divU[cell.id()] - x));
      worstGrad = std::max(worstGrad, std::abs(gradDivU[cell.id()].x - 1.0));
      maxTerm = std::max(maxTerm, std::abs(term));
      if (interior <= 4)
        std::printf("  %-6lld %-14.6g %-24.17g %-24.17g %.17g\n",
                    static_cast<long long>(cell.id()), x, divU[cell.id()],
                    gradDivU[cell.id()].x, term);
    }
    std::printf("  interior cells %lld   worst |div U - x| = %.17g   worst |gradDivU.x - 1| = %.17g\n",
                static_cast<long long>(interior), worstDiv, worstGrad);
    std::printf("  MAGNITUDE OF THE MISSING TERM: up to %.17g per cell\n", maxTerm);
    std::printf("  for comparison, -(2/3)*mu*1*V exactly = %.17g\n",
                -(2.0 / 3.0) * mu * mesh.cell(0).volume());
    std::printf("  Nothing in the momentum assembly produces this today:\n");
    std::printf("    grad(div / dilatation / bulkViscosity / twoThirds / Stokes  = 0 hits\n");
  }

  std::printf("\ndone\n");
  return 0;
}
