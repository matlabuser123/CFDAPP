#include "cfd/turbulence/KEpsilonModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"

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

ScalarField computeTurbulentViscosityField(const Mesh& mesh, Real density, Real cMu,
                                           const ScalarField& k, const ScalarField& epsilon,
                                           Real kFloor, Real epsilonFloor) {
  const Index n = mesh.numberOfCells();
  ScalarField muT(n);
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k[i], kFloor);
    const Real epsSafe = std::max(epsilon[i], epsilonFloor);
    muT[i] = density * cMu * kSafe * kSafe / epsSafe;
  }
  return muT;
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
}

std::string_view KEpsilonModel::name() const noexcept { return "kEpsilon"; }

const ScalarField& KEpsilonModel::turbulentViscosity() const { return turbulentViscosity_; }

const ScalarField& KEpsilonModel::k() const noexcept { return k_; }

const ScalarField& KEpsilonModel::epsilon() const noexcept { return epsilon_; }

void KEpsilonModel::correct(const Mesh& mesh, const VectorField& velocity,
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
  const ScalarField production =
      computeTurbulentProduction(mesh, turbulentViscosity_, velocityGradient);

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
  ScalarField suK(n);
  ScalarField spK(n);
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k_[i], config_.kFloor);
    suK[i] = production[i];
    spK[i] = -rho * epsilon_[i] / kSafe;
  }

  // epsilon equation: Su = C1eps*(epsilon/k)*P_k (explicit production),
  // Sp = -C2eps*rho*epsilon/k (implicit destruction -- Sp*epsilon_new ->
  // -C2eps*rho*epsilon_old^2/k_old at self-consistency).
  ScalarField suEpsilon(n);
  ScalarField spEpsilon(n);
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k_[i], config_.kFloor);
    const Real epsOverK = epsilon_[i] / kSafe;
    suEpsilon[i] = c.c1Epsilon * epsOverK * production[i];
    spEpsilon[i] = -c.c2Epsilon * rho * epsOverK;
  }

  // Both solves are attempted before either member field is touched --
  // if either throws, k_/epsilon_/turbulentViscosity_ stay exactly as
  // they were on entry (see this class's own header comment).
  // Not `const`: both are moved from below (k_ = std::move(newK), ...)
  // once the whole update has succeeded -- a `const` local would compile
  // but silently degrade that into a copy instead of a move.
  ScalarField newK =
      solveRelaxedScalarTransport(mesh, k_, massFlux, gammaK, kBoundaries_, suK, spK,
                                  config_.kRelaxation, config_.kFloor, config_.kSolver);
  ScalarField newEpsilon = solveRelaxedScalarTransport(
      mesh, epsilon_, massFlux, gammaEpsilon, epsilonBoundaries_, suEpsilon, spEpsilon,
      config_.epsilonRelaxation, config_.epsilonFloor, config_.epsilonSolver);

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

}  // namespace cfd::turbulence
