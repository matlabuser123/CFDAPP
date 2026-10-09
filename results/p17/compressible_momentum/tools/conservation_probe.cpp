// P17.4 -- gate criteria C2 and C4, which the freeze specified with a derived bound and which
// NO test implemented. Found by auditing the frozen gate against the tests that exist -- the
// same kind of gap control N6 exposed for D3. Probed here as a standalone program first, so the
// numbers and the bound are known BEFORE they become assertions in the suite.
//
// C2  Uniform flow (rho, U, p all constant) gives a momentum residual at round-off.
//     For a uniform field every term vanishes for a structural reason, not a numerical one:
//       convection   sum_f m_f U_f = U sum_f m_f = U * (net mass flux) = 0 on a Cartesian mesh
//       diffusion    lap(constant) = 0
//       pressure     grad(constant) = 0
//       transient    rho V/dt u - rho V/dt u = 0   (rho_old == rho_new, u_old == u_new)
//     So the residual A u - b must be round-off, and anything larger is a real defect.
//
// C4  The assembly is GLOBALLY CONSERVATIVE: an internal face contributes +x to its owner and
//     -x to its neighbour, so summing over all cells cancels every internal face and leaves
//     boundary faces only. With the boundary mass flux zero -- a closed box -- the global sum of
//     the convective operator must be zero for ANY field and ANY internal flux. Note this needs
//     no divergence-free assumption: the cancellation is per FACE, not per cell. Per-cell values
//     are O(1), which is what keeps the test from being vacuous.
//
// Each criterion carries an IN-TEST control. physics/MomentumEquation.cpp may not be touched
// (criterion L5), so the controls perturb the INPUT rather than the code.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
// EMPIRICAL, and labelled as such, exactly as P17.1 and P17.3 label it. Reused verbatim, not
// re-derived and not tuned to the numbers this program prints.
constexpr Real kAccum = 8.0;

BoundaryConditionSet allOutlet(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<Outlet>());
  return b;
}

// Pressure is a SCALAR field: interpolateFace rejects a vector-valued BC like Outlet for it.
// Outlet is the zero-gradient VECTOR BC; FixedGradient(0.0) is its scalar counterpart.
BoundaryConditionSet zeroGradientScalar(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  return b;
}

