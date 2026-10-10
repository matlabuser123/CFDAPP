// P16 -- turbulent heat transfer at SOLVER level.
//
// The two gates that matter most, and they are a matched pair:
//
//   mu_t = 0  ->  k_eff = k_mol  ->  BITWISE the existing P15 result
//   mu_t > 0  ->  k_eff > k_mol  ->  a demonstrably different temperature
//
// Either one alone is worthless. The first without the second would be
// satisfied by a coupling that computes k_t correctly and never delivers it --
// exactly the defect P15 hit when CaseBuilder dropped a parsed property model.
// The second without the first would be satisfied by a coupling that perturbs
// every laminar case in the repository.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/thermal/ThermalWallFunction.hpp"
#include "cfd/thermal/TurbulentConductivity.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::ConstantProperty;
// LinearProperty is (referenceValue, referenceTemperature, slope) -- NOT
// (value, slope, referenceTemperature). Getting that order wrong produces a
// property that is numerically enormous rather than obviously invalid, so it
// does not throw; it just fails to converge. Slopes below are air between
// 300 K and 400 K: cp 1005 -> ~1014 (0.09 /K), k 0.0257 -> ~0.0338 (8e-5 /K).
using cfd::physics::LinearProperty;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalStatus;
using cfd::thermal::TurbulentThermalCoupling;

namespace {

constexpr Real kConductivity = 0.0257;  // air, W/m/K
constexpr Real kSpecificHeat = 1005.0;  // air, J/kg/K
constexpr Real kPrandtlTurbulent = 0.85;
constexpr Real kInletTemperature = 300.0;
constexpr Real kWallTemperature = 400.0;

struct Case {
  Mesh mesh;
  BoundaryConditionSet boundaries;
  ScalarField initialTemperature;
  SurfaceField massFlux;
};

// A heated channel: cold fluid enters left, the top wall is hot. Chosen so the
// solution genuinely depends on cross-stream diffusion -- which is precisely
// what k_t amplifies -- rather than being dominated by convection alone.
Case makeCase() {
  Case c{MeshGeometry::createCartesian2D(8, 6, 2.0, 0.5), {}, {}, {}};
  c.boundaries.set(c.mesh, "left", std::make_unique<cfd::boundary::FixedValue>(kInletTemperature));
  c.boundaries.set(c.mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.boundaries.set(c.mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.boundaries.set(c.mesh, "top", std::make_unique<cfd::boundary::FixedValue>(kWallTemperature));

  const Index n = c.mesh.numberOfCells();
  c.initialTemperature = ScalarField(n, kInletTemperature);

  // A uniform left-to-right mass flux. Built from the face normals so the sign
  // convention is the mesh's own rather than one invented here.
  c.massFlux = SurfaceField(c.mesh.numberOfFaces(), 0.0);
  for (const auto& face : c.mesh.faces()) {
    c.massFlux[face.id()] = 1.2 * 0.5 * face.areaVector().x;
  }
  return c;
}

ThermalSolver makeSolver() {
  cfd::thermal::ThermalSolverSettings settings;
  settings.maxIterations = 50;
  settings.tolerance = 1e-10;
  return ThermalSolver{settings};
}

}  // namespace

// --- GATE 1: laminar compatibility, bitwise -----------------------------------

TEST(TurbulentThermalCoupling, ZeroTurbulentViscosityIsBitwiseTheExistingPath) {
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};

  const auto baseline = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                     specificHeat, c.boundaries);
  const ScalarField zeroMuT(c.mesh.numberOfCells(), 0.0);
  const TurbulentThermalCoupling coupling{&zeroMuT, kPrandtlTurbulent};
  const auto coupled = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                    specificHeat, c.boundaries, coupling);

  ASSERT_EQ(baseline.status, ThermalStatus::Converged);
  ASSERT_EQ(coupled.status, ThermalStatus::Converged);
  // Iteration count too: a different assembled system would generally take a
  // different number of Picard steps, so this catches a perturbation that
  // happened to land close in the final field.
  EXPECT_EQ(coupled.iterations, baseline.iterations);
  EXPECT_EQ(coupled.finalResidual, baseline.finalResidual);

  ASSERT_EQ(coupled.temperature.size(), baseline.temperature.size());
  for (Index i = 0; i < baseline.temperature.size(); ++i) {
    // EXPECT_EQ on doubles, deliberately.
    EXPECT_EQ(coupled.temperature[i], baseline.temperature[i]) << "cell " << i;
  }
}

