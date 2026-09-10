#include "cfd/turbulence/SSTModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/SSTEquation.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"
#include "cfd/turbulence/WallDistance.hpp"

namespace cfd::turbulence {

using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::computeVelocityGradient;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

void validateCoefficients(const SSTCoefficients& c) {
  const std::array<Real, 9> values = {c.betaStar, c.a1,          c.kappa,
                                      c.sigmaK1,  c.sigmaOmega1, c.beta1,
                                      c.sigmaK2,  c.sigmaOmega2, c.beta2};
  for (const Real v : values) {
    if (!std::isfinite(v) || !(v > 0.0)) {
      throw InvalidArgumentError("SSTModel: every SSTCoefficients entry must be finite and > 0");
    }
  }
  if (!std::isfinite(c.productionLimiterFactor) || !(c.productionLimiterFactor > 0.0)) {
    throw InvalidArgumentError(
        "SSTModel: SSTCoefficients::productionLimiterFactor must be finite and > 0");
  }
}

}  // namespace

SSTModel::SSTModel(const Mesh& mesh, const FluidProperties& fluid,
                   const BoundaryConditionSet& velocityBoundaries,
                   const BoundaryConditionSet& kBoundaries,
                   const BoundaryConditionSet& omegaBoundaries, SSTConfig config)
    : mesh_(mesh),
      fluid_(fluid),
      velocityBoundaries_(velocityBoundaries),
      kBoundaries_(kBoundaries),
      omegaBoundaries_(omegaBoundaries),
      config_(std::move(config)),
      wallDistance_(computeWallDistance(mesh, velocityBoundaries)),
      k_(mesh.numberOfCells(), config_.initialK),
      omega_(mesh.numberOfCells(), config_.initialOmega),
      turbulentViscosity_(mesh.numberOfCells(), 0.0),
      f1_(mesh.numberOfCells(), 0.0),
      f2_(mesh.numberOfCells(), 0.0) {
  validateCoefficients(config_.coefficients);
  if (!std::isfinite(config_.initialK) || !(config_.initialK > 0.0)) {
    throw InvalidArgumentError("SSTModel: initialK must be finite and > 0");
  }
  if (!std::isfinite(config_.initialOmega) || !(config_.initialOmega > 0.0)) {
    throw InvalidArgumentError("SSTModel: initialOmega must be finite and > 0");
  }

  // Construction-time mu_t/F1/F2: S=0 (no velocity field yet), so
  // computeSSTTurbulentViscosity's denominator reduces to exactly
  // a1*omega (since strainMagnitude*F2 = 0 <= a1*omega for any positive
  // omega), giving mu_t = rho*a1*k/(a1*omega) = rho*k/omega -- the same
  // well-defined starting value standard k-omega would give (see this
  // class's own constructor comment).
  const Real rho = fluid_.density();
  const Real nu = fluid_.kinematicViscosity();
  const auto& c = config_.coefficients;
  const Index n = mesh.numberOfCells();
  for (Index i = 0; i < n; ++i) {
    const Real cdkw = computeCrossDiffusionCoefficient(rho, c.sigmaOmega2, omega_[i], 0.0);
    const Real arg1 = computeF1Argument(k_[i], omega_[i], wallDistance_[i], nu, rho, c.betaStar,
                                        c.sigmaOmega2, cdkw);
    f1_[i] = computeF1(arg1);
    const Real arg2 = computeF2Argument(k_[i], omega_[i], wallDistance_[i], nu, c.betaStar);
    f2_[i] = computeF2(arg2);
    turbulentViscosity_[i] =
        computeSSTTurbulentViscosity(rho, c.a1, k_[i], omega_[i], /*strainMagnitude=*/0.0, f2_[i]);
  }
  validateTurbulentViscosityField(mesh_, turbulentViscosity_);
}

std::string_view SSTModel::name() const noexcept { return "SST"; }

const ScalarField& SSTModel::turbulentViscosity() const { return turbulentViscosity_; }

const ScalarField& SSTModel::k() const noexcept { return k_; }
const ScalarField& SSTModel::omega() const noexcept { return omega_; }
const ScalarField& SSTModel::f1() const noexcept { return f1_; }
const ScalarField& SSTModel::f2() const noexcept { return f2_; }
const ScalarField& SSTModel::wallDistance() const noexcept { return wallDistance_; }

