#include "cfd/turbulence/KOmegaEquation.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::turbulence {

using cfd::fields::ScalarField;

ScalarField computeLinearEffectiveDiffusivity(Real molecularViscosity,
                                              const ScalarField& turbulentViscosity, Real sigma) {
  if (!std::isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw InvalidArgumentError(
        "computeLinearEffectiveDiffusivity: molecularViscosity must be finite and > 0");
  }
  if (!std::isfinite(sigma) || !(sigma > 0.0)) {
    throw InvalidArgumentError("computeLinearEffectiveDiffusivity: sigma must be finite and > 0");
  }
  ScalarField gamma(turbulentViscosity.size());
  for (Index i = 0; i < turbulentViscosity.size(); ++i) {
    const Real muT = turbulentViscosity[i];
    if (!std::isfinite(muT) || muT < 0.0) {
      throw InvalidArgumentError(
          "computeLinearEffectiveDiffusivity: turbulentViscosity must be finite and >= 0");
    }
    // Gamma = mu + sigma*mu_t -- the standard k-omega linear form, NOT
    // k-epsilon's mu + mu_t/sigma.
    gamma[i] = molecularViscosity + (sigma * muT);
  }
  return gamma;
}

}  // namespace cfd::turbulence
