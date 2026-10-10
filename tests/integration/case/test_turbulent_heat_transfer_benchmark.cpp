// P16 -- turbulent forced-convection benchmark, Gnielinski.
//
// The definition -- reference, geometry, Re, Pr, thermal BCs, the exact Nu
// convention, and the acceptance band -- was FROZEN before any CFD Nusselt
// number was computed, in
// results/p16/turbulence/eddy_viscosity/benchmark.md. That file also records
// an a-priori PREDICTION that this benchmark would fail by a factor of 4-8
// because the codebase has no thermal wall function, with the derivation.
//
// Read the prediction before reading the result. It is there so the outcome
// cannot be rationalised after the fact in either direction.
//
// This asks a different question from the conservation test. Conservation asks
// "did we discretise energy consistently?" and is satisfied at machine
// precision (eps_E = 2.8e-16). This asks "does the model predict credible wall
// heat transfer?" -- a physics question the discretisation cannot answer.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <vector>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/thermal/TurbulentConductivity.hpp"

using cfd::Index;
using cfd::Real;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

namespace {

constexpr const char* kCaseDirectory = "cases/turbulent_heated_channel";

// FROZEN -- all from the committed case file.
constexpr Real kDensity = 1.225;
constexpr Real kViscosity = 1.8e-5;
constexpr Real kMolecularConductivity = 0.0257;
constexpr Real kSpecificHeat = 1005.0;
constexpr Real kInletVelocity = 10.0;
constexpr Real kChannelHeight = 0.1;
constexpr Real kChannelLength = 2.0;
constexpr Real kHydraulicDiameter = 2.0 * kChannelHeight;  // parallel plates
constexpr Real kHeatedArea = 2.0 * kChannelLength;         // both walls, unit depth
constexpr Real kInletTemperature = 300.0;
constexpr Real kWallTemperature = 400.0;

// FROZEN acceptance band -- from reference uncertainty, never from CFD.
constexpr Real kBandLow = 0.5;
constexpr Real kBandHigh = 2.0;

Real reynolds() { return kDensity * kInletVelocity * kHydraulicDiameter / kViscosity; }
Real prandtl() { return kViscosity * kSpecificHeat / kMolecularConductivity; }

Real gnielinskiNusselt() {
  const Real re = reynolds();
  const Real pr = prandtl();
  const Real f = std::pow((0.79 * std::log(re)) - 1.64, -2.0);
  const Real fOver8 = f / 8.0;
  const Real numerator = fOver8 * (re - 1000.0) * pr;
  const Real denominator = 1.0 + (12.7 * std::sqrt(fOver8) * (std::pow(pr, 2.0 / 3.0) - 1.0));
  return numerator / denominator;
}

struct Measured {
  Real wallHeat{};
  Real bulkOutlet{};
  Real logMeanDeltaT{};
  Real heatTransferCoefficient{};
  Real nusselt{};
};

// Wall heat and outlet enthalpy from the SAME operator the conservation test
// uses and the SAME one the solve used -- not a second formula. `muTScale`
// multiplies mu_t, so the k_t = 0 control runs through this identical routine.
Measured measure(const ProjectRunResult& run, Real muTScale) {
  const auto setup = cfd::io::CaseBuilder{}.build(*run.caseDefinition);
  const auto& mesh = setup.mesh;
  const auto& temperature = run.thermalResult->temperature;
  const auto& muT = run.turbulentThermalResult->turbulentViscosity;
  const Real prt = run.turbulentThermalResult->turbulentPrandtl;

  const auto massFlux = cfd::physics::calculateMassFlux(mesh, run.simpleResult->velocity,
                                                        setup.fluid, setup.velocityBoundaries);
  const auto gradT = cfd::discretization::gradient(mesh, temperature, *setup.temperatureBoundaries);

  Real wallHeat = 0.0;
  Real outletEnthalpy = 0.0;
  Real outletMass = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    const bool isWall = (patch.name() == "bottom" || patch.name() == "top");
    const bool isOutlet = (patch.name() == "right");
    for (const Index faceId : patch.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Index owner = face.owner();
      if (isWall) {
        // THE PRODUCTION WALL FLUX. Read, never re-derived.
        //
        // With a thermal wall function active the k_eff cell-centre gradient
        // does not describe the wall treatment at all -- reconstructing it
        // that way reported Q_wall = 1.13e5 W against a real 4.1e4, and turned
        // a corrected model into an apparently worse one. The solver now
        // publishes the exact per-face result its own matrix was built from.
        //
        // `muTScale == 0` is control A: there is no production result for a
        // hypothetical k_t = 0 run, so that control falls back on the
        // conduction-only gradient deliberately, which is what it is testing.
        if (muTScale == 1.0 && !run.thermalResult->wallHeatFlux.empty()) {
          wallHeat += run.thermalResult->wallHeatFlux[faceId];
        } else {
          const Real kEff = kMolecularConductivity + cfd::thermal::turbulentConductivity(
                                                         muTScale * muT[owner], kSpecificHeat, prt);
          const Real distance =
              cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
          const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
              mesh, face, kEff, distance, &gradT, /*prescribedValue=*/true);
          Real q = (terms.boundaryValueCoefficient * kWallTemperature) + terms.explicitFlux -
                   (terms.coefficient * temperature[owner]);
          if (terms.farCellCoefficient != 0.0) {
            q += terms.farCellCoefficient * temperature[terms.farCell];
          }
          wallHeat += q;
        }
      } else if (isOutlet) {
        outletMass += massFlux[faceId];
        outletEnthalpy += massFlux[faceId] * kSpecificHeat * temperature[owner];
      }
    }
  }

  Measured m;
  m.wallHeat = wallHeat;
  m.bulkOutlet = outletEnthalpy / (outletMass * kSpecificHeat);
  // FROZEN: log-mean, because the wall is isothermal and the bulk varies along
  // the channel.
  const Real dIn = kWallTemperature - kInletTemperature;
  const Real dOut = kWallTemperature - m.bulkOutlet;
  m.logMeanDeltaT = (dIn - dOut) / std::log(dIn / dOut);
  m.heatTransferCoefficient = m.wallHeat / (kHeatedArea * m.logMeanDeltaT);
  m.nusselt = m.heatTransferCoefficient * kHydraulicDiameter / kMolecularConductivity;
  return m;
}

}  // namespace

