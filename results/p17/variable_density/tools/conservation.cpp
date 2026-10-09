// P17.1 conservation measurement. Reports NUMBERS, not PASS/FAIL, because the gate
// (acceptance_gate.md Group K) requires the measured imbalance next to its derived
// bound so the margin is visible rather than asserted.
//
// The bound is derived, not chosen:
//     bound = kAccum * eps * N_f * max|mDot_f|
// eps is the double epsilon; N_f is the case's own face count, so the bound scales with
// the mesh; max|mDot_f| is measured from the case, so it scales with the flux magnitude.
// kAccum = 8 is EMPIRICAL and labelled as such -- a safety factor on the summation
// order, not a fitted value.
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::calculateCompressibleMassFlux;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;
constexpr Real kU = 2.0;
constexpr Real kAccum = 8.0;  // EMPIRICAL, see the header comment
const Real kEps = std::numeric_limits<Real>::epsilon();

BoundaryConditionSet velocityBoundaries(const Mesh& mesh, Real u) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(Vector2{u, 0.0}));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const char* p : {"left", "right", "bottom", "top"})
    b.set(mesh, p, std::make_unique<FixedGradient>(0.0));
  return b;
}

struct Report {
  Real maxCellImbalance;
  Real totalAbsoluteImbalance;
  Real globalNetFlux;
  Real maxAbsFlux;
  Real bound;
  Real mdotIn;
  Real mdotOut;
};

Report measure(const Mesh& mesh, const ScalarField& density, const ScalarField& temperature,
               Real referencePressure) {
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{kU, 0.0});
  const ScalarField pGauge(n, 0.0);
  const auto vb = velocityBoundaries(mesh, kU);
  const auto pb = zeroGradientPressure(mesh);
  const ThermodynamicProperties thermo(kR, kCp);

  const SurfaceField flux = calculateCompressibleMassFlux(
      mesh, velocity, density, vb, pGauge, pb, referencePressure, thermo, temperature, nullptr);
  const cfd::physics::ContinuityResult c = cfd::physics::evaluateContinuity(mesh, flux);

  Report r{};
  r.maxCellImbalance = c.maxCellImbalance;
  r.totalAbsoluteImbalance = c.totalAbsoluteImbalance;
  r.globalNetFlux = c.globalNetFlux;
  r.maxAbsFlux = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f)
    r.maxAbsFlux = std::max(r.maxAbsFlux, std::abs(flux[f]));
  r.bound = kAccum * kEps * static_cast<Real>(mesh.numberOfFaces()) * r.maxAbsFlux;

  // Inlet / outlet split: boundary faces with an x-normal. Inflow is negative under the
  // outward-normal convention, outflow positive.
  r.mdotIn = 0.0;
  r.mdotOut = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    if (face.areaVector().x == 0.0) continue;
    if (flux[f] < 0.0) {
      r.mdotIn += flux[f];
    } else {
      r.mdotOut += flux[f];
    }
  }
  return r;
}

void print(const char* label, const Report& r) {
  std::printf("  %s\n", label);
  std::printf("    max cell imbalance        %.17g\n", r.maxCellImbalance);
  std::printf("    total |imbalance|         %.17g\n", r.totalAbsoluteImbalance);
  std::printf("    global net boundary flux  %.17g\n", r.globalNetFlux);
  std::printf("    mdot_in                   %.17g\n", r.mdotIn);
  std::printf("    mdot_out                  %.17g\n", r.mdotOut);
  std::printf("    mdot_in + mdot_out        %.17g\n", r.mdotIn + r.mdotOut);
  std::printf("    max |mdot_f|              %.17g\n", r.maxAbsFlux);
  std::printf("    derived bound             %.17g   (8 * eps * %s faces * max|mdot|)\n", r.bound,
              "N_f");
  std::printf("    verdict                   %s\n",
              (std::abs(r.globalNetFlux) <= r.bound && r.totalAbsoluteImbalance <= r.bound)
                  ? "WITHIN BOUND"
                  : "*** EXCEEDS BOUND ***");
}

}  // namespace