BoundaryConditionSet allWall(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<Wall>());
  return b;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("P17.4 conservation probe -- gate criteria C2 and C4\n");
  std::printf("kAccum = %.1f  (EMPIRICAL)   eps = %.17g\n\n", kAccum, kEps);

  // ---------------------------------------------------------------- C2
  std::printf("### C2. uniform rho, U, p  ->  residual A u - b at round-off\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 8.0, 8.0);
    const Index n = mesh.numberOfCells();
    const Real rho = 1.3;
    const Real uUniform = 2.0;
    const Real mu = 0.05;
    const Real dt = 0.5;

    const VectorField velocity(n, Vector2{uUniform, 0.0});
    const ScalarField pressure(n, 1000.0);
    const ScalarField density(n, rho);
    const ScalarField previous(n, uUniform);
    const auto vbc = allOutlet(mesh);
    const auto pbc = zeroGradientScalar(mesh);

    // The mass flux a uniform field actually produces. Face exposes areaVector() -- the outward
    // normal scaled by the area -- not normal(), so this is rho * (U . areaVector) directly.
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    for (const auto& face : mesh.faces()) {
      massFlux[face.id()] = rho * (uUniform * face.areaVector().x);
    }

    const MomentumAssembly a = cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
        previous, 1.0, dt, {}, cfd::discretization::GradientScheme::GreenGauss, nullptr, false);

    // SparseMatrix is immutable CSR with no row iterator; multiply() is the supported way to
    // apply it.
    const auto& A = a.system.matrix();
    cfd::algebra::Vector u(n, uUniform);
    const cfd::algebra::Vector Au = A.multiply(u);
    Real worst = 0.0;
    Real scale = 0.0;
    for (Index row = 0; row < n; ++row) {
      worst = std::max(worst, std::abs(Au[row] - a.system.rhs()[row]));
      scale = std::max(scale, std::abs(A.diagonal(row) * uUniform));
    }
    const Real bound = kAccum * kEps * static_cast<Real>(n) * scale;
    std::printf("  cells N                              %lld\n", static_cast<long long>(n));
    std::printf("  scale = max|diag * u|                %.17g\n", scale);
    std::printf("  worst |A u - b|                      %.17g\n", worst);
    std::printf("  bound = kAccum * eps * N * scale     %.17g\n", bound);
    std::printf("  verdict                              %s\n",
                worst <= bound ? "WITHIN the derived bound" : "*** EXCEEDS the bound ***");
    std::printf("  margin (bound / measured)            %.6g\n",
                worst > 0.0 ? bound / worst : std::numeric_limits<Real>::infinity());

    // IN-TEST CONTROL: break uniformity in one cell and the residual must leave the bound far
    // behind. Without this, "the residual is small" could equally mean the operator is small.
    cfd::algebra::Vector perturbed(n, uUniform);
    perturbed[n / 2] += 1.0e-3;
    const cfd::algebra::Vector AuP = A.multiply(perturbed);
    Real worstP = 0.0;
    for (Index row = 0; row < n; ++row)
      worstP = std::max(worstP, std::abs(AuP[row] - a.system.rhs()[row]));
    std::printf("  CONTROL: one cell perturbed by 1e-3\n");
    std::printf("    worst |A u - b|                    %.17g\n", worstP);
    std::printf("    exceeds the bound by               %.6g x\n", worstP / bound);
    std::printf("    verdict                            %s\n",
                worstP > bound ? "DETECTED (the criterion can fail)" : "*** VACUOUS ***");
  }

  // ---------------------------------------------------------------- C4
  std::printf("\n### C4. global conservation: internal faces cancel, closed box -> sum = 0\n");
  {
    const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 7.0, 5.0);
    const Index n = mesh.numberOfCells();
    const auto vbc = allWall(mesh);  // impermeable: zero normal mass flux at every boundary

    // A deliberately IRREGULAR internal flux. No divergence-free assumption is needed -- the
    // cancellation this criterion tests is per FACE, not per cell.
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) continue;  // closed box
      const Real f = static_cast<Real>(face.id());
      massFlux[face.id()] = 0.37 * std::sin(0.7 * f) + 0.11 * static_cast<Real>(face.id() % 5);
    }

    // An arbitrary, non-uniform field.
    VectorField velocity(n);
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      const Real y = cell.centroid().y;
      velocity[cell.id()] = Vector2{0.9 + 0.4 * std::sin(x) + 0.2 * y, 0.3 * std::cos(y)};
    }

    cfd::algebra::SparseMatrixBuilder builder(n, n);
    cfd::algebra::Vector rhs(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, massFlux, velocity, vbc,
                                                 VelocityComponent::U, builder, rhs);
    const cfd::algebra::SparseMatrix A = builder.build();

    cfd::algebra::Vector ux(n, 0.0);
    for (Index i = 0; i < n; ++i) ux[i] = velocity[i].x;
    const cfd::algebra::Vector Au = A.multiply(ux);

    Real total = 0.0;
    Real worstCell = 0.0;
    for (Index row = 0; row < n; ++row) {
      const Real cellValue = Au[row] - rhs[row];
      total += cellValue;
      worstCell = std::max(worstCell, std::abs(cellValue));
    }
    const Real bound = kAccum * kEps * static_cast<Real>(mesh.numberOfFaces()) * worstCell;
    std::printf("  cells N                              %lld\n", static_cast<long long>(n));
    std::printf("  faces N_f                            %lld\n",
                static_cast<long long>(mesh.numberOfFaces()));
    std::printf("  worst PER-CELL |contribution|        %.17g   <- non-vacuity: must be >> 0\n",
                worstCell);
    std::printf("  GLOBAL sum over all cells            %.17g\n", total);
    std::printf("  bound = kAccum * eps * N_f * scale   %.17g\n", bound);
    std::printf("  verdict                              %s\n",
                std::abs(total) <= bound ? "WITHIN the derived bound"
                                         : "*** EXCEEDS the bound ***");
    std::printf("  ratio worstCell / |global sum|       %.6g   <- how much cancelled\n",
                total != 0.0 ? worstCell / std::abs(total)
                             : std::numeric_limits<Real>::infinity());

    // IN-TEST CONTROL: open the box. Give one boundary face a non-zero mass flux; that face has
    // no partner to cancel against, so the global sum must become O(1). This proves the zero
    // above is the interior telescoping, not an operator that sums to zero for trivial reasons.
    SurfaceField opened = massFlux;
    Index firstBoundary = 0;
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) {
        firstBoundary = face.id();
        break;
      }
    }
    opened[firstBoundary] = 1.7;
    cfd::algebra::SparseMatrixBuilder b2(n, n);
    cfd::algebra::Vector rhs2(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, opened, velocity, vbc,
                                                 VelocityComponent::U, b2, rhs2);
    const cfd::algebra::SparseMatrix A2 = b2.build();
    const cfd::algebra::Vector Au2 = A2.multiply(ux);
    Real total2 = 0.0;
    for (Index row = 0; row < n; ++row) total2 += Au2[row] - rhs2[row];
    std::printf("  CONTROL: one boundary face given flux 1.7\n");
    std::printf("    GLOBAL sum                         %.17g\n", total2);
    std::printf("    exceeds the bound by               %.6g x\n", std::abs(total2) / bound);
    std::printf("    verdict                            %s\n",
                std::abs(total2) > bound ? "DETECTED (the criterion can fail)"
                                         : "*** VACUOUS ***");
  }

  std::printf("\ndone\n");
  return 0;
}
