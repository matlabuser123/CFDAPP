#include "cfd/turbulence/KEpsilonModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

namespace cfd::turbulence {

using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::computeVelocityGradient;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

void validateCoefficients(const KEpsilonCoefficients& c) {
  const std::array<Real, 5> values = {c.cMu, c.c1Epsilon, c.c2Epsilon, c.sigmaK, c.sigmaEpsilon};
  for (const Real v : values) {
    if (!std::isfinite(v) || !(v > 0.0)) {
      throw InvalidArgumentError(
          "KEpsilonModel: every KEpsilonCoefficients entry must be finite "
          "and > 0");
    }
  }
}

}  // namespace

KEpsilonModel::KEpsilonModel(const Mesh& mesh, const FluidProperties& fluid,
                             const BoundaryConditionSet& velocityBoundaries,
                             const BoundaryConditionSet& kBoundaries,
                             const BoundaryConditionSet& epsilonBoundaries, KEpsilonConfig config)
    : mesh_(mesh),
      fluid_(fluid),
      velocityBoundaries_(velocityBoundaries),
      kBoundaries_(kBoundaries),
      epsilonBoundaries_(epsilonBoundaries),
      config_(std::move(config)),
      k_(mesh.numberOfCells(), config_.initialK),
      epsilon_(mesh.numberOfCells(), config_.initialEpsilon),
      turbulentViscosity_(mesh.numberOfCells(), 0.0) {
  // P12-MESH-006: 2D production term (du/dx, du/dy, dv/dx, dv/dy) only.
  cfd::mesh::requireTwoDimensional(mesh, "KEpsilonModel");
  validateCoefficients(config_.coefficients);
  if (!std::isfinite(config_.initialK) || !(config_.initialK > 0.0)) {
    throw InvalidArgumentError("KEpsilonModel: initialK must be finite and > 0");
  }
  if (!std::isfinite(config_.initialEpsilon) || !(config_.initialEpsilon > 0.0)) {
    throw InvalidArgumentError("KEpsilonModel: initialEpsilon must be finite and > 0");
  }
  turbulentViscosity_ =
      computeTurbulentViscosityField(mesh_, fluid_.density(), config_.coefficients.cMu, k_,
                                     epsilon_, config_.kFloor, config_.epsilonFloor);
  validateTurbulentViscosityField(mesh_, turbulentViscosity_);

  // P16. Wall faces are geometry: collected once, never re-derived per
  // iteration. Also the point at which a case asking for wall functions on a
  // mesh with no wall fails, rather than silently running without them.
  wallViscosity_ = cfd::fields::SurfaceField(mesh.numberOfFaces(), 0.0);
  yPlus_ = ScalarField(mesh.numberOfCells(), 0.0);
  wallShearStress_ = ScalarField(mesh.numberOfCells(), 0.0);
  if (config_.wallFunctions) {
    const auto& c = config_.wallFunctionCoefficients;
    if (c.cMu != config_.coefficients.cMu) {
      throw InvalidArgumentError(
          "KEpsilonModel: wallFunctionCoefficients.cMu must equal coefficients.cMu -- the wall "
          "function forms u_tau* = C_mu^(1/4) sqrt(k) from the same C_mu the model uses for "
          "mu_t, and two different values would describe two different models");
    }
    wallFaces_ = collectWallFaces(mesh, velocityBoundaries);
  }

  // P16-GPU-TURB Stage 7D -- the backend decision, made ONCE.
  //
  // Selected by config_.kSolver.backend, the same field every other backend
  // decision in this project reads. Deciding here rather than per correction
  // means a solve cannot change backend halfway through, and it means the
  // refusal reason is available before the first iteration rather than after
  // it.
  //
  // A REFUSAL IS NOT A SILENT FALLBACK. If the GPU path was requested and
  // cannot be built, gpuActive_ stays false, the CPU path runs unchanged, and
  // gpuUnsupportedReason() says why -- so a run that asked for GPU and got CPU
  // numbers is diagnosable from the model rather than from the timings.
  if (config_.kSolver.backend == cfd::algebra::LinearSolverBackend::GPU &&
      cfd::gpu::GpuKEpsilonCorrection::available()) {
    gpuActive_ = gpuCorrection_.prepare(mesh, kBoundaries, epsilonBoundaries, k_, epsilon_,
                                        turbulentViscosity_);
  }
}

