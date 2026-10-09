// P17.1 dry-run: measure the UNCHANGED tree's variable-density behaviour before any
// edit, so every gate criterion is checked against a measurement rather than against
// memory.
//
// Two jobs:
//   1. Record reference values with %.17g, so "bit-for-bit unchanged" is a string
//      comparison afterwards rather than a tolerance judgement.
//   2. PROVE the design.md section 3 gap: that a non-positive or non-finite CELL
//      density currently flows silently into the face density and the mass flux
//      instead of being rejected. If any of those cases already throws, the gap is
//      not real and the gate must be redesigned rather than frozen.
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::calculateCompressibleMassFlux;
using cfd::compressible::evaluateCompressibleFaceDensity;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;
constexpr Real kT = 300.0;
constexpr Real kRho = 2.0;
constexpr Real kPref = kRho * kR * kT;  // p = rho R T, so EOS(kPref, kT) == kRho exactly

BoundaryConditionSet velocityBoundaries(const Mesh& mesh, Vector2 inlet) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(inlet));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const char* patch : {"left", "right", "bottom", "top"})
    b.set(mesh, patch, std::make_unique<FixedGradient>(0.0));
  return b;
}

// Runs calculateCompressibleMassFlux with the given cell-density field and reports
// whether it threw, and if not, what the fluxes are.
void probe(const char* label, const Mesh& mesh, const ScalarField& density) {
  const Index n = mesh.numberOfCells();
  VectorField velocity(n);
  for (Index i = 0; i < n; ++i) velocity[i] = Vector2{1.0, 0.0};
  ScalarField pGauge(n);
  for (Index i = 0; i < n; ++i) pGauge[i] = 0.0;
  ScalarField temperature(n);
  for (Index i = 0; i < n; ++i) temperature[i] = kT;

  const auto thermo = ThermodynamicProperties(kR, kCp);
  const auto vb = velocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pb = zeroGradientPressure(mesh);

  std::printf("  %-34s ", label);
  try {
    const SurfaceField flux = calculateCompressibleMassFlux(mesh, velocity, density, vb, pGauge,
                                                            pb, kPref, thermo, temperature,
                                                            nullptr);
    const SurfaceField fd = evaluateCompressibleFaceDensity(mesh, density, pGauge, pb, kPref,
                                                            thermo, temperature, nullptr);
    // Report the internal face (the one fed by interpolateInternalFace) plus a
    // whole-field finiteness/positivity summary.
    Index badFaceDensity = 0;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f)
      if (!std::isfinite(fd[f]) || !(fd[f] > 0.0)) ++badFaceDensity;
    Index badFlux = 0;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f)
      if (!std::isfinite(flux[f])) ++badFlux;

    std::printf("RETURNED   ");
    // find the internal face and print it
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      if (!mesh.face(f).isBoundary()) {
        std::printf("internal rho_f=%-24.17g mdot_f=%-24.17g ", fd[f], flux[f]);
        break;
      }
    }
    std::printf("non-positive/non-finite face rho: %lld   non-finite mdot: %lld\n",
                static_cast<long long>(badFaceDensity), static_cast<long long>(badFlux));
  } catch (const InvalidArgumentError& e) {
    std::printf("THREW InvalidArgumentError: %s\n", e.what());
  } catch (const std::exception& e) {
    std::printf("THREW %s\n", e.what());
  }
}

}  // namespace

