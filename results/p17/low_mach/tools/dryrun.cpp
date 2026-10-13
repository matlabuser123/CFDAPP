// P17.6 DRY-RUN on the UNCHANGED tree: run the Mach family BEFORE freezing the gate, so every
// threshold is derived from a measurement rather than guessed.
//
// WHY THIS EXISTS. The audit found that the existing low-Mach test never solves the compressible
// system -- it evaluates the EOS on an INCOMPRESSIBLE solution, so "the compressible solution
// approaches the incompressible one" is true by construction there. This probe does the thing that
// was missing: for each of several decreasing inlet Mach numbers, solve BOTH systems on the same
// mesh, viscosity and boundary conditions, and measure how far apart they are.
//
// THE CASE. The channel of the existing test: 8.0 x 1.0, mu = 0.1, T = 300 K isothermal,
// p_ref = 101325 Pa, R = 287.05, cp = 1005. Inlet Inlet(U0, 0), outlet Outlet + p = 0, walls.
//
// ONE DELIBERATE CHANGE FROM THE EXISTING TEST, and it matters. That test gives the incompressible
// solver kDensity = 1.0 while the EOS at the reference state gives 1.176624 -- a 15% mismatch. It
// compensates when comparing fluxes (it scales the incompressible flux by rhoAvg), so its own
// numbers are sound. But for a SOLUTION comparison the two formulations must describe the SAME gas,
// so here the incompressible reference density is DERIVED: rho_ref = p_ref / (R T).
//
// EXPECTED TREND, and why it is linear rather than quadratic. For this channel the pressure drop is
// viscous-dominated, dp ~ 12 mu U L / H^2, not dynamic (rho U^2). So
//     Delta rho / rho = Delta p / p ~ (12 mu L / (H^2 p)) * U  ~  Ma^1
// A quadratic expectation would be wrong here. The observed exponent is MEASURED and reported; no
// exponent is assumed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/compressible/TotalEnergyEquation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::compressible::CompressibleSIMPLE;
using cfd::compressible::CompressibleSIMPLEResult;
using cfd::compressible::CompressibleSIMPLESettings;
using cfd::compressible::CompressibleSIMPLEStatus;
using cfd::compressible::machNumber;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kGasConstant = 287.05;
constexpr Real kSpecificHeatPressure = 1005.0;
constexpr Real kTemperature = 300.0;
constexpr Real kReferencePressure = 101325.0;

BoundaryConditionSet velocityBoundaries(const Mesh& mesh, Real u0) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<Inlet>(Vector2{u0, 0.0}));
  b.set(mesh, "right", std::make_unique<Outlet>());
  b.set(mesh, "bottom", std::make_unique<Wall>());
  b.set(mesh, "top", std::make_unique<Wall>());
  return b;
}

BoundaryConditionSet pressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  b.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  b.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  b.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return b;
}

// Inherited VERBATIM from the existing low-Mach test, which itself inherited them from the
// Poiseuille validation. Not re-tuned by this phase.
// SCALED BY THE FLOW SPEED, and this is a policy correction rather than a relaxation. The inherited
// tolerances are ABSOLUTE and were locked for U_mean = 1. Across a family spanning 16x in velocity an
// absolute tolerance is NOT the same convergence policy -- it silently demands 16x more relative
// precision at the top end, and the dry-run measured exactly that: the INCOMPRESSIBLE reference hit
// maxIterations at U0 = 2, 4, 8 and 16 while the compressible solve was never reached. Multiplying
// the three field tolerances by U0 holds the RELATIVE standard fixed, and at U0 = 1 reproduces the
// inherited values bit for bit. Every quantity that scales like the flow does: velocity ~ U,
// pressure ~ 12 mu U L / H^2 ~ U, continuity ~ mdot ~ U.
SIMPLESettings incompressibleSettings(Real u0) {
  SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5 * u0;
  s.pressureTolerance = 5e-4 * u0;
  s.continuityTolerance = 1e-6 * u0;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-8;
  s.pressureSolver.relativeTolerance = 1e-6;
  return s;
}

// The same tolerances and relaxation, so the two solvers are compared under the same convergence
// policy rather than one being given an easier target.
CompressibleSIMPLESettings compressibleSettings(Real u0) {
  CompressibleSIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5 * u0;
  s.pressureTolerance = 5e-4 * u0;
  s.continuityTolerance = 1e-6 * u0;
  return s;
}