void SSTModel::correct(const Mesh& mesh, const VectorField& velocity,
                       const ScalarField& /*pressure*/) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n) {
    throw InvalidArgumentError("SSTModel::correct: velocity size does not match mesh cell count");
  }

  // Everything below reads only the pre-update k_/omega_/
  // turbulentViscosity_/f1_/f2_ (this call's "start of correct()" state)
  // -- same one-iteration-lagged coupling convention
  // KEpsilonModel/KOmegaModel already use. Wall distance is the cached
  // member, never recomputed (section 58).
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid_, velocityBoundaries_);
  const auto velocityGradient = computeVelocityGradient(mesh, velocity, velocityBoundaries_);
  const ScalarField s2 = computeStrainRateMagnitudeSquared(mesh, velocityGradient);

  // grad(k)/grad(omega): the same scalar Gauss-gradient operator
  // pressure/pressure-correction already use (discretization::gradient),
  // reused unmodified -- not a second gradient implementation.
  const VectorField gradK = cfd::discretization::gradient(mesh, k_, kBoundaries_);
  const VectorField gradOmega = cfd::discretization::gradient(mesh, omega_, omegaBoundaries_);

  const Real rho = fluid_.density();
  const Real mu = fluid_.dynamicViscosity();
  const Real nu = fluid_.kinematicViscosity();
  const auto& c = config_.coefficients;
  const Real alpha1 = computeSSTAlpha(c.beta1, c.sigmaOmega1, c.betaStar, c.kappa);
  const Real alpha2 = computeSSTAlpha(c.beta2, c.sigmaOmega2, c.betaStar, c.kappa);

  ScalarField newF1(n);
  ScalarField newF2(n);
  ScalarField gammaK(n);
  ScalarField gammaOmega(n);
  ScalarField suK(n);
  ScalarField spK(n);
  ScalarField suOmega(n);
  ScalarField spOmega(n);
  ScalarField strainMagnitude(n);

  for (Index i = 0; i < n; ++i) {
    const Real gradKDotGradOmega = dot(gradK[i], gradOmega[i]);
    const Real cdkw =
        computeCrossDiffusionCoefficient(rho, c.sigmaOmega2, omega_[i], gradKDotGradOmega);
    const Real arg1 = computeF1Argument(k_[i], omega_[i], wallDistance_[i], nu, rho, c.betaStar,
                                        c.sigmaOmega2, cdkw);
    const Real f1 = computeF1(arg1);
    newF1[i] = f1;
    const Real arg2 = computeF2Argument(k_[i], omega_[i], wallDistance_[i], nu, c.betaStar);
    newF2[i] = computeF2(arg2);

    const Real blendedSigmaK = blendSSTCoefficient(f1, c.sigmaK1, c.sigmaK2);
    const Real blendedSigmaOmega = blendSSTCoefficient(f1, c.sigmaOmega1, c.sigmaOmega2);
    const Real blendedBeta = blendSSTCoefficient(f1, c.beta1, c.beta2);
    const Real blendedAlpha = blendSSTCoefficient(f1, alpha1, alpha2);

    // Diffusivities use the *pre-update*, stored mu_t (same lag
    // convention as KEpsilonModel/KOmegaModel's own Gamma computation).
    gammaK[i] = mu + (blendedSigmaK * turbulentViscosity_[i]);
    gammaOmega[i] = mu + (blendedSigmaOmega * turbulentViscosity_[i]);

    // Raw production from the pre-update, stored mu_t and this call's
    // fresh strain -- then limited (section 19) for the k equation's own
    // source only.
    const Real rawProduction = turbulentViscosity_[i] * s2[i];
    suK[i] = limitProduction(rawProduction, c.productionLimiterFactor, c.betaStar, rho, k_[i],
                             omega_[i]);
    spK[i] = -c.betaStar * rho * omega_[i];

    // omega production computed directly as blendedAlpha*rho*S^2 --
    // algebraically equal to blendedAlpha*rho/mu_t*rawProduction (section
    // 7/8's own reconciled form), but avoids ever dividing by mu_t (see
    // SSTEquation.hpp's own header comment on limitProduction). Cross-
    // diffusion source added on top, weighted by (1-F1).
    suOmega[i] = (blendedAlpha * rho * s2[i]) +
                 computeCrossDiffusionSource(rho, c.sigmaOmega2, omega_[i], gradKDotGradOmega, f1);
    spOmega[i] = -blendedBeta * rho * omega_[i];

    strainMagnitude[i] = std::sqrt(s2[i]);
  }

  // Both solves are attempted before any member field is touched -- if
  // either throws, every member stays exactly as it was on entry (see
  // this class's own header comment). Not `const`: both are moved from
  // below once the whole update has succeeded.
  ScalarField newK =
      solveRelaxedScalarTransport(mesh, k_, massFlux, gammaK, kBoundaries_, suK, spK,
                                  config_.kRelaxation, config_.kFloor, config_.kSolver);
  ScalarField newOmega = solveRelaxedScalarTransport(
      mesh, omega_, massFlux, gammaOmega, omegaBoundaries_, suOmega, spOmega,
      config_.omegaRelaxation, config_.omegaFloor, config_.omegaSolver);

  // New mu_t from the freshly-solved k/omega, reusing *this call's*
  // strain/F2 (computed above from the pre-update state/current
  // velocity) rather than recomputing F2 from the just-solved k/omega --
  // a documented iteration-ordering choice (section 32), the same
  // "reuse what this call already computed rather than a second
  // recomputation pass" pattern KEpsilonModel/KOmegaModel already use
  // for their own end-of-call mu_t update.
  ScalarField newMuT(n);
  for (Index i = 0; i < n; ++i) {
    newMuT[i] =
        computeSSTTurbulentViscosity(rho, c.a1, newK[i], newOmega[i], strainMagnitude[i], newF2[i]);
  }
  validateTurbulentViscosityField(mesh, newMuT);

  Real maxChange = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxChange = std::max(maxChange, std::abs(newK[i] - k_[i]));
    maxChange = std::max(maxChange, std::abs(newOmega[i] - omega_[i]));
  }
  lastConvergenceResidual_ = maxChange;

  k_ = std::move(newK);
  omega_ = std::move(newOmega);
  turbulentViscosity_ = std::move(newMuT);
  f1_ = std::move(newF1);
  f2_ = std::move(newF2);
}

std::optional<Real> SSTModel::convergenceResidual() const { return lastConvergenceResidual_; }

}  // namespace cfd::turbulence
