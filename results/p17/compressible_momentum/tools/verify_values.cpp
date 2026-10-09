// P17.4 post-implementation measurement. Reports the VALUES the gate's criteria turn into
// PASS/FAIL, because "do not report only PASS/FAIL" needs numbers to report.
//
// Distinct from tools/dryrun.cpp, which measured the UNCHANGED tree and whose section E
// quantified the term as a GAP. This one exercises the IMPLEMENTED operator, on the meshes
// the tests actually use after the N2 fix (10x10 over 20.0x20.0, so cell volume is 4 and the
// V_P factor is observable).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/compressible/CompressibleRelaxedMomentum.hpp"
#include "cfd/compressible/DilatationalStress.hpp"
#include "cfd/discretization/Divergence.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Outlet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet zeroGradientVelocity(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<Outlet>());
  return b;
}

BoundaryConditionSet zeroGradientScalar(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches())
    b.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  return b;
}

// div U = x for U = (x^2/2, 0).
VectorField quadraticX(const Mesh& mesh) {
  VectorField v(mesh.numberOfCells());
  for (const auto& cell : mesh.cells())
    v[cell.id()] = Vector2{0.5 * cell.centroid().x * cell.centroid().x, 0.0};
  return v;
}

// div U = 0 exactly: solid-body rotation.
VectorField solenoidal(const Mesh& mesh) {
  VectorField v(mesh.numberOfCells());
  for (const auto& cell : mesh.cells())
    v[cell.id()] = Vector2{-cell.centroid().y, cell.centroid().x};
  return v;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const Real lx = 20.0;
  const Real ly = 20.0;
  const Index nx = 10;
  const Index ny = 10;
  const Real h = lx / static_cast<Real>(nx);
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const Index n = mesh.numberOfCells();
  const Real mu = 0.1;

  std::printf("P17.4 measured values -- the IMPLEMENTED operator\n");
  std::printf("mesh %lldx%lld over %.1f x %.1f  =>  h = %.17g   cell volume = %.17g\n\n",
              static_cast<long long>(nx), static_cast<long long>(ny), lx, ly, h,
              mesh.cells()[0].volume());

  const auto vbc = zeroGradientVelocity(mesh);
  const auto sbc = zeroGradientScalar(mesh);

  // --- A. the operator against the analytical value ------------------------
  std::printf("### A. U = (x^2/2, 0)  =>  div U = x,  grad(div U) = (1, 0) analytically\n");
  {
    const VectorField velocity = quadraticX(mesh);
    const ScalarField divU = cfd::discretization::divergence(mesh, velocity, vbc);
    const VectorField gradDivU = cfd::discretization::gradient(mesh, divU, sbc);

    cfd::algebra::Vector rhs(n, 0.0);
    cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, mu,
                                                              VelocityComponent::U, rhs);

    const Real expected = -(2.0 / 3.0) * mu * 1.0 * mesh.cells()[0].volume();
    Real worstDiv = 0.0;
    Real worstGrad = 0.0;
    Real worstRhs = 0.0;
    Index deep = 0;
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      const Real y = cell.centroid().y;
      worstDiv = std::max(worstDiv, std::abs(divU[cell.id()] - x));
      const bool isDeep = x > 2.0 * h && x < lx - 2.0 * h && y > 2.0 * h && y < ly - 2.0 * h;
      if (!isDeep) continue;
      ++deep;
      worstGrad = std::max(worstGrad, std::abs(gradDivU[cell.id()].x - 1.0));
      worstRhs = std::max(worstRhs, std::abs(rhs[cell.id()] - expected));
    }
    // Broken down deliberately: an earlier draft of formulation.md claimed div U was exact
    // EVERYWHERE. It is not -- the zero-gradient velocity BC sets the boundary face value to
    // the owner cell's, which halves the one-sided difference there. The earlier measurement
    // had been taken over INTERIOR cells only and the claim overstated it.
    Real worstDivInterior = 0.0;
    Real worstDivBoundaryCell = 0.0;
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      const Real y = cell.centroid().y;
      const Real e = std::abs(divU[cell.id()] - x);
      if (x > h && x < lx - h && y > h && y < ly - h) {
        worstDivInterior = std::max(worstDivInterior, e);
      } else {
        worstDivBoundaryCell = std::max(worstDivBoundaryCell, e);
      }
    }
    std::printf("  worst |div U - x|, ALL cells                 %.17g\n", worstDiv);
    std::printf("  worst |div U - x|, boundary-adjacent cells    %.17g\n", worstDivBoundaryCell);
    std::printf("  worst |div U - x|, interior cells             %.17g\n", worstDivInterior);
    std::printf("  deep-interior cells (2+ cells from any wall) %lld\n",
                static_cast<long long>(deep));
    std::printf("  worst |grad(div U).x - 1|, deep interior     %.17g\n", worstGrad);
    std::printf("  expected contribution -(2/3)*mu*1*V          %.17g\n", expected);
    std::printf("  worst |rhs - expected|, deep interior        %.17g\n", worstRhs);
    std::printf("  V_P sensitivity: with V=1 the expected value would be %.17g\n",
                -(2.0 / 3.0) * mu);
    std::printf("    => dropping the V_P factor is a factor-%.17g error, which is what makes\n",
                mesh.cells()[0].volume());
    std::printf("       control N2b non-vacuous (it was a no-op when V was exactly 1).\n");
  }

  // --- B. the divergence-free limit ---------------------------------------
  std::printf("\n### B. a divergence-free field must give EXACTLY zero\n");
  {
    const VectorField velocity = solenoidal(mesh);
    cfd::algebra::Vector rhs(n, 0.0);
    cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, mu,
                                                              VelocityComponent::U, rhs);
    Real worst = 0.0;
    Index nonzero = 0;
    for (Index i = 0; i < n; ++i) {
      worst = std::max(worst, std::abs(rhs[i]));
      if (rhs[i] != 0.0) ++nonzero;
    }
    std::printf("  worst |rhs|                 %.17g\n", worst);
    std::printf("  cells with rhs != 0         %lld  (0 = bitwise zero everywhere)\n",
                static_cast<long long>(nonzero));
  }

  // --- C. viscosity scaling ----------------------------------------------
  std::printf("\n### C. linear in mu, and exactly zero at mu = 0\n");
  {
    const VectorField velocity = quadraticX(mesh);
    // A DEEP-INTERIOR probe cell, not mesh.numberOfCells()/2: on a 10x10 mesh index 50 is
    // row 5 column 0, i.e. a boundary column, whose magnitude is not the analytical value.
    // Linearity held there, but the number printed was not comparable to -(2/3) mu V.
    Index probe = 0;
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      const Real y = cell.centroid().y;
      if (x > 2.0 * h && x < lx - 2.0 * h && y > 2.0 * h && y < ly - 2.0 * h) {
        probe = cell.id();
        break;
      }
    }
    std::printf("  probe cell %lld at (%.17g, %.17g), deep interior\n",
                static_cast<long long>(probe), mesh.cells()[probe].centroid().x,
                mesh.cells()[probe].centroid().y);
    for (const Real m : {0.0, 0.05, 0.1, 0.2}) {
      cfd::algebra::Vector rhs(n, 0.0);
      cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, m,
                                                                VelocityComponent::U, rhs);
      std::printf("  mu %-6.2f -> rhs[%lld] = %-24.17g expected -(2/3)*mu*1*V = %.17g\n", m,
                  static_cast<long long>(probe), rhs[probe],
                  -(2.0 / 3.0) * m * mesh.cells()[probe].volume());
    }
  }

  // --- D. the WIRED path, and the default ---------------------------------
  std::printf("\n### D. through assembleRelaxedCompressibleMomentumComponent (the wired path)\n");
  {
    const VectorField velocity = quadraticX(mesh);
    const ScalarField pressure(n, 1000.0);
    const ScalarField density(n, 1.3);
    const ScalarField previous(n, 0.0);
    const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    const auto pbc = zeroGradientScalar(mesh);

    const auto assemble = [&](bool on) {
      return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
          mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc,
          VelocityComponent::U, previous, 1.0, 1.0, {},
          cfd::discretization::GradientScheme::GreenGauss, nullptr, on);
    };
    const MomentumAssembly off = assemble(false);
    const MomentumAssembly on = assemble(true);

    // the default, with the argument OMITTED
    const MomentumAssembly def = cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
        mesh, velocity, pressure, massFlux, density, density, mu, vbc, pbc,
        VelocityComponent::U, previous, 1.0, 1.0, {},
        cfd::discretization::GradientScheme::GreenGauss, nullptr);

    cfd::algebra::Vector standalone(n, 0.0);
    cfd::compressible::assembleDilatationalStressContribution(mesh, velocity, vbc, mu,
                                                              VelocityComponent::U, standalone);

    Index differing = 0;
    Real worstMismatch = 0.0;
    Index defEqualsOff = 0;
    for (Index i = 0; i < n; ++i) {
      if (on.system.rhs()[i] != off.system.rhs()[i]) ++differing;
      worstMismatch = std::max(worstMismatch,
                               std::abs((on.system.rhs()[i] - off.system.rhs()[i]) - standalone[i]));
      if (def.system.rhs()[i] == off.system.rhs()[i]) ++defEqualsOff;
    }
    std::printf("  cells where on != off                        %lld of %lld\n",
                static_cast<long long>(differing), static_cast<long long>(n));
    std::printf("  worst |(on - off) - standalone operator|      %.17g\n", worstMismatch);
    std::printf("    => the wiring is not merely present, it delivers exactly the operator\n");
    std::printf("  cells where DEFAULT == off (bitwise)         %lld of %lld  (n = default off)\n",
                static_cast<long long>(defEqualsOff), static_cast<long long>(n));

    const VectorField sol = solenoidal(mesh);
    const auto assembleSol = [&](bool onFlag) {
      return cfd::compressible::assembleRelaxedCompressibleMomentumComponent(
          mesh, sol, pressure, massFlux, density, density, mu, vbc, pbc, VelocityComponent::U,
          previous, 1.0, 1.0, {}, cfd::discretization::GradientScheme::GreenGauss, nullptr,
          onFlag);
    };
    const MomentumAssembly sOff = assembleSol(false);
    const MomentumAssembly sOn = assembleSol(true);
    Index solDiff = 0;
    for (Index i = 0; i < n; ++i)
      if (sOn.system.rhs()[i] != sOff.system.rhs()[i]) ++solDiff;
    std::printf("  divergence-free field: cells where on != off %lld  (0 = incompressible\n",
                static_cast<long long>(solDiff));
    std::printf("                                                  limit is undisturbed)\n");
  }

  // --- E. the boundary limitation, quantified -----------------------------
  std::printf("\n### E. the boundary limitation -- why the claim is deep-interior only\n");
  {
    const VectorField velocity = quadraticX(mesh);
    const ScalarField divU = cfd::discretization::divergence(mesh, velocity, vbc);
    const VectorField gradDivU = cfd::discretization::gradient(mesh, divU, sbc);
    Real worstAll = 0.0;
    Real worstOneIn = 0.0;
    Real worstDeep = 0.0;
    for (const auto& cell : mesh.cells()) {
      const Real x = cell.centroid().x;
      const Real y = cell.centroid().y;
      const Real e = std::abs(gradDivU[cell.id()].x - 1.0);
      worstAll = std::max(worstAll, e);
      if (x > h && x < lx - h && y > h && y < ly - h) worstOneIn = std::max(worstOneIn, e);
      if (x > 2.0 * h && x < lx - 2.0 * h && y > 2.0 * h && y < ly - 2.0 * h)
        worstDeep = std::max(worstDeep, e);
    }
    std::printf("  worst |grad(div U).x - 1|, ALL cells          %.17g\n", worstAll);
    std::printf("  worst, ONE cell in from the boundary          %.17g\n", worstOneIn);
    std::printf("  worst, TWO cells in (the deep interior)       %.17g\n", worstDeep);
    std::printf("    => the artificial zero-gradient BC on the derived scalar div U pollutes\n");
    std::printf("       one layer, and a Green-Gauss gradient of that spreads it one further.\n");
  }

  std::printf("\ndone\n");
  return 0;
}