struct Level {
  Real u0{};
  bool ok{false};
  // Mach
  Real inletMach{}, machMaxComp{}, machMaxInc{};
  // density
  Real rhoMin{}, rhoMax{}, rhoMean{}, deltaRhoOverRho{}, maxAbsRhoMinusRef{}, maxEosError{};
  // velocity differences
  Real uLinf{}, uL2{}, uRelL2{}, vLinf{}, magLinf{}, magRelL2{};
  // pressure
  Real pLinf{}, pRelL2{}, dropComp{}, dropInc{}, dropRelDiff{};
  // mass flux
  Real fluxMaxRelDiff{}, inletFlowComp{}, outletFlowComp{}, inletFlowInc{};
  Real massImbalanceComp{}, massImbalanceInc{};
  // residuals / iterations
  Index itComp{}, itInc{};
  Real resUComp{}, resPComp{}, resCComp{}, resUInc{}, resPInc{}, resCInc{};
  // energy (P17.5 on the converged compressible fields)
  Real eInternal{}, eKinetic{}, keOverE{}, energyImbalance{}, energyBound{}, tRecoveryMaxErr{};
  Real energyPredicted{};
  Real kineticComp{}, kineticInc{}, kineticRelDiff{};
  Real energyNoiseFloor{}, energySignalToNoise{};
  // momentum budget
  Real momFluxNet{}, momPressureForce{}, momViscousImplied{}, momViscousAnalytic{};
};

Real l2(const std::vector<Real>& d, const Mesh& mesh) {
  Real num = 0.0, vol = 0.0;
  for (const auto& cell : mesh.cells()) {
    num += d[cell.id()] * d[cell.id()] * cell.volume();
    vol += cell.volume();
  }
  return std::sqrt(num / vol);
}