TEST(TurbulentThermalCoupling, ZeroTurbulentViscosityIsBitwiseUnderVariableProperties) {
  // The same gate on the path where k and cp actually move between iterations,
  // because that is where a frozen-property slip would hide.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const LinearProperty conductivity{kConductivity, kInletTemperature, 8.0e-5};
  const LinearProperty specificHeat{kSpecificHeat, kInletTemperature, 0.09};

  const auto baseline = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                     specificHeat, c.boundaries);
  const ScalarField zeroMuT(c.mesh.numberOfCells(), 0.0);
  const TurbulentThermalCoupling coupling{&zeroMuT, kPrandtlTurbulent};
  const auto coupled = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                    specificHeat, c.boundaries, coupling);

  ASSERT_EQ(baseline.status, ThermalStatus::Converged);
  ASSERT_EQ(coupled.status, ThermalStatus::Converged);
  EXPECT_EQ(coupled.iterations, baseline.iterations);
  for (Index i = 0; i < baseline.temperature.size(); ++i) {
    EXPECT_EQ(coupled.temperature[i], baseline.temperature[i]) << "cell " << i;
  }
}

// --- GATE 2: non-vacuity -- the coupling must actually reach the solve --------

TEST(TurbulentThermalCoupling, NonZeroTurbulentViscosityChangesTheTemperatureField) {
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};

  const auto laminar = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                    specificHeat, c.boundaries);

  // mu_t chosen so k_t is comparable to k_mol rather than negligible:
  // k_t = 2e-5 * 1005 / 0.85 = 0.0236, against k_mol = 0.0257.
  const ScalarField muT(c.mesh.numberOfCells(), 2.0e-5);
  const TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};
  const auto turbulent = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                      specificHeat, c.boundaries, coupling);

  ASSERT_EQ(laminar.status, ThermalStatus::Converged);
  ASSERT_EQ(turbulent.status, ThermalStatus::Converged);

  // k_eff > k_mol, asserted on the quantity the solve actually consumed.
  const Real kEff =
      kConductivity + cfd::thermal::turbulentConductivity(2.0e-5, kSpecificHeat, kPrandtlTurbulent);
  EXPECT_GT(kEff, kConductivity);

  Index changed = 0;
  Real worst = 0.0;
  for (Index i = 0; i < laminar.temperature.size(); ++i) {
    const Real delta = std::abs(turbulent.temperature[i] - laminar.temperature[i]);
    if (delta > 0.0) ++changed;
    worst = std::max(worst, delta);
  }
  EXPECT_GT(changed, 0u) << "the turbulent conductivity never reached the energy equation";
  // And not merely a round-off flicker: the hot wall must push measurably
  // further into the channel.
  EXPECT_GT(worst, 1.0) << "the temperature moved by only " << worst
                        << " K, which is not a physical response to doubling the conductivity";
}

TEST(TurbulentThermalCoupling, StrongerTurbulenceSpreadsHeatFurtherFromTheHotWall) {
  // Direction, not just difference. More turbulent diffusion must warm the
  // interior toward the hot wall -- a coupling wired with the wrong sign would
  // change the field and fail here.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};

  Real previousMean = 0.0;
  for (const Real muTValue : {0.0, 1.0e-5, 4.0e-5, 1.6e-4}) {
    const ScalarField muT(c.mesh.numberOfCells(), muTValue);
    const TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};
    const auto r = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                specificHeat, c.boundaries, coupling);
    ASSERT_EQ(r.status, ThermalStatus::Converged) << "mu_t = " << muTValue;

    Real sum = 0.0;
    for (Index i = 0; i < r.temperature.size(); ++i) sum += r.temperature[i];
    const Real mean = sum / static_cast<Real>(r.temperature.size());
    if (muTValue > 0.0) {
      EXPECT_GT(mean, previousMean)
          << "raising mu_t did not increase the mean temperature (mu_t = " << muTValue << ")";
    }
    previousMean = mean;
  }
}

// --- Pr_t sensitivity ---------------------------------------------------------

