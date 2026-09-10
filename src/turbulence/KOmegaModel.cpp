#include "cfd/turbulence/KOmegaModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/KOmegaEquation.hpp"
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

void validateCoefficients(const KOmegaCoefficients& c) {
  const std::array<Real, 5> values = {c.betaStar, c.alpha, c.beta, c.sigmaK, c.sigmaOmega};
  for (const Real v : values) {
    if (!std::isfinite(v) || !(v > 0.0)) {
      throw InvalidArgumentError(
          "KOmegaModel: every KOmegaCoefficients entry must be finite and > 0");
    }
  }
}

// mu_t = rho*k/omega -- P2-TURB-005 section 5. omegaFloor guards the
// denominator only (see KOmegaConfig's own header comment); kFloor is
// accepted for interface symmetry with KEpsilonModel's equivalent
// helper but is not needed by this particular formula (k appears only
// in the numerator here, never divided by).
ScalarField computeTurbulentViscosityField(const Mesh& mesh, Real density, const ScalarField& k,
                                           const ScalarField& omega, Real omegaFloor) {
  const Index n = mesh.numberOfCells();
  ScalarField muT(n);
  for (Index i = 0; i < n; ++i) {
    const Real omegaSafe = std::max(omega[i], omegaFloor);
    muT[i] = density * k[i] / omegaSafe;
  }
  return muT;
}

}  // namespace

KOmegaModel::KOmegaModel(const Mesh& mesh, const FluidProperties& fluid,
                         const BoundaryConditionSet& velocityBoundaries,
                         const BoundaryConditionSet& kBoundaries,
                         const BoundaryConditionSet& omegaBoundaries, KOmegaConfig config)
    : mesh_(mesh),
      fluid_(fluid),
      velocityBoundaries_(velocityBoundaries),
      kBoundaries_(kBoundaries),
      omegaBoundaries_(omegaBoundaries),
      config_(std::move(config)),
      k_(mesh.numberOfCells(), config_.initialK),
      omega_(mesh.numberOfCells(), config_.initialOmega),
      turbulentViscosity_(mesh.numberOfCells(), 0.0) {
  validateCoefficients(config_.coefficients);
  if (!std::isfinite(config_.initialK) || !(config_.initialK > 0.0)) {
    throw InvalidArgumentError("KOmegaModel: initialK must be finite and > 0");
  }
  if (!std::isfinite(config_.initialOmega) || !(config_.initialOmega > 0.0)) {
    throw InvalidArgumentError("KOmegaModel: initialOmega must be finite and > 0");
  }
  turbulentViscosity_ =
      computeTurbulentViscosityField(mesh_, fluid_.density(), k_, omega_, config_.omegaFloor);
  validateTurbulentViscosityField(mesh_, turbulentViscosity_);
}

std::string_view KOmegaModel::name() const noexcept { return "kOmega"; }

const ScalarField& KOmegaModel::turbulentViscosity() const { return turbulentViscosity_; }

const ScalarField& KOmegaModel::k() const noexcept { return k_; }

const ScalarField& KOmegaModel::omega() const noexcept { return omega_; }

void KOmegaModel::correct(const Mesh& mesh, const VectorField& velocity,
                          const ScalarField& /*pressure*/) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n) {
    throw InvalidArgumentError(
        "KOmegaModel::correct: velocity size does not match mesh cell "
        "count");
  }

  // Everything below reads only the pre-update k_/omega_/
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

  // Gamma_k = mu + sigma_k*mu_t, Gamma_omega = mu + sigma_omega*mu_t --
  // the *linear* form (P2-TURB-005 sections 4/17), NOT k-epsilon's
  // mu + mu_t/sigma. Uses the shared, independently-tested
  // computeLinearEffectiveDiffusivity (KOmegaEquation.hpp), not
  // KEpsilonEquation.hpp's computeEffectiveDiffusivity -- the two
  // conventions are deliberately kept as two separate, separately-named
  // functions (see KOmegaEquation.hpp's own header comment).
  const ScalarField gammaK = computeLinearEffectiveDiffusivity(mu, turbulentViscosity_, c.sigmaK);
  const ScalarField gammaOmega =
      computeLinearEffectiveDiffusivity(mu, turbulentViscosity_, c.sigmaOmega);

  // k equation: Su = P_k (explicit), Sp = -beta_star*rho*omega
  // (implicit destruction -- Sp*k_new -> -beta_star*rho*k_new*omega_old,
  // which is exactly -beta_star*rho*k*omega at self-consistency,
  // P2-TURB-005 section 12). Unlike k-epsilon's k-equation destruction
  // (-rho*epsilon, made implicit via an artificial epsilon/k factor),
  // k-omega's own destruction term -beta_star*rho*k*omega is already
  // exactly linear in k, so Sp needs no k_safe floor at all here.
  ScalarField suK(n);
  ScalarField spK(n);
  for (Index i = 0; i < n; ++i) {
    suK[i] = production[i];
    spK[i] = -c.betaStar * rho * omega_[i];
  }

  // omega equation: Su = alpha*(omega/k_safe)*P_k (explicit production,
  // section 14 -- k_safe used only for this denominator, never to alter
  // the physical field itself), Sp = -beta*rho*omega (implicit
  // destruction -- Sp*omega_new -> -beta*rho*omega_old*omega_new, which
  // is exactly -beta*rho*omega^2 at self-consistency, section 13).
  ScalarField suOmega(n);
  ScalarField spOmega(n);
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k_[i], config_.kFloor);
    suOmega[i] = c.alpha * (omega_[i] / kSafe) * production[i];
    spOmega[i] = -c.beta * rho * omega_[i];
  }

  // Both solves are attempted before either member field is touched --
  // if either throws, k_/omega_/turbulentViscosity_ stay exactly as
  // they were on entry (see this class's own header comment). Not
  // `const`: both are moved from below once the whole update has
  // succeeded.
  ScalarField newK =
      solveRelaxedScalarTransport(mesh, k_, massFlux, gammaK, kBoundaries_, suK, spK,
                                  config_.kRelaxation, config_.kFloor, config_.kSolver);
  ScalarField newOmega = solveRelaxedScalarTransport(
      mesh, omega_, massFlux, gammaOmega, omegaBoundaries_, suOmega, spOmega,
      config_.omegaRelaxation, config_.omegaFloor, config_.omegaSolver);

  ScalarField newMuT =
      computeTurbulentViscosityField(mesh, rho, newK, newOmega, config_.omegaFloor);
  validateTurbulentViscosityField(mesh, newMuT);

  // Section 23-24: computed from the pre-update (k_/omega_) vs. freshly-
  // solved (newK/newOmega) fields, before either member is overwritten
  // below.
  Real maxChange = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxChange = std::max(maxChange, std::abs(newK[i] - k_[i]));
    maxChange = std::max(maxChange, std::abs(newOmega[i] - omega_[i]));
  }
  lastConvergenceResidual_ = maxChange;

  k_ = std::move(newK);
  omega_ = std::move(newOmega);
  turbulentViscosity_ = std::move(newMuT);
}

std::optional<Real> KOmegaModel::convergenceResidual() const { return lastConvergenceResidual_; }

}  // namespace cfd::turbulence