std::string_view KEpsilonModel::name() const noexcept { return "kEpsilon"; }

const ScalarField& KEpsilonModel::turbulentViscosity() const { return turbulentViscosity_; }

const ScalarField& KEpsilonModel::k() const noexcept { return k_; }

const ScalarField& KEpsilonModel::epsilon() const noexcept { return epsilon_; }

void KEpsilonModel::correct(const Mesh& mesh, const VectorField& velocity,
                            const ScalarField& pressure) {
  if (gpuActive_) {
    correctOnDevice(mesh, velocity);
    return;
  }
  correctOnHost(mesh, velocity, pressure);
}

// P16-GPU-TURB Stage 7D. The GPU branch.
//
// THE WALL TREATMENT IS EVALUATED HERE, ON THE HOST, exactly as the CPU branch
// does and from the same pre-update k -- the P16 scope decision keeps it on the
// CPU. What crosses to the device is its COMPACT output: O(wall cells) indices
// and values, never a field. The wall diagnostics (wallViscosity_, yPlus_,
// wallShearStress_, the counters) are published identically on both branches,
// so a GPU run exports the same y+ a user would plot from a CPU run.
//
// Everything after that is the qualified device correction, which owns the
// lag semantics and the atomic publication.
void KEpsilonModel::correctOnDevice(const Mesh& mesh, const VectorField& velocity) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n) {
    throw InvalidArgumentError(
        "KEpsilonModel::correct: velocity size does not match mesh cell count");
  }

  std::vector<Index> wallCells;
  std::vector<Real> wallCellProduction;
  std::vector<Real> wallCellDissipation;
  if (config_.wallFunctions) {
    const auto treatment =
        computeWallTreatment(mesh, wallFaces_, velocity, k_, fluid_.density(),
                             fluid_.dynamicViscosity(), config_.wallFunctionCoefficients);
    wallCells = treatment.wallCells;
    wallCellProduction = treatment.wallCellProduction;
    wallCellDissipation = treatment.wallCellDissipation;

    wallViscosity_ = treatment.wallViscosity;
    yPlus_ = treatment.yPlus;
    wallShearStress_ = treatment.wallShearStress;
    wallFacesOutsideValidBand_ = treatment.facesOutsideValidBand;
    wallFacesWithFlooredViscosity_ = treatment.facesWithFlooredViscosity;
    minimumWallYPlus_ = treatment.minimumYPlus;
    maximumWallYPlus_ = treatment.maximumYPlus;
  }

  cfd::gpu::GpuKEpsilonWallInputs wallInputs;
  wallInputs.wallCells = &wallCells;
  wallInputs.wallCellProduction = &wallCellProduction;
  wallInputs.wallCellDissipation = &wallCellDissipation;

  cfd::gpu::GpuKEpsilonResult result;
  gpuCorrection_.correct(mesh, velocity, velocityBoundaries_, fluid_, config_, wallInputs, result);
  validateTurbulentViscosityField(mesh, result.turbulentViscosity);

  // Publication, still atomic and still last. The device already refused to
  // publish its own state unless both solves succeeded; this mirrors that on
  // the host side.
  lastConvergenceResidual_ = result.convergenceResidual;
  k_ = std::move(result.k);
  epsilon_ = std::move(result.epsilon);
  turbulentViscosity_ = std::move(result.turbulentViscosity);
}

// P16 Step 4 -- the resident branch, driven from device state.
//
// The difference from correctOnDevice is not the physics; it is WHERE the
// velocity comes from. correctOnDevice evaluates the wall treatment on the host
// from the caller's velocity field, which a resident SIMPLE loop leaves frozen
// at iteration 0. This one hands the discretization's own resident U(N-1) to
// the qualified resident correction, which gathers the O(N_wall) wall-owner
// values off the device and runs the SAME host wall law on them.
//
// Publication is identical and still atomic and still last: nothing here
// writes to a member until both solves have succeeded.
bool KEpsilonModel::correctResidentGpu(const Mesh& mesh,
                                       cfd::gpu::GpuSimpleDiscretization& discretization) {
  if (!gpuActive_) return false;

  cfd::turbulence::WallTreatment treatment;
  cfd::gpu::GpuKEpsilonResult result;
  if (!gpuCorrection_.correctFromResidentDiscretization(
          discretization, mesh, fluid_, config_,
          config_.wallFunctions ? wallFaces_ : std::vector<WallFace>{}, treatment, result)) {
    return false;
  }
  validateTurbulentViscosityField(mesh, result.turbulentViscosity);

  if (config_.wallFunctions) {
    wallViscosity_ = treatment.wallViscosity;
    yPlus_ = treatment.yPlus;
    wallShearStress_ = treatment.wallShearStress;
    wallFacesOutsideValidBand_ = treatment.facesOutsideValidBand;
    wallFacesWithFlooredViscosity_ = treatment.facesWithFlooredViscosity;
    minimumWallYPlus_ = treatment.minimumYPlus;
    maximumWallYPlus_ = treatment.maximumYPlus;
  }
  lastConvergenceResidual_ = result.convergenceResidual;
  k_ = std::move(result.k);
  epsilon_ = std::move(result.epsilon);
  turbulentViscosity_ = std::move(result.turbulentViscosity);
  return true;
}