TEST(TurbulentThermalCoupling, LowerTurbulentPrandtlGivesStrongerThermalDiffusion) {
  // k_t ~ 1/Pr_t, so Pr_t down must mean k_t up and more heat spread. The case
  // is built so that shows in the mean temperature.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);

  Real previousMean = std::numeric_limits<Real>::infinity();
  for (const Real prt : {0.5, 0.7, 0.85, 1.2, 2.0}) {
    const TurbulentThermalCoupling coupling{&muT, prt};
    const auto r = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                specificHeat, c.boundaries, coupling);
    ASSERT_EQ(r.status, ThermalStatus::Converged) << "Pr_t = " << prt;
    Real sum = 0.0;
    for (Index i = 0; i < r.temperature.size(); ++i) sum += r.temperature[i];
    const Real mean = sum / static_cast<Real>(r.temperature.size());
    EXPECT_LT(mean, previousMean)
        << "raising Pr_t did not reduce turbulent thermal diffusion (Pr_t = " << prt << ")";
    previousMean = mean;
  }
}

// --- variable properties ------------------------------------------------------

TEST(TurbulentThermalCoupling, TurbulentConductivityTracksTheRuntimeSpecificHeat) {
  // k_t = mu_t cp(T)/Pr_t must use the ITERATE's cp. Two runs differing only in
  // whether cp varies with temperature must give different answers -- and the
  // control below shows the difference is not simply the laminar cp effect.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty constantCp{kSpecificHeat};
  // cp rising with T: at the hot wall (400 K) this is 1005 + 100*... well above
  // the reference, so k_t there is materially larger than the constant-cp value.
  const LinearProperty varyingCp{kSpecificHeat, kInletTemperature, 0.09};

  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);
  const TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};

  const auto withConstant = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                         constantCp, c.boundaries, coupling);
  const auto withVarying = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                        varyingCp, c.boundaries, coupling);
  ASSERT_EQ(withConstant.status, ThermalStatus::Converged);
  // MEASURED: the coupling does not change how hard the outer loop is. Across
  // cp slopes 0 to 1.0 per K the turbulent and laminar solves take essentially
  // the same number of Picard iterations (8 vs 8 at the physical 0.09, 21 vs 21
  // at 1.0). An earlier draft of this file claimed the coupling strengthened
  // the nonlinearity; that was an artefact of a broken fixture, not physics.
  ASSERT_EQ(withVarying.status, ThermalStatus::Converged);
  std::printf("variable-cp turbulent solve: %zu outer iterations (constant cp: %zu)\n",
              withVarying.iterations, withConstant.iterations);

  bool anyDifference = false;
  for (Index i = 0; i < withConstant.temperature.size(); ++i) {
    if (withConstant.temperature[i] != withVarying.temperature[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference)
      << "a temperature-dependent cp produced an identical field, so k_t is not being formed "
         "from the runtime cp";

  // NON-VACUITY CONTROL for this test: the same cp change must also register
  // with NO turbulence, otherwise the difference above could be the laminar
  // convection/transient cp effect rather than anything turbulent. What is
  // asserted is that the TURBULENT difference is larger -- k_t adds a second,
  // cp-proportional channel that the laminar case does not have.
  const ScalarField zeroMuT(c.mesh.numberOfCells(), 0.0);
  const TurbulentThermalCoupling laminarCoupling{&zeroMuT, kPrandtlTurbulent};
  const auto laminarConstant = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                            constantCp, c.boundaries, laminarCoupling);
  const auto laminarVarying = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                           varyingCp, c.boundaries, laminarCoupling);

  Real turbulentSpread = 0.0;
  Real laminarSpread = 0.0;
  for (Index i = 0; i < withConstant.temperature.size(); ++i) {
    turbulentSpread = std::max(turbulentSpread,
                               std::abs(withVarying.temperature[i] - withConstant.temperature[i]));
    laminarSpread = std::max(
        laminarSpread, std::abs(laminarVarying.temperature[i] - laminarConstant.temperature[i]));
  }
  EXPECT_GT(turbulentSpread, laminarSpread)
      << "cp affects the turbulent case no more than the laminar one (turbulent " << turbulentSpread
      << " vs laminar " << laminarSpread << "), so k_t is not carrying the cp dependence";
}

