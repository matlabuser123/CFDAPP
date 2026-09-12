#include "cfd/app/ProjectRunner.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/multiphase/MultiphaseProperties.hpp"
#include "cfd/multiphase/VolumeFractionEquation.hpp"
#include "cfd/multiphase/VolumeFractionSolver.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

namespace cfd::app {

using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

int exitCodeFor(ProjectRunStatus status) noexcept {
  switch (status) {
    case ProjectRunStatus::Converged:
      return 0;
    case ProjectRunStatus::ApplicationError:
      return 1;
    case ProjectRunStatus::InvalidCase:
      return 2;
    case ProjectRunStatus::DidNotConverge:
      return 3;
    case ProjectRunStatus::NumericalFailure:
      return 4;
    case ProjectRunStatus::Cancelled:
      return 5;
  }
  return 1;
}

namespace {

bool anyNonFinite(const SIMPLEResult& result) {
  for (cfd::Index i = 0; i < result.velocity.size(); ++i) {
    if (!std::isfinite(result.velocity[i].x) || !std::isfinite(result.velocity[i].y)) return true;
  }
  for (cfd::Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return true;
  }
  return false;
}

std::string thermalStatusName(cfd::thermal::ThermalStatus status) {
  using cfd::thermal::ThermalStatus;
  switch (status) {
    case ThermalStatus::Converged:
      return "Converged";
    case ThermalStatus::MaxIterations:
      return "MaxIterations";
    case ThermalStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case ThermalStatus::NonFiniteState:
      return "NonFiniteState";
    case ThermalStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-001: the species counterpart of thermalStatusName above.
std::string speciesStatusName(cfd::species::SpeciesStatus status) {
  using cfd::species::SpeciesStatus;
  switch (status) {
    case SpeciesStatus::Converged:
      return "Converged";
    case SpeciesStatus::MaxIterations:
      return "MaxIterations";
    case SpeciesStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case SpeciesStatus::NonFiniteState:
      return "NonFiniteState";
    case SpeciesStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-002: the multiphase counterpart of thermalStatusName above.
std::string volumeFractionStatusName(cfd::multiphase::VolumeFractionStatus status) {
  using cfd::multiphase::VolumeFractionStatus;
  switch (status) {
    case VolumeFractionStatus::Converged:
      return "Converged";
    case VolumeFractionStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case VolumeFractionStatus::NonFiniteState:
      return "NonFiniteState";
    case VolumeFractionStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

// P6-PHYS-002: reuses SIMPLE's existing turbulenceModel effective-
// viscosity injection point (mu_eff = molecular + mu_t, see
// cfd::turbulence::TurbulenceModel.hpp's own header comment: this
// formula is generic, not turbulence-specific) to feed mu_mix(alpha)
// into momentum assembly -- see ProjectRunner.hpp's own header comment
// for the full justification and scope (mu_mix only, evaluated at the
// case's configured *initial* alpha and held fixed for the whole SIMPLE
// solve; rho_mix stays out of continuity entirely). name() reports this
// plainly as what it is, not as a turbulence model, so a case's own
// metadata.json/CLI report is never confusing about what actually ran.
// correct() is a deliberate no-op: mu_t here is a fixed, precomputed
// field (not something SIMPLE's own outer iterations should update),
// unlike a genuine RANS model's own state.
class MixtureViscosityModel final : public cfd::turbulence::TurbulenceModel {
 public:
  // The caller (ProjectRunner::run(), where the real mesh is available)
  // is responsible for calling
  // cfd::turbulence::validateTurbulentViscosityField(mesh,
  // turbulentViscosityField) before constructing this -- not repeated
  // here, since that check needs the mesh this constructor does not
  // itself take (mirrors KEpsilonModel/KOmegaModel/SSTModel's own
  // convention of trusting already-mesh-sized fields their own
  // constructors receive).
  explicit MixtureViscosityModel(cfd::fields::ScalarField turbulentViscosityField)
      : turbulentViscosity_(std::move(turbulentViscosityField)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "multiphaseMixtureViscosity";
  }
  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override {
    return turbulentViscosity_;
  }
  void correct(const cfd::mesh::Mesh&, const cfd::fields::VectorField&,
               const cfd::fields::ScalarField&) override {}

 private:
  cfd::fields::ScalarField turbulentViscosity_;
};

ProjectRunStatus statusFor(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged:
      return ProjectRunStatus::Converged;
    case SIMPLEStatus::MaxIterations:
      return ProjectRunStatus::DidNotConverge;
    case SIMPLEStatus::Cancelled:
      return ProjectRunStatus::Cancelled;
    case SIMPLEStatus::MomentumFailure:
    case SIMPLEStatus::PressureCorrectionFailure:
    case SIMPLEStatus::NonFiniteState:
    case SIMPLEStatus::InvalidConfiguration:
      return ProjectRunStatus::NumericalFailure;
  }
  return ProjectRunStatus::NumericalFailure;
}

// P12-COMP-002: same mapping convention as statusFor(SIMPLEStatus) above,
// applied to the coupled compressible solve's own status -- used only
// when compressible.coupled is set, in which case this solve's outcome
// (not the incompressible warm-start's) becomes the run's authoritative
// status.
ProjectRunStatus statusFor(cfd::compressible::CompressibleSIMPLEStatus status) {
  using cfd::compressible::CompressibleSIMPLEStatus;
  switch (status) {
    case CompressibleSIMPLEStatus::Converged:
      return ProjectRunStatus::Converged;
    case CompressibleSIMPLEStatus::MaxIterations:
      return ProjectRunStatus::DidNotConverge;
    case CompressibleSIMPLEStatus::MomentumFailure:
    case CompressibleSIMPLEStatus::PressureCorrectionFailure:
    case CompressibleSIMPLEStatus::NonFiniteState:
    case CompressibleSIMPLEStatus::InvalidConfiguration:
      return ProjectRunStatus::NumericalFailure;
  }
  return ProjectRunStatus::NumericalFailure;
}

std::string compressibleSimpleStatusName(cfd::compressible::CompressibleSIMPLEStatus status) {
  using cfd::compressible::CompressibleSIMPLEStatus;
  switch (status) {
    case CompressibleSIMPLEStatus::Converged:
      return "Converged";
    case CompressibleSIMPLEStatus::MaxIterations:
      return "MaxIterations";
    case CompressibleSIMPLEStatus::MomentumFailure:
      return "MomentumFailure";
    case CompressibleSIMPLEStatus::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case CompressibleSIMPLEStatus::NonFiniteState:
      return "NonFiniteState";
    case CompressibleSIMPLEStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

}  // namespace

ProjectRunResult ProjectRunner::run(const std::filesystem::path& caseDirectory,
                                    const ProjectRunOptions& options) {
  ProjectRunResult out;

  std::optional<cfd::io::CaseDefinition> caseDefinition;
  std::optional<cfd::io::SimulationSetup> setupOpt;
  try {
    caseDefinition = cfd::io::CaseReader{}.read(caseDirectory);
    setupOpt = cfd::io::CaseBuilder{}.build(*caseDefinition);
  } catch (const cfd::Error& e) {
    out.status = ProjectRunStatus::InvalidCase;
    out.errorMessage = e.what();
    return out;
  }
  out.caseDefinition = caseDefinition;
  const cfd::io::SimulationSetup& setup = *setupOpt;
  out.mesh = setup.mesh;

  // Same turbulence-model construction/dispatch as the pre-P5 CLI
  // runCase() (see that function's own header comment for why this is
  // built here rather than by CaseBuilder).
  std::optional<cfd::turbulence::KEpsilonModel> kEpsilonModel;
  std::optional<cfd::turbulence::KOmegaModel> kOmegaModel;
  std::optional<cfd::turbulence::SSTModel> sstModel;
  cfd::turbulence::TurbulenceModel* activeTurbulenceModel = nullptr;
  if (setup.kEpsilonConfig.has_value()) {
    kEpsilonModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                          *setup.epsilonBoundaries, *setup.kEpsilonConfig);
    activeTurbulenceModel = &(*kEpsilonModel);
  } else if (setup.kOmegaConfig.has_value()) {
    kOmegaModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                        *setup.omegaBoundaries, *setup.kOmegaConfig);
    activeTurbulenceModel = &(*kOmegaModel);
  } else if (setup.sstConfig.has_value()) {
    sstModel.emplace(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                     *setup.omegaBoundaries, *setup.sstConfig);
    activeTurbulenceModel = &(*sstModel);
  }

  // P6-PHYS-002: reuses the same turbulenceModel injection slot as
  // above -- mutually exclusive with real turbulence by construction
  // (PhysicsConfigParser.cpp's own cross-block rejection), so this
  // `else if` is never reached when any of the three models above
  // already claimed the slot. mu_mix is evaluated at the case's
  // *initial* alpha only (held fixed for this entire SIMPLE solve -- see
  // ProjectRunner.hpp's own header comment on why), never the alpha
  // this same run will go on to solve for below.
  std::optional<MixtureViscosityModel> mixtureViscosityModel;
  if (setup.multiphase.has_value() && activeTurbulenceModel == nullptr) {
    cfd::fields::ScalarField turbulentViscosityField =
        cfd::multiphase::evaluateMixtureViscosityField(setup.mesh, setup.multiphase->initialAlpha,
                                                       setup.multiphase->system);
    for (cfd::Index i = 0; i < turbulentViscosityField.size(); ++i) {
      turbulentViscosityField[i] -= setup.fluid.dynamicViscosity();
    }
    cfd::turbulence::validateTurbulentViscosityField(setup.mesh, turbulentViscosityField);
    mixtureViscosityModel.emplace(std::move(turbulentViscosityField));
    activeTurbulenceModel = &(*mixtureViscosityModel);
  }

  const cfd::pressure_velocity::SIMPLE simple(
      setup.solverSettings, /*referenceCell=*/0, activeTurbulenceModel, /*temperature=*/nullptr,
      /*buoyancy=*/nullptr, options.progressCallback, options.cancellationCheck);
  const SIMPLEResult result =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);
  out.simpleResult = result;
  out.status = statusFor(result.status);

  // massFlux is needed by both thermal and species below (P6-PHYS-001:
  // hoisted here, computed at most once, rather than each recomputing its
  // own copy) -- a pure function of setup.mesh/result.velocity/setup.fluid/
  // setup.velocityBoundaries, so sharing it changes nothing about either
  // solve's result.
  std::optional<cfd::fields::SurfaceField> massFlux;
  if ((setup.thermal.has_value() || !setup.species.empty() || setup.multiphase.has_value() ||
       setup.compressible.has_value()) &&
      !anyNonFinite(result)) {
    massFlux = cfd::physics::calculateMassFlux(setup.mesh, result.velocity, setup.fluid,
                                               setup.velocityBoundaries);
  }

  // Same one-way, best-available thermal solve as the pre-P5 CLI (see
  // apps/cli/main.cpp's own comment on this policy -- unchanged here).
  std::optional<cfd::thermal::ThermalResult> thermalResult;
  std::optional<cfd::io::ThermalRunMetadata> thermalMetadata;
  if (setup.thermal.has_value() && massFlux.has_value()) {
    const cfd::thermal::ThermalSolver thermalSolver{};
    thermalResult = thermalSolver.solve(setup.mesh, *setup.initialTemperature, *massFlux,
                                        *setup.thermal, *setup.temperatureBoundaries);
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(),
                                                  thermalStatusName(thermalResult->status),
                                                  thermalResult->converged(),
                                                  thermalResult->iterations,
                                                  thermalResult->finalResidual};
  } else if (setup.thermal.has_value()) {
    thermalMetadata = cfd::io::ThermalRunMetadata{setup.thermal->conductivity(),
                                                  setup.thermal->specificHeat(),
                                                  "NotRun",
                                                  /*converged=*/false,
                                                  /*iterations=*/0,
                                                  /*finalResidual=*/0.0};
  }
  out.thermalResult = thermalResult;

  // P6-PHYS-001: same one-way, best-available policy as thermal above --
  // run only once the SIMPLE result is fully finite, one
  // cfd::species::SpeciesSolver::solve() call per declared species (never
  // a dedicated multi-species solver class, see SpeciesSolver.hpp's own
  // header comment), each independent of the others (this codebase's
  // species are passive/non-reacting -- no cross-species coupling term
  // exists to solve). out.speciesResults stays empty (not partially
  // populated) when massFlux was never computed; speciesMetadataForExport
  // still gets one "NotRun" entry per declared species either way, the
  // same "metadata always reflects the real outcome" policy thermal's
  // own metadata already follows.
  std::vector<cfd::io::SpeciesRunMetadata> speciesMetadataForExport;
  std::vector<cfd::io::NamedScalarField> speciesForExport;
  if (massFlux.has_value()) {
    const cfd::species::SpeciesSolver speciesSolver{};
    for (const cfd::io::SpeciesSetup& speciesSetup : setup.species) {
      const cfd::species::SpeciesResult speciesResult =
          speciesSolver.solve(setup.mesh, speciesSetup.initialConcentration, *massFlux, setup.fluid,
                              speciesSetup.properties, speciesSetup.concentrationBoundaries);
      out.speciesResults.push_back(SpeciesRunResult{speciesSetup.properties.name(), speciesResult});
      speciesMetadataForExport.push_back(cfd::io::SpeciesRunMetadata{
          speciesSetup.properties.name(), speciesSetup.properties.diffusivity(),
          speciesStatusName(speciesResult.status), speciesResult.converged(),
          speciesResult.iterations, speciesResult.finalResidual});
      speciesForExport.emplace_back("concentration_" + speciesSetup.properties.name(),
                                    speciesResult.concentration);
    }
  } else {
    for (const cfd::io::SpeciesSetup& speciesSetup : setup.species) {
      speciesMetadataForExport.push_back(cfd::io::SpeciesRunMetadata{
          speciesSetup.properties.name(), speciesSetup.properties.diffusivity(), "NotRun",
          /*converged=*/false, /*iterations=*/0, /*finalResidual=*/0.0});
    }
  }

  // P6-PHYS-002: same one-way, best-available policy as thermal/species
  // above. One VolumeFractionSolver::step() call (see
  // ProjectRunner.hpp's own header comment: this is the foundation's own
  // "single implicit-Euler step, no outer loop" scope, not an
  // approximation invented here), then mixture density/viscosity fields
  // evaluated at the *final* (post-step) alpha for reporting/export --
  // never fed back into this same SIMPLE solve (which already ran with
  // mu_mix at the *initial* alpha, above).
  std::optional<cfd::io::MultiphaseRunMetadata> multiphaseMetadata;
  std::vector<cfd::io::NamedScalarField> multiphaseForExport;
  if (setup.multiphase.has_value()) {
    const auto& m = *setup.multiphase;
    const auto& p1 = m.system.phase1();
    const auto& p2 = m.system.phase2();
    if (massFlux.has_value()) {
      const cfd::multiphase::VolumeFractionSolver alphaSolver{};
      const cfd::multiphase::VolumeFractionStepResult alphaStep = alphaSolver.step(
          setup.mesh, m.initialAlpha, *massFlux, m.alphaBoundaries, m.transportTimeStep);
      cfd::fields::ScalarField mixtureDensity =
          cfd::multiphase::evaluateMixtureDensityField(setup.mesh, alphaStep.alpha, m.system);
      cfd::fields::ScalarField mixtureViscosity =
          cfd::multiphase::evaluateMixtureViscosityField(setup.mesh, alphaStep.alpha, m.system);
      const Real phase1Volume = cfd::multiphase::phaseVolume(setup.mesh, alphaStep.alpha);

      multiphaseMetadata =
          cfd::io::MultiphaseRunMetadata{p1.name(),
                                         p2.name(),
                                         p1.density(),
                                         p1.viscosity(),
                                         p2.density(),
                                         p2.viscosity(),
                                         volumeFractionStatusName(alphaStep.status),
                                         alphaStep.converged(),
                                         phase1Volume};
      multiphaseForExport.emplace_back("volume_fraction", alphaStep.alpha);
      multiphaseForExport.emplace_back("mixture_density", mixtureDensity);
      multiphaseForExport.emplace_back("mixture_viscosity", mixtureViscosity);
      out.multiphaseResult = MultiphaseRunResult{alphaStep, std::move(mixtureDensity),
                                                 std::move(mixtureViscosity), phase1Volume};
    } else {
      multiphaseMetadata = cfd::io::MultiphaseRunMetadata{
          p1.name(), p2.name(), p1.density(), p1.viscosity(), p2.density(), p2.viscosity(),
          "NotRun",  false,     0.0};
    }
  }

  // P6-PHYS-003 (extended by P12-COMP-002): two modes, see
  // ProjectRunner.hpp's own header comment. Runs whenever the SIMPLE
  // result is fully finite -- same gate as massFlux above (compressible
  // needs velocity, not massFlux itself, but shares the same "only a
  // usable flow" precondition).
  std::optional<cfd::io::CompressibleRunMetadata> compressibleMetadata;
  std::vector<cfd::io::NamedScalarField> compressibleForExport;
  if (setup.compressible.has_value()) {
    const auto& c = *setup.compressible;
    // thermal_coupled requires "thermal" (PhysicsConfigParser.cpp's own
    // cross-block check), and both compressible and thermal share the
    // identical `!anyNonFinite(result)` gate above, so thermalResult is
    // guaranteed to have a value here whenever c.thermalCoupled is true
    // and this branch is reached -- checked anyway (defensive, not load-
    // bearing) rather than assumed.
    const bool canEvaluate =
        !anyNonFinite(result) && (!c.thermalCoupled || thermalResult.has_value());
    if (canEvaluate) {
      const cfd::Index n = setup.mesh.numberOfCells();
      cfd::fields::ScalarField temperatureField(n);
      if (c.thermalCoupled) {
        for (cfd::Index i = 0; i < n; ++i) temperatureField[i] = thermalResult->temperature[i];
      } else {
        for (cfd::Index i = 0; i < n; ++i) temperatureField[i] = *c.temperature;
      }
      // P12-COMP-001: boundary faces now get a real EOS-evaluated density
      // at their own boundary pressure/temperature (via
      // setup.pressureBoundaries and, when thermal-coupled,
      // *setup.temperatureBoundaries), superseding the previous owner-
      // cell-reuse simplification -- see CompressibleMassFlux.hpp's own
      // header comment. Shared by both modes below.
      const cfd::boundary::BoundaryConditionSet* temperatureBoundariesPtr =
          c.thermalCoupled ? &(*setup.temperatureBoundaries) : nullptr;

      cfd::fields::ScalarField density(n);
      cfd::fields::ScalarField pressureAbsolute(n);
      cfd::fields::ScalarField mach(n);
      Real machMax = 0.0;
      cfd::fields::SurfaceField compressibleMassFlux(setup.mesh.numberOfFaces());
      std::string statusName;

      if (c.coupled) {
        // P12-COMP-002: genuinely coupled solve, warm-started from the
        // incompressible SIMPLE result already computed above -- see
        // CompressibleSIMPLE.hpp's own header comment. Initial density
        // guess is the EOS evaluated at the warm-start's own gauge
        // pressure (a reasonable starting iterate, not itself part of the
        // converged answer).
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + result.pressure[i];
        const cfd::fields::ScalarField initialDensity = cfd::compressible::evaluateDensityField(
            setup.mesh, pressureAbsolute, temperatureField, c.thermodynamics);

        cfd::compressible::CompressibleSIMPLESettings compressibleSettings;
        compressibleSettings.maxIterations = setup.solverSettings.maxIterations;
        compressibleSettings.velocityRelaxation = setup.solverSettings.velocityRelaxation;
        compressibleSettings.pressureRelaxation = setup.solverSettings.pressureRelaxation;
        compressibleSettings.velocityTolerance = setup.solverSettings.velocityTolerance;
        compressibleSettings.pressureTolerance = setup.solverSettings.pressureTolerance;
        compressibleSettings.continuityTolerance = setup.solverSettings.continuityTolerance;
        compressibleSettings.momentumSolver = setup.solverSettings.momentumSolver;
        compressibleSettings.pressureSolver = setup.solverSettings.pressureSolver;
        // compressibleSettings.pseudoTimeStep is left at its own default
        // (1.0) -- no case-format exposure for it in this phase (see
        // ROADMAP.md's P12-COMP-002 scope note).

        const cfd::compressible::CompressibleSIMPLE compressibleSimple(
            compressibleSettings, c.thermodynamics, c.referencePressure, /*referenceCell=*/0);
        cfd::compressible::CompressibleSIMPLEResult coupled = compressibleSimple.solve(
            setup.mesh, setup.fluid.dynamicViscosity(), setup.velocityBoundaries,
            setup.pressureBoundaries, temperatureField, temperatureBoundariesPtr, result.velocity,
            result.pressure, initialDensity);

        density = coupled.density;
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + coupled.pressure[i];
        for (cfd::Index i = 0; i < n; ++i) {
          const Real speed = magnitude(coupled.velocity[i]);
          const Real soundSpeed = c.thermodynamics.speedOfSound(temperatureField[i]);
          mach[i] = cfd::compressible::machNumber(speed, soundSpeed);
          machMax = std::max(machMax, mach[i]);
        }
        compressibleMassFlux = coupled.massFlux;
        statusName = compressibleSimpleStatusName(coupled.status);
        // The coupled solve's own convergence is this run's authoritative
        // status -- see ProjectRunner.hpp's own header comment on
        // compressibleSimpleResult.
        out.status = statusFor(coupled.status);
        out.compressibleSimpleResult = std::move(coupled);
      } else {
        // The post-hoc pass: a *reinterpretation* of the already-converged
        // incompressible SIMPLE result, never a second flow solve -- see
        // ProjectRunner.hpp's own header comment for the exact recipe.
        for (cfd::Index i = 0; i < n; ++i)
          pressureAbsolute[i] = c.referencePressure + result.pressure[i];

        density = cfd::compressible::evaluateDensityField(setup.mesh, pressureAbsolute,
                                                           temperatureField, c.thermodynamics);

        for (cfd::Index i = 0; i < n; ++i) {
          const Real speed = magnitude(result.velocity[i]);
          const Real soundSpeed = c.thermodynamics.speedOfSound(temperatureField[i]);
          mach[i] = cfd::compressible::machNumber(speed, soundSpeed);
          machMax = std::max(machMax, mach[i]);
        }

        compressibleMassFlux = cfd::compressible::calculateCompressibleMassFlux(
            setup.mesh, result.velocity, density, setup.velocityBoundaries, result.pressure,
            setup.pressureBoundaries, c.referencePressure, c.thermodynamics, temperatureField,
            temperatureBoundariesPtr);
        statusName = "Evaluated";
      }

      const cfd::physics::ContinuityResult continuity =
          cfd::physics::evaluateContinuity(setup.mesh, compressibleMassFlux);

      compressibleMetadata =
          cfd::io::CompressibleRunMetadata{c.thermodynamics.gasConstant(),
                                           c.thermodynamics.specificHeatPressure(),
                                           c.referencePressure,
                                           c.thermalCoupled,
                                           c.coupled,
                                           statusName,
                                           machMax,
                                           std::abs(continuity.globalNetFlux)};
      compressibleForExport.emplace_back("density", density);
      compressibleForExport.emplace_back("pressure_absolute", pressureAbsolute);
      compressibleForExport.emplace_back("compressible_temperature", temperatureField);
      compressibleForExport.emplace_back("mach_number", mach);
      out.compressibleResult = CompressibleRunResult{std::move(density),
                                                     std::move(pressureAbsolute),
                                                     std::move(temperatureField),
                                                     std::move(mach),
                                                     compressibleMassFlux,
                                                     continuity,
                                                     machMax};
    } else {
      compressibleMetadata =
          cfd::io::CompressibleRunMetadata{c.thermodynamics.gasConstant(),
                                           c.thermodynamics.specificHeatPressure(),
                                           c.referencePressure,
                                           c.thermalCoupled,
                                           c.coupled,
                                           "NotRun",
                                           0.0,
                                           0.0};
    }
  }

  const cfd::io::RunMetadata exportMetadata{
      caseDefinition->caseConfig.name, caseDefinition->physics.density,
      caseDefinition->physics.dynamicViscosity, caseDefinition->solver.type};
  try {
    const std::optional<cfd::fields::ScalarField> temperatureForExport =
        thermalResult.has_value() ? std::optional(thermalResult->temperature) : std::nullopt;
    // P6-PHYS-001 (generalized by P6-PHYS-002/003): every species/
    // multiphase/compressible field combined into one flat list -- see
    // ResultExporter.hpp's own header comment on why this is one
    // parameter, not one per physics module.
    std::vector<cfd::io::NamedScalarField> extraFieldsForExport = std::move(speciesForExport);
    extraFieldsForExport.insert(extraFieldsForExport.end(),
                                std::make_move_iterator(multiphaseForExport.begin()),
                                std::make_move_iterator(multiphaseForExport.end()));
    extraFieldsForExport.insert(extraFieldsForExport.end(),
                                std::make_move_iterator(compressibleForExport.begin()),
                                std::make_move_iterator(compressibleForExport.end()));
    out.exportSummary = cfd::io::ResultExporter::write(
        caseDirectory / "results", setup.mesh, result, exportMetadata, temperatureForExport,
        thermalMetadata, extraFieldsForExport, speciesMetadataForExport, multiphaseMetadata,
        compressibleMetadata);
  } catch (const cfd::Error& e) {
    out.status = ProjectRunStatus::ApplicationError;
    out.errorMessage = e.what();
  }
  return out;
}

}  // namespace cfd::app