void KEpsilonModel::correctOnHost(const Mesh& mesh, const VectorField& velocity,
                                  const ScalarField& /*pressure*/) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n) {
    throw InvalidArgumentError(
        "KEpsilonModel::correct: velocity size does not match mesh cell "
        "count");
  }

  // Everything below reads only the pre-update k_/epsilon_/
  // turbulentViscosity_ (this call's "start of correct()" state) -- see
  // this class's own header comment on why both equations are lagged
  // against each other this way, and are only applied to the member
  // fields at the very end, once both solves have succeeded.
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid_, velocityBoundaries_);
  const auto velocityGradient = computeVelocityGradient(mesh, velocity, velocityBoundaries_);
  ScalarField production = computeTurbulentProduction(mesh, turbulentViscosity_, velocityGradient);

  // P16 -- the wall treatment, evaluated from this iteration's velocity and the
  // pre-update k (the same lagged state everything else in this function reads).
  //
  // Three effects, and a treatment doing fewer than three is not a wall
  // function:
  //   * production in the wall cell is REPLACED by the equilibrium expression,
  //     because mu_t*S^2 from a cell-centre gradient badly under-resolves dU/dy
  //     across a cell that spans the log layer -- exactly where production
  //     peaks;
  //   * epsilon is IMPOSED there (below), not transported;
  //   * the wall shear reaches momentum through wallViscosity_.
  std::vector<ImposedCell> imposedEpsilon;
  if (config_.wallFunctions) {
    const auto treatment =
        computeWallTreatment(mesh, wallFaces_, velocity, k_, fluid_.density(),
                             fluid_.dynamicViscosity(), config_.wallFunctionCoefficients);

    for (std::size_t i = 0; i < treatment.wallCells.size(); ++i) {
      production[treatment.wallCells[i]] = treatment.wallCellProduction[i];
      imposedEpsilon.push_back(
          ImposedCell{treatment.wallCells[i], treatment.wallCellDissipation[i]});
    }

    wallViscosity_ = treatment.wallViscosity;
    yPlus_ = treatment.yPlus;
    wallShearStress_ = treatment.wallShearStress;
    wallFacesOutsideValidBand_ = treatment.facesOutsideValidBand;
    wallFacesWithFlooredViscosity_ = treatment.facesWithFlooredViscosity;
    minimumWallYPlus_ = treatment.minimumYPlus;
    maximumWallYPlus_ = treatment.maximumYPlus;
  }

  const Real rho = fluid_.density();
  const Real mu = fluid_.dynamicViscosity();
  const auto& c = config_.coefficients;

  // Gamma_k = mu + mu_t/sigma_k, Gamma_epsilon = mu + mu_t/sigma_epsilon
  // -- NOT (mu+mu_t)/sigma (P2-TURB-004 sections 13-14). Uses the shared,
  // independently-tested computeEffectiveDiffusivity (KEpsilonEquation.
  // hpp) rather than an inline formula here.
  const ScalarField gammaK = computeEffectiveDiffusivity(mu, turbulentViscosity_, c.sigmaK);
  const ScalarField gammaEpsilon =
      computeEffectiveDiffusivity(mu, turbulentViscosity_, c.sigmaEpsilon);

  // k equation: Su = P_k (explicit), Sp = -rho*epsilon/k (implicit
  // destruction -- Sp*k_new -> -rho*epsilon_old at self-consistency,
  // P2-TURB-004 section 12).
  // P16-GPU-TURB: the loop moved to computeKSourceTerms (KEpsilonEquation.hpp)
  // so the device port can be compared against the expression production
  // evaluates, not a re-typed copy of it. Same values, same order.
  const KSourceTerms kSources = computeKSourceTerms(rho, k_, epsilon_, production, config_.kFloor);
  const ScalarField& suK = kSources.Su;
  const ScalarField& spK = kSources.Sp;

  // epsilon equation: Su = C1eps*(epsilon/k)*P_k (explicit production),
  // Sp = -C2eps*rho*epsilon/k (implicit destruction -- Sp*epsilon_new ->
  // -C2eps*rho*epsilon_old^2/k_old at self-consistency).
  // P16-GPU-TURB: moved to computeEpsilonSourceTerms for the same reason as the
  // k pair. Same values, same order, same single epsOverK.
  const EpsilonSourceTerms epsilonSources = computeEpsilonSourceTerms(
      rho, c.c1Epsilon, c.c2Epsilon, k_, epsilon_, production, config_.kFloor);
  const ScalarField& suEpsilon = epsilonSources.Su;
  const ScalarField& spEpsilon = epsilonSources.Sp;

  // Both solves are attempted before either member field is touched --
  // if either throws, k_/epsilon_/turbulentViscosity_ stay exactly as
  // they were on entry (see this class's own header comment).
  // Not `const`: both are moved from below (k_ = std::move(newK), ...)
  // once the whole update has succeeded -- a `const` local would compile
  // but silently degrade that into a copy instead of a move.
  // P14 MIGRATION. Assembly only: relaxation, fixRow, the solve, the floor
  // and the imposed-value re-assertion all stay in
  // solveRelaxedScalarTransport. Bitwise equivalence of the two routes is
  // gated by test_p14_migration_equivalence.cpp.
  ScalarField newK = solveRelaxedScalarTransport(mesh, k_, massFlux, gammaK, kBoundaries_, suK, spK,
                                                 config_.kRelaxation, config_.kFloor,
                                                 config_.kSolver, config_.nonOrthogonal);
  // Only epsilon is imposed. k keeps its own boundary condition and responds to
  // the modified production -- imposing both over-constrains the wall cell.
  // P14 MIGRATION, equation 1 of 6. epsilon goes first deliberately: it is the
  // only turbulence equation that uses imposed cells (the wall-function
  // epsilon), so the migration exercises that path immediately rather than
  // deferring the risk to the end.
  //
  // Only the ASSEMBLY moves. Under-relaxation, fixRow, the solve, the floor and
  // the imposed-value re-assertion stay in solveRelaxedScalarTransport,
  // untouched. Bitwise equivalence of the two routes is gated by
  // test_p14_migration_equivalence.cpp, including with imposed cells and under
  // non-orthogonal correction, with a non-vacuity control.
  ScalarField newEpsilon = solveRelaxedScalarTransport(
      mesh, epsilon_, massFlux, gammaEpsilon, epsilonBoundaries_, suEpsilon, spEpsilon,
      config_.epsilonRelaxation, config_.epsilonFloor, config_.epsilonSolver, config_.nonOrthogonal,
      imposedEpsilon);

  ScalarField newMuT = computeTurbulentViscosityField(mesh, rho, c.cMu, newK, newEpsilon,
                                                      config_.kFloor, config_.epsilonFloor);
  validateTurbulentViscosityField(mesh, newMuT);

  // P2-TURB-004 section 23-24: computed from the pre-update (k_/
  // epsilon_) vs. freshly-solved (newK/newEpsilon) fields, before either
  // member is overwritten below.
  Real maxChange = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxChange = std::max(maxChange, std::abs(newK[i] - k_[i]));
    maxChange = std::max(maxChange, std::abs(newEpsilon[i] - epsilon_[i]));
  }
  lastConvergenceResidual_ = maxChange;

  k_ = std::move(newK);
  epsilon_ = std::move(newEpsilon);
  turbulentViscosity_ = std::move(newMuT);
}

std::optional<Real> KEpsilonModel::convergenceResidual() const { return lastConvergenceResidual_; }

const cfd::fields::SurfaceField* KEpsilonModel::wallViscosity() const {
  // nullptr, not an all-zero field, when wall functions are off: a caller
  // threading this into momentum then provably changes nothing.
  return config_.wallFunctions ? &wallViscosity_ : nullptr;
}

}  // namespace cfd::turbulence