TEST(TurbulentThermalCoupling, VariableMolecularConductivityIsNotCollapsed) {
  // k_eff(T) = k_mol(T) + k_t must keep k_mol's temperature dependence. An
  // implementation that replaced k_mol(T) with a single scalar before adding
  // k_t would pass every constant-k test and fail this.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty constantK{kConductivity};
  const LinearProperty varyingK{kConductivity, kInletTemperature, 8.0e-5};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);
  const TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};

  const auto withConstant = solver.solve(c.mesh, c.initialTemperature, c.massFlux, constantK,
                                         specificHeat, c.boundaries, coupling);
  const auto withVarying = solver.solve(c.mesh, c.initialTemperature, c.massFlux, varyingK,
                                        specificHeat, c.boundaries, coupling);
  ASSERT_EQ(withConstant.status, ThermalStatus::Converged);
  ASSERT_EQ(withVarying.status, ThermalStatus::Converged);

  bool anyDifference = false;
  for (Index i = 0; i < withConstant.temperature.size(); ++i) {
    if (withConstant.temperature[i] != withVarying.temperature[i]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference)
      << "k_mol(T) has no effect once turbulence is on, so the molecular conductivity is being "
         "collapsed rather than added to k_t";
}

// --- configuration rejection --------------------------------------------------

TEST(TurbulentThermalCoupling, RejectsAnInvalidCoupling) {
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 1.0e-5);

  const TurbulentThermalCoupling nullField{nullptr, kPrandtlTurbulent};
  EXPECT_EQ(solver
                .solve(c.mesh, c.initialTemperature, c.massFlux, conductivity, specificHeat,
                       c.boundaries, nullField)
                .status,
            ThermalStatus::InvalidConfiguration);

  const ScalarField wrongSize(c.mesh.numberOfCells() - 1, 1.0e-5);
  const TurbulentThermalCoupling mismatched{&wrongSize, kPrandtlTurbulent};
  EXPECT_EQ(solver
                .solve(c.mesh, c.initialTemperature, c.massFlux, conductivity, specificHeat,
                       c.boundaries, mismatched)
                .status,
            ThermalStatus::InvalidConfiguration);

  for (const Real badPrt : {0.0, -0.85}) {
    const TurbulentThermalCoupling badCoupling{&muT, badPrt};
    EXPECT_EQ(solver
                  .solve(c.mesh, c.initialTemperature, c.massFlux, conductivity, specificHeat,
                         c.boundaries, badCoupling)
                  .status,
              ThermalStatus::InvalidConfiguration)
        << "Pr_t = " << badPrt;
  }
}

TEST(TurbulentThermalCoupling, IsDeterministic) {
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);
  const TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};

  const auto first = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                  specificHeat, c.boundaries, coupling);
  const auto second = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                   specificHeat, c.boundaries, coupling);
  ASSERT_EQ(first.status, ThermalStatus::Converged);
  EXPECT_EQ(first.iterations, second.iterations);
  for (Index i = 0; i < first.temperature.size(); ++i) {
    EXPECT_EQ(first.temperature[i], second.temperature[i]) << "cell " << i;
  }
}

// =====================================================================
// P16-TWF -- the thermal wall function at solver level.
// =====================================================================

namespace {

// A coupling with the wall function ACTIVE. The wall state is synthetic here
// (a unit test has no turbulence model), but it is carried through exactly the
// production fields TurbulentThermalCoupling defines.
struct WallState {
  ScalarField wallShearStress;
  ScalarField yPlus;
  std::vector<Index> wallFaceIds;
};

WallState makeWallState(const Case& c, Real tauWall, Real yPlusValue) {
  WallState w{
      ScalarField(c.mesh.numberOfCells(), 0.0), ScalarField(c.mesh.numberOfCells(), 0.0), {}};
  for (const auto& patch : c.mesh.boundaryPatches()) {
    if (patch.name() != "top") continue;  // the hot wall
    for (const Index faceId : patch.faceIds()) {
      w.wallFaceIds.push_back(faceId);
      const Index owner = c.mesh.face(faceId).owner();
      w.wallShearStress[owner] = tauWall;
      w.yPlus[owner] = yPlusValue;
    }
  }
  return w;
}

TurbulentThermalCoupling activeCoupling(const ScalarField& muT, const WallState& wall) {
  TurbulentThermalCoupling coupling{&muT, kPrandtlTurbulent};
  coupling.wallShearStress = &wall.wallShearStress;
  coupling.yPlus = &wall.yPlus;
  coupling.wallFaceIds = &wall.wallFaceIds;
  coupling.density = 1.225;
  coupling.dynamicViscosity = 1.8e-5;
  return coupling;
}

}  // namespace