TEST(TurbulentHeatTransferBenchmark, NusseltAgainstGnielinski) {
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.thermalResult.has_value());
  ASSERT_TRUE(run.turbulentThermalResult.has_value());
  ASSERT_TRUE(run.simpleResult.has_value());
  ASSERT_TRUE(run.caseDefinition.has_value());

  const Real nuReference = gnielinskiNusselt();
  const Measured m = measure(run, 1.0);
  const Real ratio = m.nusselt / nuReference;

  std::printf("\nGnielinski benchmark, turbulent_heated_channel\n");
  std::printf("  Re_Dh            = %.4e\n", reynolds());
  std::printf("  Pr               = %.4f\n", prandtl());
  std::printf("  Nu_Gnielinski    = %.2f\n", nuReference);
  std::printf("  Q_wall           = %.5e W\n", m.wallHeat);
  std::printf("  T_bulk_out       = %.2f K\n", m.bulkOutlet);
  std::printf("  DT_lm            = %.2f K\n", m.logMeanDeltaT);
  std::printf("  h                = %.2f W/m2K\n", m.heatTransferCoefficient);
  std::printf("  Nu_CFD           = %.2f\n", m.nusselt);
  std::printf("  Nu_CFD / Nu_ref  = %.3f   (frozen band %.1f - %.1f)\n", ratio, kBandLow,
              kBandHigh);

  // Sanity on the reference itself, so a broken correlation cannot masquerade
  // as a broken solver.
  EXPECT_GT(nuReference, 100.0);
  EXPECT_LT(nuReference, 500.0);

  EXPECT_GE(ratio, kBandLow) << "Nu_CFD is far below the correlation";
  EXPECT_LE(ratio, kBandHigh)
      << "Nu_CFD = " << m.nusselt << " against Gnielinski " << nuReference << " (ratio " << ratio
      << "). See benchmark.md: this was PREDICTED before the run, and the predicted cause is "
         "the absence of a thermal wall function (no T+ law), which makes the wall heat flux "
         "the raw cell-centre gradient across a cell that spans the log layer.";
}

TEST(TurbulentHeatTransferBenchmark, ControlAZeroTurbulentConductivityCollapsesNusselt) {
  // CONTROL A. With k_t forced to zero the wall heat transfer must collapse
  // toward pure conduction, far below the turbulent correlation. If it did not,
  // the benchmark would be insensitive to the very term it exists to test.
  const CaseFixtureCopy fixture(kCaseDirectory);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.turbulentThermalResult.has_value());

  const Measured turbulent = measure(run, 1.0);
  const Measured conductionOnly = measure(run, 0.0);
  std::printf("\ncontrol A: Nu with k_t = 0 is %.2f, against %.2f with k_t\n",
              conductionOnly.nusselt, turbulent.nusselt);

  EXPECT_LT(conductionOnly.nusselt, 0.2 * turbulent.nusselt)
      << "removing the turbulent conductivity barely changed Nu, so this benchmark cannot "
         "detect a broken k_t coupling";
  EXPECT_LT(conductionOnly.nusselt / gnielinskiNusselt(), kBandLow)
      << "conduction alone still lands inside the turbulent acceptance band, so the band "
         "cannot discriminate";
}

TEST(TurbulentHeatTransferBenchmark, ControlBNusseltIsMonotoneInTurbulentPrandtl) {
  // CONTROL B. k_t ~ 1/Pr_t, so Nu must fall monotonically as Pr_t rises --
  // through the real case path, one number changed in physics.json.
  Real previous = std::numeric_limits<Real>::infinity();
  for (const Real prt : {0.5, 0.85, 1.5, 3.0}) {
    const CaseFixtureCopy fixture(kCaseDirectory);
    {
      const std::filesystem::path physicsPath = fixture.path() / "physics.json";
      nlohmann::json physics;
      {
        std::ifstream in(physicsPath);
        ASSERT_TRUE(in.good());
        in >> physics;
      }
      physics["turbulence"]["turbulent_prandtl"] = prt;
      std::ofstream out(physicsPath);
      ASSERT_TRUE(out.good());
      out << physics.dump(2);
    }
    const ProjectRunResult run = ProjectRunner::run(fixture.path());
    ASSERT_EQ(run.status, ProjectRunStatus::Converged) << "Pr_t = " << prt;
    ASSERT_TRUE(run.turbulentThermalResult.has_value());
    const Measured m = measure(run, 1.0);
    std::printf("control B: Pr_t = %.2f -> Nu = %.2f\n", prt, m.nusselt);
    EXPECT_LT(m.nusselt, previous) << "Nu did not fall as Pr_t rose to " << prt;
    previous = m.nusselt;
  }
}
