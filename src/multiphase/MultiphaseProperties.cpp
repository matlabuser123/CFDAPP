#include "cfd/multiphase/MultiphaseProperties.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::multiphase {

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TwoPhaseSystem::TwoPhaseSystem(PhaseProperties phase1, PhaseProperties phase2)
    : phase1_(std::move(phase1)), phase2_(std::move(phase2)) {}

const PhaseProperties& TwoPhaseSystem::phase1() const noexcept { return phase1_; }
const PhaseProperties& TwoPhaseSystem::phase2() const noexcept { return phase2_; }

namespace {
void requireFiniteAlpha(Real alpha) {
  if (!std::isfinite(alpha)) {
    throw InvalidArgumentError("TwoPhaseSystem: alpha must be finite");
  }
}
}  // namespace

Real TwoPhaseSystem::mixtureDensity(Real alpha) const {
  requireFiniteAlpha(alpha);
  return alpha * phase1_.density() + (1.0 - alpha) * phase2_.density();
}

Real TwoPhaseSystem::mixtureViscosity(Real alpha) const {
  requireFiniteAlpha(alpha);
  return alpha * phase1_.viscosity() + (1.0 - alpha) * phase2_.viscosity();
}

namespace {

ScalarField evaluateField(const Mesh& mesh, const ScalarField& alpha, const TwoPhaseSystem& system,
                          Real (TwoPhaseSystem::*evaluate)(Real) const) {
  if (alpha.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluateMixtureField: alpha size does not match mesh cell count");
  }
  ScalarField result(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    result[i] = (system.*evaluate)(alpha[i]);
  }
  return result;
}

}  // namespace

ScalarField evaluateMixtureDensityField(const Mesh& mesh, const ScalarField& alpha,
                                        const TwoPhaseSystem& system) {
  return evaluateField(mesh, alpha, system, &TwoPhaseSystem::mixtureDensity);
}

ScalarField evaluateMixtureViscosityField(const Mesh& mesh, const ScalarField& alpha,
                                          const TwoPhaseSystem& system) {
  return evaluateField(mesh, alpha, system, &TwoPhaseSystem::mixtureViscosity);
}

}  // namespace cfd::multiphase