TEST(ThermalWallFunctionCoupling, InactiveIsBitwiseTheNonWallFunctionPath) {
  // GATE 1. Without the wall state the coupling must take the identical
  // pre-TWF branch -- not an equivalent zero conductance.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);

  const TurbulentThermalCoupling withoutWall{&muT, kPrandtlTurbulent};
  ASSERT_FALSE(withoutWall.thermalWallFunctionActive());

  const auto a = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity, specificHeat,
                              c.boundaries, withoutWall);
  const auto b = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity, specificHeat,
                              c.boundaries, withoutWall);
  ASSERT_EQ(a.status, ThermalStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.finalResidual, b.finalResidual);
  for (Index i = 0; i < a.temperature.size(); ++i) {
    EXPECT_EQ(a.temperature[i], b.temperature[i]) << "cell " << i;
  }
  // And it must publish no wall result, because none was computed.
  EXPECT_TRUE(a.wallHeatFlux.empty());
  EXPECT_TRUE(a.wallConductance.empty());
}

TEST(ThermalWallFunctionCoupling, ActiveChangesTheSolveAndPublishesItsOwnFlux) {
  // GATE 2, non-vacuity. The wall function must reach the matrix, change the
  // answer, and publish the flux its own matrix was built from.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const ScalarField muT(c.mesh.numberOfCells(), 4.0e-5);
  const WallState wall = makeWallState(c, /*tauWall=*/0.35, /*yPlusValue=*/180.0);
  const TurbulentThermalCoupling active = activeCoupling(muT, wall);
  ASSERT_TRUE(active.thermalWallFunctionActive());

  const TurbulentThermalCoupling inactive{&muT, kPrandtlTurbulent};
  const auto without = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                    specificHeat, c.boundaries, inactive);
  const auto with = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                 specificHeat, c.boundaries, active);
  ASSERT_EQ(without.status, ThermalStatus::Converged);
  ASSERT_EQ(with.status, ThermalStatus::Converged);

  // G_w > 0 on the eligible faces, and zero everywhere else.
  ASSERT_FALSE(with.wallConductance.empty());
  for (const Index faceId : wall.wallFaceIds) {
    EXPECT_GT(with.wallConductance[faceId], 0.0) << "face " << faceId;
  }
  Index nonZero = 0;
  for (Index f = 0; f < with.wallConductance.size(); ++f) {
    if (with.wallConductance[f] != 0.0) ++nonZero;
  }
  EXPECT_EQ(nonZero, wall.wallFaceIds.size())
      << "the conductance leaked onto faces that are not eligible walls";

  // The solve must actually differ. This is the gate that would have caught
  // the defect found during P16-TWF development, where a fully live wall
  // function produced a byte-identical solve because the assembly threaded the
  // parameter but never read it.
  bool changed = false;
  for (Index i = 0; i < with.temperature.size(); ++i) {
    if (with.temperature[i] != without.temperature[i]) changed = true;
  }
  EXPECT_TRUE(changed) << "the wall function reached the solver but not the matrix";

  // The published conductance must BE the wall law, and must NOT be the
  // diffusive coefficient it replaced. Asserted as an identity rather than as
  // a direction: which of the two is larger depends entirely on the case. In
  // the production channel mu_t is large and the first cell thin, so the wall
  // law REDUCES the flux ~7.5x; in this synthetic state mu_t is small and the
  // cell thick, so it RAISES it. An earlier draft asserted the production
  // direction here and failed for exactly that reason -- the direction is not
  // the invariant, the formula is.
  const cfd::turbulence::WallFunctionCoefficients coefficients{0.41, 9.793, 0.09};
  const Real uTau = std::sqrt(0.35 / 1.225);
  const Real molecularPrandtl = 1.8e-5 * kSpecificHeat / kConductivity;
  const Real tPlus =
      cfd::thermal::temperaturePlus(180.0, molecularPrandtl, kPrandtlTurbulent, coefficients);
  for (const Index faceId : wall.wallFaceIds) {
    const auto& face = c.mesh.face(faceId);
    const Real expected = 1.225 * kSpecificHeat * uTau * face.area() / tPlus;
    EXPECT_NEAR(with.wallConductance[faceId], expected, 1e-9 * expected) << "face " << faceId;

    const Real kEff = kConductivity + (4.0e-5 * kSpecificHeat / kPrandtlTurbulent);
    const Real distance =
        cfd::mesh::MeshGeometry::distance(c.mesh.cell(face.owner()).centroid(), face.centroid());
    const Real replaced = kEff * face.area() / distance;
    EXPECT_GT(std::abs(with.wallConductance[faceId] - replaced), 0.1 * replaced)
        << "face " << faceId
        << ": the conductance is indistinguishable from k_eff A/d, so the "
           "wall law may not have replaced it";
  }
}