Level runLevel(Real u0, Index nx, Index ny) {
  Level L;
  L.u0 = u0;
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, kLength, kHeight);
  const Index n = mesh.numberOfCells();
  const auto vbc = velocityBoundaries(mesh, u0);
  const auto pbc = pressureBoundaries(mesh);
  const ThermodynamicProperties thermo(kGasConstant, kSpecificHeatPressure);

  // DERIVED, not chosen: the two formulations must describe the same gas.
  const Real rhoRef = thermo.density(kReferencePressure, kTemperature);
  const Real soundSpeed = thermo.speedOfSound(kTemperature);
  L.inletMach = machNumber(u0, soundSpeed);

  // ---- the incompressible reference -------------------------------------
  const SIMPLE simple(incompressibleSettings(u0));
  const SIMPLEResult inc =
      simple.solve(mesh, FluidProperties(rhoRef, kViscosity), vbc, pbc,
                   VectorField(n, Vector2{u0, 0.0}), ScalarField(n, 0.0));
  if (inc.status != SIMPLEStatus::Converged) {
    std::printf("  U0 = %.4g : INCOMPRESSIBLE reference did NOT converge (status %d)\n", u0,
                static_cast<int>(inc.status));
    return L;
  }

  // ---- the compressible solve -------------------------------------------
  const CompressibleSIMPLE comp(compressibleSettings(u0), thermo, kReferencePressure,
                                /*referenceCell=*/0);
  const CompressibleSIMPLEResult cmp =
      comp.solve(mesh, kViscosity, vbc, pbc, ScalarField(n, kTemperature), nullptr,
                 VectorField(n, Vector2{u0, 0.0}), ScalarField(n, 0.0), ScalarField(n, rhoRef));
  if (cmp.status != CompressibleSIMPLEStatus::Converged) {
    std::printf("  U0 = %.4g : COMPRESSIBLE solve did NOT converge (status %d, %llu iterations)\n",
                u0, static_cast<int>(cmp.status), static_cast<unsigned long long>(cmp.iterations));
    return L;
  }
  L.ok = true;

  // ---- density -----------------------------------------------------------
  L.rhoMin = cmp.density[0];
  L.rhoMax = cmp.density[0];
  Real sum = 0.0;
  for (Index i = 0; i < n; ++i) {
    L.rhoMin = std::min(L.rhoMin, cmp.density[i]);
    L.rhoMax = std::max(L.rhoMax, cmp.density[i]);
    sum += cmp.density[i];
    const Real pAbs = kReferencePressure + cmp.pressure[i];
    L.maxEosError = std::max(L.maxEosError, std::abs(cmp.density[i] - pAbs / (kGasConstant * kTemperature)));
    L.maxAbsRhoMinusRef = std::max(L.maxAbsRhoMinusRef, std::abs(cmp.density[i] - rhoRef));
  }
  L.rhoMean = sum / static_cast<Real>(n);
  L.deltaRhoOverRho = (L.rhoMax - L.rhoMin) / L.rhoMean;

  // ---- Mach --------------------------------------------------------------
  for (Index i = 0; i < n; ++i) {
    const Real sc = std::sqrt(cmp.velocity[i].x * cmp.velocity[i].x +
                              cmp.velocity[i].y * cmp.velocity[i].y);
    const Real si = std::sqrt(inc.velocity[i].x * inc.velocity[i].x +
                              inc.velocity[i].y * inc.velocity[i].y);
    L.machMaxComp = std::max(L.machMaxComp, machNumber(sc, soundSpeed));
    L.machMaxInc = std::max(L.machMaxInc, machNumber(si, soundSpeed));
  }

  // ---- velocity differences ---------------------------------------------
  std::vector<Real> du(n), dv(n), dmag(n), magInc(n);
  for (Index i = 0; i < n; ++i) {
    du[i] = cmp.velocity[i].x - inc.velocity[i].x;
    dv[i] = cmp.velocity[i].y - inc.velocity[i].y;
    const Real mc = std::sqrt(cmp.velocity[i].x * cmp.velocity[i].x +
                              cmp.velocity[i].y * cmp.velocity[i].y);
    const Real mi = std::sqrt(inc.velocity[i].x * inc.velocity[i].x +
                             inc.velocity[i].y * inc.velocity[i].y);
    dmag[i] = mc - mi;
    magInc[i] = mi;
    L.uLinf = std::max(L.uLinf, std::abs(du[i]));
    L.vLinf = std::max(L.vLinf, std::abs(dv[i]));
    L.magLinf = std::max(L.magLinf, std::abs(dmag[i]));
  }
  L.uL2 = l2(du, mesh);
  const Real magL2 = l2(magInc, mesh);
  L.uRelL2 = L.uL2 / std::max(magL2, 1e-30);
  L.magRelL2 = l2(dmag, mesh) / std::max(magL2, 1e-30);

  // ---- pressure ----------------------------------------------------------
  // Both solvers use the SAME gauge: Outlet + FixedValue(0.0) at "right". The comparison is
  // therefore made on the gauge field directly AND, as a gauge-independent cross-check, on the
  // field referenced to cell 0.
  std::vector<Real> dp(n);
  for (Index i = 0; i < n; ++i) {
    dp[i] = (cmp.pressure[i] - cmp.pressure[0]) - (inc.pressure[i] - inc.pressure[0]);
    L.pLinf = std::max(L.pLinf, std::abs(dp[i]));
  }
  std::vector<Real> pIncRef(n);
  for (Index i = 0; i < n; ++i) pIncRef[i] = inc.pressure[i] - inc.pressure[0];
  L.pRelL2 = l2(dp, mesh) / std::max(l2(pIncRef, mesh), 1e-30);

  // inlet-to-outlet drop, averaged over the patch owner cells
  const auto patchMeanPressure = [&](const ScalarField& p, const char* name) {
    Real acc = 0.0;
    Index cnt = 0;
    for (const Index f : mesh.boundaryPatch(name).faceIds()) {
      acc += p[mesh.face(f).owner()];
      ++cnt;
    }
    return (cnt == 0) ? 0.0 : acc / static_cast<Real>(cnt);
  };
  L.dropComp = patchMeanPressure(cmp.pressure, "left") - patchMeanPressure(cmp.pressure, "right");
  L.dropInc = patchMeanPressure(inc.pressure, "left") - patchMeanPressure(inc.pressure, "right");
  L.dropRelDiff = std::abs(L.dropComp - L.dropInc) / std::max(std::abs(L.dropInc), 1e-30);

  // ---- mass flux ---------------------------------------------------------
  // The incompressible flux is rho_ref * volumetric; the compressible one is the solver's own,
  // which is the P17.3 authoritative field.
  const Real fluxScale = std::max<Real>(1e-9, rhoRef * u0 * kHeight);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    L.fluxMaxRelDiff =
        std::max(L.fluxMaxRelDiff, std::abs(cmp.massFlux[f] - inc.massFlux[f]) / fluxScale);
  }
  const auto patchFlow = [&](const SurfaceField& mf, const char* name) {
    Real acc = 0.0;
    for (const Index f : mesh.boundaryPatch(name).faceIds()) acc += mf[f];
    return acc;
  };
  L.inletFlowComp = patchFlow(cmp.massFlux, "left");
  L.outletFlowComp = patchFlow(cmp.massFlux, "right");
  L.inletFlowInc = patchFlow(inc.massFlux, "left");
  L.massImbalanceComp = cmp.globalMassImbalance;
  L.massImbalanceInc = inc.globalMassImbalance;

  // ---- residuals ---------------------------------------------------------
  L.itComp = cmp.iterations;
  L.itInc = inc.iterations;
  L.resUComp = cmp.finalUResidual;
  L.resPComp = cmp.finalPressureResidual;
  L.resCComp = cmp.finalContinuityResidual;
  L.resUInc = inc.finalUResidual;
  L.resPInc = inc.finalPressureResidual;
  L.resCInc = inc.finalContinuityResidual;

  // ---- energy: P17.5 evaluated on the CONVERGED compressible fields ------
  {
    const TemperatureDependentProperty cp(std::vector<Real>{kSpecificHeatPressure}, 0.0, 3000.0,
                                          "cp");
    const TotalEnergyState state(thermo, SensibleEnthalpy(cp, 0.0, 0.0));
    const ScalarField temperature(n, kTemperature);
    L.eInternal = state.specificInternalEnergy(kTemperature);
    Real keMax = 0.0;
    for (Index i = 0; i < n; ++i) {
      keMax = std::max(keMax, TotalEnergyState::specificKineticEnergy(cmp.velocity[i]));
    }
    L.eKinetic = keMax;
    L.keOverE = keMax / L.eInternal;

    const ScalarField rhoE = cfd::compressible::conservedTotalEnergyField(
        mesh, cmp.density, temperature, cmp.velocity, state);
    const ScalarField hTotal =
        cfd::compressible::specificTotalEnthalpyField(mesh, temperature, cmp.velocity, state);

    // temperature recovery stability: rhoE/rho - ke -> T, against the known 300 K
    VectorField momentum(n);
    for (Index i = 0; i < n; ++i) {
      momentum[i] = Vector2{cmp.density[i] * cmp.velocity[i].x, cmp.density[i] * cmp.velocity[i].y};
    }
    const auto rec = cfd::compressible::recoverPrimitiveFields(mesh, cmp.density, momentum, rhoE, state);
    for (Index i = 0; i < n; ++i) {
      L.tRecoveryMaxErr = std::max(L.tRecoveryMaxErr, std::abs(rec.temperature[i] - kTemperature));
    }

    // the steady energy balance on the converged state: accumulation is zero, so the balance is
    // convective + conductive - sources. Isothermal, so conduction is zero by construction; the
    // boundary hTotal is the inlet's own state on an inflow face.
    SurfaceField hBoundary(mesh.numberOfFaces(), 0.0);
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) hBoundary[face.id()] = hTotal[face.owner()];
    }
    const ScalarField conv = cfd::compressible::totalEnergyConvectiveDivergence(
        mesh, cmp.massFlux, hTotal, hBoundary);
    const ScalarField zero(n, 0.0);
    const ScalarField accum = cfd::compressible::totalEnergyAccumulation(mesh, rhoE, rhoE, 1.0);
    const auto bal = cfd::compressible::evaluateTotalEnergyBalance(mesh, accum, conv, zero, nullptr,
                                                                  nullptr, nullptr);
    L.energyImbalance = bal.imbalance;
    L.energyBound = bal.bound;
    // The balance CANNOT close on an isothermal solve: T is an INPUT, so the isothermal constraint
    // is itself an unmodelled heat sink, and P17.4 supplies no stress tensor for the viscous-work
    // term. The imbalance is therefore PREDICTED independently as the mass-weighted kinetic-energy
    // gain between inlet and outlet, and the two are compared -- which turns an unclosable balance
    // into a real check rather than an unexplained number.
    Real keIn = 0.0, mIn = 0.0, keOut = 0.0, mOut = 0.0;
    for (const Index f : mesh.boundaryPatch("left").faceIds()) {
      const Real m = cmp.massFlux[f];
      keIn += m * TotalEnergyState::specificKineticEnergy(cmp.velocity[mesh.face(f).owner()]);
      mIn += m;
    }
    for (const Index f : mesh.boundaryPatch("right").faceIds()) {
      const Real m = cmp.massFlux[f];
      keOut += m * TotalEnergyState::specificKineticEnergy(cmp.velocity[mesh.face(f).owner()]);
      mOut += m;
    }
    L.energyPredicted = keIn + keOut;

    // G5's FROZEN energy clause names the trap this measures around: "at Ma = 1e-3 the kinetic term
    // is ~1e-6 of the internal energy, so a criterion on e_t can be satisfied while the kinetic part
    // is entirely wrong. The criterion must bound the KINETIC CONTRIBUTION SEPARATELY, relative to
    // its own magnitude."
    //
    // So the kinetic contribution is compared DIRECTLY against the incompressible reference's own,
    // normalised by itself -- never through rhoE, where h*mdot (~3e5) is nine orders above the
    // kinetic gain (~1e-4) and any mass-flux residual swamps it.
    Real keComp = 0.0, keInc = 0.0;
    for (const auto& cell : mesh.cells()) {
      const Index i = cell.id();
      keComp += cmp.density[i] * TotalEnergyState::specificKineticEnergy(cmp.velocity[i]) *
                cell.volume();
      keInc += rhoRef * TotalEnergyState::specificKineticEnergy(inc.velocity[i]) * cell.volume();
    }
    L.kineticComp = keComp;
    L.kineticInc = keInc;
    L.kineticRelDiff = std::abs(keComp - keInc) / std::max(std::abs(keInc), 1e-300);

    // The signal-to-noise floor of the BALANCE, which is why the balance cannot carry this
    // criterion: the enthalpy flux error implied by the residual mass imbalance, against the
    // physical kinetic gain the balance is supposed to reveal.
    const Real hFlux = state.specificStaticEnthalpy(kTemperature);
    L.energyNoiseFloor = std::abs(cmp.globalMassImbalance) * hFlux;
    L.energySignalToNoise =
        std::abs(L.energyPredicted) / std::max(L.energyNoiseFloor, 1e-300);
  }

  // ---- x-momentum budget on the converged compressible solution ---------
  {
    Real fluxNet = 0.0;
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) continue;
      fluxNet += cmp.massFlux[face.id()] * cmp.velocity[face.owner()].x;
    }
    L.momFluxNet = fluxNet;
    Real pForce = 0.0;
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) continue;
      pForce -= cmp.pressure[face.owner()] * face.areaVector().x;
    }
    L.momPressureForce = pForce;
    L.momViscousImplied = fluxNet - pForce;
    // Poiseuille: tau_w = 6 mu U_mean / H per wall, over length L, both walls, opposing the flow.
    L.momViscousAnalytic = -2.0 * (6.0 * kViscosity * u0 / kHeight) * kLength;
  }
  return L;
}