int main() {
  std::printf("P17.1 conservation measurement\n");
  std::printf("eps = %.17g   kAccum = %.17g (EMPIRICAL)\n\n", kEps, kAccum);

  const Index nx = 3;
  const Index ny = 2;
  const Real lx = 3.0;
  const Real ly = 2.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
  const ThermodynamicProperties thermo(kR, kCp);
  const Real tBottom = 300.0;
  const Real tTop = 600.0;
  const Real pRef = 2.0 * kR * tBottom;  // so rho_bottom == 2 exactly
  const Real rhoBottom = thermo.density(pRef, tBottom);
  const Real rhoTop = thermo.density(pRef, tTop);

  std::printf("mesh %lldx%lld, %lld cells, %lld faces\n", static_cast<long long>(nx),
              static_cast<long long>(ny), static_cast<long long>(mesh.numberOfCells()),
              static_cast<long long>(mesh.numberOfFaces()));
  std::printf("rho_bottom = %.17g   rho_top = %.17g   ratio = %.17g\n\n", rhoBottom, rhoTop,
              rhoBottom / rhoTop);

  // ---- (a) VARIABLE density, row-varying -------------------------------------
  ScalarField dVar(mesh.numberOfCells());
  ScalarField tVar(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const bool top = cell.centroid().y > 0.5 * ly;
    tVar[cell.id()] = top ? tTop : tBottom;
    dVar[cell.id()] = top ? rhoTop : rhoBottom;
  }
  std::printf("### (a) VARIABLE density -- rho varies by row, ratio exactly 2:1\n");
  const Report varReport = measure(mesh, dVar, tVar, pRef);
  print("row-varying rho", varReport);

  // ---- (b) CONSTANT density, same everything else ----------------------------
  ScalarField dConst(mesh.numberOfCells(), rhoBottom);
  ScalarField tConst(mesh.numberOfCells(), tBottom);
  std::printf("\n### (b) CONSTANT density -- the comparison case\n");
  const Report constReport = measure(mesh, dConst, tConst, pRef);
  print("uniform rho", constReport);

  // ---- (c) the DISCRIMINATOR, quantified ------------------------------------
  // Conservation alone cannot tell (a) from a run whose face density was collapsed to
  // one constant. The per-row flux magnitude can. Report both rows' flux so the
  // difference is a number rather than a claim.
  std::printf("\n### (c) the discriminator -- per-row flux magnitude\n");
  {
    const Index n = mesh.numberOfCells();
    const VectorField velocity(n, Vector2{kU, 0.0});
    const ScalarField pGauge(n, 0.0);
    const auto vb = velocityBoundaries(mesh, kU);
    const auto pb = zeroGradientPressure(mesh);
    const SurfaceField flux = calculateCompressibleMassFlux(mesh, velocity, dVar, vb, pGauge, pb,
                                                            pRef, thermo, tVar, nullptr);
    Real bottomFlux = 0.0;
    Real topFlux = 0.0;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const auto& face = mesh.face(f);
      if (face.areaVector().x == 0.0) continue;
      const bool top = mesh.cell(face.owner()).centroid().y > 0.5 * ly;
      if (top) {
        topFlux = std::abs(flux[f]);
      } else {
        bottomFlux = std::abs(flux[f]);
      }
    }
    std::printf("    |mdot| on a bottom-row x-face   %.17g\n", bottomFlux);
    std::printf("    |mdot| on a top-row x-face      %.17g\n", topFlux);
    std::printf("    ratio                           %.17g   (expect exactly 2)\n",
                bottomFlux / topFlux);
    std::printf("    a collapsed constant face density would give ratio 1 -- see control N4/N5\n");
  }

  std::printf("\ndone\n");
  return 0;
}