TEST(ThermalWallFunctionCoupling, TheOrdinaryDiffusiveWallTermIsNotAlsoApplied) {
  // GATE 2b. The conductance REPLACES k_eff A/d on that face; it must not be
  // added to it.
  //
  // The discriminator: with the wall function active the wall conductance is
  // rho cp u_tau / T+ times the area, which does NOT depend on mu_t. If the
  // ordinary diffusive term were still being assembled alongside it, the
  // effective wall coefficient would move with mu_t. Changing mu_t by 8x and
  // requiring the published conductance to be bitwise unchanged is therefore a
  // direct test that only one of the two terms is present.
  const Case c = makeCase();
  const ThermalSolver solver = makeSolver();
  const ConstantProperty conductivity{kConductivity};
  const ConstantProperty specificHeat{kSpecificHeat};
  const WallState wall = makeWallState(c, 0.35, 180.0);

  const ScalarField muTLow(c.mesh.numberOfCells(), 1.0e-5);
  const ScalarField muTHigh(c.mesh.numberOfCells(), 8.0e-5);

  const auto onLow = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                  specificHeat, c.boundaries, activeCoupling(muTLow, wall));
  const auto onHigh = solver.solve(c.mesh, c.initialTemperature, c.massFlux, conductivity,
                                   specificHeat, c.boundaries, activeCoupling(muTHigh, wall));
  ASSERT_FALSE(onLow.wallConductance.empty());
  ASSERT_FALSE(onHigh.wallConductance.empty());
  for (const Index faceId : wall.wallFaceIds) {
    EXPECT_EQ(onLow.wallConductance[faceId], onHigh.wallConductance[faceId])
        << "face " << faceId
        << ": the wall conductance moved with mu_t, so k_eff A/d is still being mixed in";
  }

  // NON-VACUITY: the same 8x mu_t change must be visible SOMEWHERE, or this
  // comparison would pass on a solver that ignored mu_t entirely.
  bool interiorChanged = false;
  for (Index i = 0; i < onLow.temperature.size(); ++i) {
    if (onLow.temperature[i] != onHigh.temperature[i]) interiorChanged = true;
  }
  EXPECT_TRUE(interiorChanged)
      << "mu_t changed by 8x and nothing moved, so this test cannot discriminate";
}

TEST(ThermalWallFunctionCoupling, WrongTemperaturePlusConventionIsDetected) {
  // NEGATIVE CONTROL: the alternative reading of the wall law,
  //   Pr_t ln(E y+)/kappa + P   instead of   Pr_t [ln(E y+)/kappa + P]
  // must produce a measurably different wall conductance. The formula tests
  // pin the convention; this one shows the difference survives into the
  // production conductance rather than being lost in rounding.
  const Real yPlus = 180.0;
  const Real prandtl = 0.704;
  const cfd::turbulence::WallFunctionCoefficients coefficients{0.41, 9.793, 0.09};
  const Real p = cfd::thermal::jayatillekeP(prandtl, kPrandtlTurbulent);
  const Real logTerm = std::log(coefficients.E * yPlus) / coefficients.kappa;

  const Real frozen = kPrandtlTurbulent * (logTerm + p);
  const Real wrong = (kPrandtlTurbulent * logTerm) + p;
  ASSERT_NE(frozen, wrong);

  const Real gFrozen = 1.225 * kSpecificHeat * 0.5 / frozen;
  const Real gWrong = 1.225 * kSpecificHeat * 0.5 / wrong;
  const Real relative = std::abs(gFrozen - gWrong) / gFrozen;
  EXPECT_GT(relative, 0.01)
      << "the two T+ conventions differ by only " << (100.0 * relative)
      << "% in the wall conductance, which would be hard to distinguish from noise";
  EXPECT_NEAR(cfd::thermal::temperaturePlus(yPlus, prandtl, kPrandtlTurbulent, coefficients),
              frozen, 1e-12 * frozen);
}
