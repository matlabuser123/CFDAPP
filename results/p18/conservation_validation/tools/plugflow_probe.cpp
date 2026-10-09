// P18.7 -- INSTRUMENTATION. Diagnosing why the plug-flow coupled loop diverges.
//
// The driver produced Y_A = -8.6e9. The system with ZERO sources is well posed -- it converges to
// Y = 1 exactly, with row sums 2.0 and diagonal 2.0 -- so the blow-up comes from the COUPLING.
// This probe runs the actual loop and prints what each solve produced, so the first divergent
// step is identified rather than guessed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/SpeciesTransport.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using namespace cfd;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::MeshGeometry;
using cfd::species::MassFractionState;
using cfd::species::SpeciesRegistry;

int main() {
  const Index nx = 10;
  const auto mesh = MeshGeometry::createCartesian2D(nx, 1, 2.0, 1.0);
  const Index cells = mesh.numberOfCells();
  const Real rho = 1.0;
  const Real u = 2.0;
  const Real cpValue = 1000.0;
  const Real hfB = -2000000.0;
  const Real mw = 0.0625;
  const Real arrA = 5.0;
  const Real arrEa = 3.0e4;
  const Real ru = 8.31446261815324;
  const Real cellVolume = (2.0 / static_cast<Real>(nx)) * 1.0;

  std::printf("=== P18.7 plug-flow coupled-loop diagnosis, %lld cells ===\n",
              static_cast<long long>(cells));
  std::printf("cell volume = %.6e\n", cellVolume);

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    massFlux[f] = rho * u * mesh.face(f).areaVector().x;
  }

  auto bcs = std::make_unique<BoundaryConditionSet>();
  auto tbcs = std::make_unique<BoundaryConditionSet>();
  for (const std::string& patch : {"left", "right", "bottom", "top"}) {
    if (patch == std::string("left")) {
      bcs->set(mesh, patch, std::make_unique<cfd::boundary::FixedValue>(1.0));
      tbcs->set(mesh, patch, std::make_unique<cfd::boundary::FixedValue>(1000.0));
    } else {
      bcs->set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
      tbcs->set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }

  const SpeciesRegistry registry({"A", "B"});
  const auto diffusivities =
      cfd::species::makeSpeciesDiffusivities(ScalarField(cells, rho), {1.0, 1.0});

  MassFractionState st(registry, cells, {1.0, 0.0});
  ScalarField temp(cells, 1000.0);

  cfd::species::SpeciesTransportOptions opt;
  opt.convection = true;
  opt.diffusion = false;
  opt.closureCorrection = false;

  algebra::LinearSolverSettings ls;
  ls.absoluteTolerance = 1.0e-15;
  ls.relativeTolerance = 1.0e-15;
  ls.maxIterations = 4000;
  const algebra::BiCGSTAB sv(ls);

  for (int outer = 1; outer <= 6; ++outer) {
    ScalarField su(cells, 0.0);
    ScalarField heat(cells, 0.0);
    for (Index c = 0; c < cells; ++c) {
      const Real k = arrA * std::exp(-arrEa / (ru * temp[c]));
      const Real q = k * rho * st.massFraction(0, c) / mw;
      su[c] = mw * (-1.0) * q;
      heat[c] = -q * (mw * hfB);
    }
    cfd::transport::ScalarSourceFields src;
    src.Su = su;
    src.Sp = ScalarField(cells, 0.0);

    const auto a1 = cfd::species::speciesTransportAssembly(
        mesh, 0, st, massFlux, diffusivities, *bcs, src, cfd::species::SpeciesTimeState{}, nullptr,
        opt);
    algebra::Vector g(cells, 0.0);
    for (Index c = 0; c < cells; ++c) {
      g[c] = st.massFraction(0, c);
    }
    const auto r1 = sv.solve(a1.system, g);

    Real ymin = 1.0e300;
    Real ymax = -1.0e300;
    for (Index c = 0; c < cells; ++c) {
      ymin = std::min(ymin, r1.solution[c]);
      ymax = std::max(ymax, r1.solution[c]);
    }
    std::printf("  it %d species: status=%d iters=%lld  Y[%.4e, %.4e]  diag0=%.4e rhs0=%.4e"
                " Su0V=%.4e\n",
                outer, static_cast<int>(r1.status), static_cast<long long>(r1.iterations), ymin,
                ymax, a1.diagonal[0], a1.system.rhs()[0], su[0] * cellVolume);
    if (ymin < -1.0 || ymax > 2.0) {
      std::printf("    *** SPECIES DIVERGED. First 4 rows: ***\n");
      for (Index c = 0; c < std::min<Index>(4, cells); ++c) {
        std::printf("      cell %lld diag=%.6e rhs=%.6e Su=%.6e SuV=%.6e sol=%.6e\n",
                    static_cast<long long>(c), a1.diagonal[c], a1.system.rhs()[c], su[c],
                    su[c] * cellVolume, r1.solution[c]);
      }
      break;
    }
    for (Index c = 0; c < cells; ++c) {
      const Real ya = r1.solution[c];
      st.setCellComposition(c, {ya, 1.0 - ya});
    }

    ScalarField cpf(cells, cpValue);
    algebra::SparseMatrixBuilder bld(cells, cells);
    algebra::Vector erhs(cells, 0.0);
    cfd::thermal::assembleThermalConvectionContribution(mesh, cpf, massFlux, temp, *tbcs, bld,
                                                        erhs);
    cfd::thermal::assembleThermalSourceContribution(mesh, heat, erhs);
    algebra::LinearSystem esys{bld.build(), erhs};
    algebra::Vector tg(cells, 0.0);
    for (Index c = 0; c < cells; ++c) {
      tg[c] = temp[c];
    }
    const auto r2 = sv.solve(esys, tg);
    Real tmin = 1.0e300;
    Real tmax = -1.0e300;
    for (Index c = 0; c < cells; ++c) {
      tmin = std::min(tmin, r2.solution[c]);
      tmax = std::max(tmax, r2.solution[c]);
    }
    std::printf("  it %d energy : status=%d iters=%lld  T[%.4e, %.4e]  Q0V=%.4e\n", outer,
                static_cast<int>(r2.status), static_cast<long long>(r2.iterations), tmin, tmax,
                heat[0] * cellVolume);
    if (tmin < 0.0 || tmax > 1.0e5) {
      std::printf("    *** ENERGY DIVERGED -- this is the root cause ***\n");
      break;
    }
    for (Index c = 0; c < cells; ++c) {
      temp[c] = r2.solution[c];
    }
  }
  return 0;
}