Real observedExponent(Real ma1, Real e1, Real ma2, Real e2) {
  if (!(ma1 > 0.0 && ma2 > 0.0 && e1 > 0.0 && e2 > 0.0)) return 0.0;
  return std::log(e1 / e2) / std::log(ma1 / ma2);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const ThermodynamicProperties thermo(kGasConstant, kSpecificHeatPressure);
  const Real rhoRef = thermo.density(kReferencePressure, kTemperature);
  const Real a = thermo.speedOfSound(kTemperature);

  std::printf("P17.6 dry-run -- the Mach family, UNCHANGED tree\n");
  std::printf("channel %.1f x %.1f   mu %.3g   T %.1f K   p_ref %.0f Pa\n", kLength, kHeight,
              kViscosity, kTemperature, kReferencePressure);
  std::printf("rho_ref = p_ref/(R T) = %.9g   (DERIVED, so both formulations are the same gas)\n",
              rhoRef);
  std::printf("a = sqrt(gamma R T) = %.6g m/s   gamma = %.10g\n\n", a,
              thermo.specificHeatRatio());

  const Index nx = 32;
  const Index ny = 6;
  std::printf("mesh %llux%llu, matching the existing low-Mach test's default\n\n",
              static_cast<unsigned long long>(nx), static_cast<unsigned long long>(ny));

  std::vector<Level> levels;
  // DESCENDING from the existing test's anchor (U0 = 1). U0 = 2 and 4 cannot converge inside the
  // inherited maxIterations = 3000 -- measured, and section 22 forbids raising an iteration limit to
  // obtain a pass. Descending is also the correct direction for a low-Mach LIMIT study.
  for (const Real u0 : {1.0, 0.5, 0.25, 0.125}) {
    const Level L = runLevel(u0, nx, ny);
    if (L.ok) levels.push_back(L);
  }

  if (levels.empty()) {
    std::printf("\n*** NO LEVEL CONVERGED -- nothing can be derived from this run ***\n");
    return 1;
  }

  std::printf("### the family, as achieved ###\n");
  std::printf("%-7s %-11s %-11s %-12s %-11s %-11s %-9s %-9s\n", "U0", "inletMa", "machMax",
              "drho/rho", "maxEosErr", "relL2(|U|)", "itComp", "itInc");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-11.5g %-11.5g %-12.5g %-11.4g %-11.5g %-9llu %-9llu\n", L.u0, L.inletMach,
                L.machMaxComp, L.deltaRhoOverRho, L.maxEosError, L.magRelL2,
                static_cast<unsigned long long>(L.itComp),
                static_cast<unsigned long long>(L.itInc));
  }

  std::printf("\n### velocity and pressure differences vs the incompressible reference ###\n");
  std::printf("%-7s %-12s %-12s %-12s %-12s %-12s %-12s\n", "U0", "Linf(u)", "relL2(u)", "Linf(v)",
              "Linf(p')", "relL2(p')", "dropRelDiff");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-12.5g %-12.5g %-12.5g %-12.5g %-12.5g %-12.5g\n", L.u0, L.uLinf, L.uRelL2,
                L.vLinf, L.pLinf, L.pRelL2, L.dropRelDiff);
  }

  std::printf("\n### density, mass flux and conservation ###\n");
  std::printf("%-7s %-11s %-11s %-12s %-12s %-12s %-12s\n", "U0", "rhoMin", "rhoMax",
              "max|rho-ref|", "fluxRelDiff", "massImbComp", "massImbInc");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-11.7g %-11.7g %-12.5g %-12.5g %-12.5g %-12.5g\n", L.u0, L.rhoMin, L.rhoMax,
                L.maxAbsRhoMinusRef, L.fluxMaxRelDiff, L.massImbalanceComp, L.massImbalanceInc);
  }

  std::printf("\n### mass flow balance (compressible) ###\n");
  std::printf("%-7s %-14s %-14s %-14s %-12s\n", "U0", "inletFlow", "outletFlow", "sum", "inletInc");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-14.7g %-14.7g %-14.4g %-12.7g\n", L.u0, L.inletFlowComp, L.outletFlowComp,
                L.inletFlowComp + L.outletFlowComp, L.inletFlowInc);
  }

  std::printf("\n### energy: P17.5 on the converged compressible fields ###\n");
  std::printf("%-7s %-13s %-12s %-12s %-13s %-14s %-14s %-8s\n", "U0", "e [J/kg]", "ke_max", "ke/e",
              "T recov err", "E imbalance", "predicted", "ratio");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-13.7g %-12.6g %-12.4g %-13.4g %-14.6g %-14.6g %-8.5g\n", L.u0,
                L.eInternal, L.eKinetic, L.keOverE, L.tRecoveryMaxErr, L.energyImbalance,
                L.energyPredicted,
                (L.energyPredicted != 0.0) ? L.energyImbalance / L.energyPredicted : 0.0);
  }
  std::printf("  The balance CANNOT close on an ISOTHERMAL solve: T is an INPUT, so the isothermal\n");
  std::printf("  constraint is itself an unmodelled heat sink, and P17.4 supplies no stress tensor\n");
  std::printf("  for the viscous-work term. 'predicted' is the INDEPENDENTLY computed mass-weighted\n");
  std::printf("  kinetic-energy gain from inlet to outlet; its agreement with 'E imbalance' is the\n");
  std::printf("  real check, and turns an unclosable balance into a measurement.\n");

  std::printf("\n### G5's ACTUAL energy criterion: the KINETIC contribution, relative to itself ###\n");
  std::printf("%-7s %-15s %-15s %-14s %-14s %-12s\n", "U0", "ke_comp [J]", "ke_inc [J]",
              "relDiff", "noise floor", "signal/noise");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-15.8g %-15.8g %-14.5g %-14.5g %-12.4g\n", L.u0, L.kineticComp,
                L.kineticInc, L.kineticRelDiff, L.energyNoiseFloor, L.energySignalToNoise);
  }
  std::printf("  relDiff is the domain kinetic energy of the COMPRESSIBLE solution against the\n");
  std::printf("  INCOMPRESSIBLE one, normalised by the latter -- the kinetic contribution measured\n");
  std::printf("  against its OWN magnitude, exactly as the frozen G5 clause requires.\n");
  std::printf("  'noise floor' is |massImbalance| * h, the enthalpy-flux error the residual mass\n");
  std::printf("  imbalance implies. Where signal/noise falls below ~1 the BALANCE cannot reveal the\n");
  std::printf("  kinetic term at all -- which is why the criterion is placed on ke directly.\n");

  std::printf("\n### x-momentum budget (compressible), with an ANALYTICAL cross-check ###\n");
  std::printf("%-7s %-14s %-14s %-15s %-15s %-10s\n", "U0", "netMomFlux", "pressureForce",
              "viscousImplied", "viscousPoiseuille", "ratio");
  for (const Level& L : levels) {
    std::printf("%-7.3g %-14.6g %-14.6g %-15.6g %-15.6g %-10.5g\n", L.u0, L.momFluxNet,
                L.momPressureForce, L.momViscousImplied, L.momViscousAnalytic,
                (L.momViscousAnalytic != 0.0) ? L.momViscousImplied / L.momViscousAnalytic : 0.0);
  }

  std::printf("\n### observed TREND exponents, error ~ C Ma^n (measured, not assumed) ###\n");
  if (levels.size() >= 2) {
    const Level& hi = levels.front();
    const Level& lo = levels.back();
    std::printf("  over Ma %.5g -> %.5g  (a factor of %.3g)\n", hi.inletMach, lo.inletMach,
                hi.inletMach / lo.inletMach);
    std::printf("  %-22s n = %.4f\n", "drho/rho",
                observedExponent(hi.inletMach, hi.deltaRhoOverRho, lo.inletMach, lo.deltaRhoOverRho));
    std::printf("  %-22s n = %.4f\n", "relL2(|U|)",
                observedExponent(hi.inletMach, hi.magRelL2, lo.inletMach, lo.magRelL2));
    std::printf("  %-22s n = %.4f\n", "relL2(p')",
                observedExponent(hi.inletMach, hi.pRelL2, lo.inletMach, lo.pRelL2));
    std::printf("  %-22s n = %.4f\n", "fluxRelDiff",
                observedExponent(hi.inletMach, hi.fluxMaxRelDiff, lo.inletMach, lo.fluxMaxRelDiff));
    std::printf("  theory for THIS case: the pressure drop is viscous-dominated,\n");
    std::printf("    dp ~ 12 mu U L / H^2, so drho/rho = dp/p ~ Ma^1. A quadratic expectation\n");
    std::printf("    (dp ~ rho U^2) would be wrong here.\n");
  }

  std::printf("\n### section 20: is the COMPRESSIBLE path genuinely active? ###\n");
  for (const Level& L : levels) {
    std::printf("  U0 %-6.3g density varies over %.6g  (uniform would mean NO EOS coupling)\n", L.u0,
                L.rhoMax - L.rhoMin);
  }
  std::printf("  maxEosError above is |rho_solver - p_abs/(R T)|: if the solver had fallen back to\n");
  std::printf("  a constant density this would be O(drho), not O(eps).\n");

  std::printf("\ndone\n");
  return 0;
}