int main() {
  std::printf("P17.1 dry-run -- variable density on the UNCHANGED tree\n\n");

  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const Index n = mesh.numberOfCells();
  std::printf("mesh: createCartesian2D(2,1,2.0,1.0)  cells=%lld faces=%lld\n\n",
              static_cast<long long>(n), static_cast<long long>(mesh.numberOfFaces()));

  // ---------------------------------------------------------------- group A
  std::printf("### A. EOS reference values, %%.17g (for bit-for-bit comparison later)\n");
  const auto thermo = ThermodynamicProperties(kR, kCp);
  std::printf("  R=%.17g  cp=%.17g  cv=%.17g  gamma=%.17g\n", thermo.gasConstant(),
              thermo.specificHeatPressure(), thermo.specificHeatVolume(),
              thermo.specificHeatRatio());
  struct PT {
    Real p, T;
    const char* label;
  };
  const PT states[] = {{kPref, kT, "reference"},
                       {2.0 * kPref, kT, "2p, same T"},
                       {kPref, 2.0 * kT, "same p, 2T"},
                       {101325.0, 288.15, "sea-level standard"}};
  for (const auto& s : states)
    std::printf("  %-22s p=%-20.17g T=%-20.17g rho=%.17g\n", s.label, s.p, s.T,
                thermo.density(s.p, s.T));

  // ---------------------------------------------------------------- group B
  std::printf("\n### B. p/T coupling signs -- dRho/dp > 0 and dRho/dT < 0 must hold\n");
  const Real dRdp = thermo.equationOfState().dDensityDPressure(kPref, kT);
  const Real dRdT = thermo.equationOfState().dDensityDTemperature(kPref, kT);
  std::printf("  dRho/dp = %.17g   sign %s\n", dRdp, dRdp > 0.0 ? "POSITIVE (expected)" : "*** WRONG ***");
  std::printf("  dRho/dT = %.17g   sign %s\n", dRdT, dRdT < 0.0 ? "NEGATIVE (expected)" : "*** WRONG ***");
  std::printf("  rho(2p,T)/rho(p,T) = %.17g   (expect exactly 2)\n",
              thermo.density(2.0 * kPref, kT) / thermo.density(kPref, kT));
  std::printf("  rho(p,2T)/rho(p,T) = %.17g   (expect exactly 0.5)\n",
              thermo.density(kPref, 2.0 * kT) / thermo.density(kPref, kT));

  // ---------------------------------------------------------------- group C
  std::printf("\n### C. spatially varying density -- the internal face must see the MEAN\n");
  {
    ScalarField d(n);
    d[0] = 1.0;
    d[1] = 3.0;  // deliberately nonuniform; arithmetic mean is 2.0
    probe("rho = {1.0, 3.0} nonuniform", mesh, d);
    std::printf("  expected internal rho_f = 2 (arithmetic mean on a uniform mesh)\n");
  }

  // ---------------------------------------------------------------- group D
  std::printf("\n### D. valid uniform density -- the reference the change must not move\n");
  {
    ScalarField d(n);
    for (Index i = 0; i < n; ++i) d[i] = kRho;
    probe("rho = 2.0 uniform (VALID)", mesh, d);
  }

  // ---------------------------------------------------------------- group E
  std::printf("\n### E. THE GAP: invalid CELL density values. Each SHOULD be rejected.\n");
  std::printf("###    'RETURNED' below means the gap is REAL and the gate is non-vacuous.\n");
  const Real qnan = std::numeric_limits<Real>::quiet_NaN();
  const Real pinf = std::numeric_limits<Real>::infinity();
  struct Bad {
    Real value;
    const char* label;
  };
  const Bad bad[] = {{-2.0, "rho[0] = -2.0  (negative)"},
                     {0.0, "rho[0] =  0.0  (zero)"},
                     {qnan, "rho[0] =  NaN"},
                     {pinf, "rho[0] = +Inf"},
                     {-pinf, "rho[0] = -Inf"}};
  for (const auto& b : bad) {
    ScalarField d(n);
    for (Index i = 0; i < n; ++i) d[i] = kRho;
    d[0] = b.value;
    probe(b.label, mesh, d);
  }

  // ---------------------------------------------------------------- group F
  std::printf("\n### F. constant-density limit: compressible uniform rho vs incompressible\n");
  {
    ScalarField d(n);
    for (Index i = 0; i < n; ++i) d[i] = kRho;
    VectorField velocity(n);
    for (Index i = 0; i < n; ++i) velocity[i] = Vector2{1.0, 0.0};
    ScalarField pGauge(n);
    ScalarField temperature(n);
    for (Index i = 0; i < n; ++i) {
      pGauge[i] = 0.0;
      temperature[i] = kT;
    }
    const auto vb = velocityBoundaries(mesh, Vector2{1.0, 0.0});
    const auto pb = zeroGradientPressure(mesh);
    const SurfaceField comp = calculateCompressibleMassFlux(mesh, velocity, d, vb, pGauge, pb,
                                                            kPref, thermo, temperature, nullptr);
    const cfd::physics::FluidProperties fluid(kRho, 1.0);
    const SurfaceField inc = cfd::physics::calculateMassFlux(mesh, velocity, fluid, vb);
    Index differing = 0;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f)
      if (comp[f] != inc[f]) ++differing;
    std::printf("  faces where compressible != incompressible: %lld  (expect 0, BITWISE)\n",
                static_cast<long long>(differing));
  }

  std::printf("\ndone\n");
  return 0;
}
