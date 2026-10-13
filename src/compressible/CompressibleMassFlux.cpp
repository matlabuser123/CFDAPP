#include "cfd/compressible/CompressibleMassFlux.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::compressible {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;

namespace {

void validateFaceDensityInputs(const Mesh& mesh, const ScalarField& density,
                               const ScalarField& pressureGauge, const ScalarField& temperature,
                               const char* callerName) {
  if (density.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string(callerName) +
                               ": density size does not match mesh cell count");
  }
  if (pressureGauge.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string(callerName) +
                               ": pressureGauge size does not match mesh cell count");
  }
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string(callerName) +
                               ": temperature size does not match mesh cell count");
  }
  // P17.1: the density VALUES, not only the array length. Both functions below reach
  // this validator, so checking here covers the canonical face-density and mass-flux
  // path in one place.
  //
  // Why this is input validation and not a downstream finiteness sweep: an internal
  // face takes interpolateInternalFace of this field, which is a plain weighted mean
  // with no validation of its own, so a bad cell value reaches mDot_f unexamined. The
  // dangerous cases are the FINITE ones. Measured on the pre-P17.1 tree
  // (results/p17/variable_density/logs/00_dryrun.log): rho[0] = -2 against a valid
  // rho[1] = 2 gave face rho_f = 0 and mDot_f = 0 -- a face silently carrying no mass
  // -- and rho[0] = 0 gave rho_f = 1, a plausible-looking flux from an impossible
  // state. Neither is non-finite, so CompressibleSIMPLE's own allFinite() sweep (the
  // only protection downstream) cannot see either one.
  //
  // Rejected, never clamped: a clamped density produces a mass flux that no
  // thermodynamic state corresponds to, and lets a diverging solve look plausible --
  // the same reasoning thermal/Enthalpy.hpp records for an out-of-range enthalpy
  // ("an ERROR, not a clamp"). A positivity LIMITER is a conserved-state concern and
  // belongs with a density-based solver, not here.
  //
  // Only `density` is value-checked here. `temperature` and `pressureGauge` reach the
  // EOS at every boundary face and it already validates them, proven by
  // CompressibleMassFluxTest.ThrowsOnNonPositiveBoundaryTemperature -- and that test
  // relies on reaching the boundary EOS, so an up-front temperature check would leave
  // it passing while no longer exercising the path it was written for. Note also that
  // `pressureGauge` is GAUGE pressure and is legitimately negative below the reference,
  // so positivity would be the wrong rule for it in any case.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!std::isfinite(density[i]) || !(density[i] > 0.0)) {
      throw InvalidArgumentError(std::string(callerName) + ": cell " + std::to_string(i) +
                                 ": density must be finite and > 0");
    }
  }
}

}  // namespace

SurfaceField evaluateCompressibleFaceDensity(const Mesh& mesh, const ScalarField& density,
                                             const ScalarField& pressureGauge,
                                             const BoundaryConditionSet& pressureBoundaries,
                                             Real referencePressure,
                                             const ThermodynamicProperties& thermodynamics,
                                             const ScalarField& temperature,
                                             const BoundaryConditionSet* temperatureBoundaries) {
  validateFaceDensityInputs(mesh, density, pressureGauge, temperature,
                            "evaluateCompressibleFaceDensity");

  SurfaceField faceDensity(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    if (face.isBoundary()) {
      // P12-COMP-001: the EOS evaluated at this boundary face's own
      // boundary-interpolated absolute pressure and temperature --
      // supersedes the previous owner-cell-reuse simplification. See
      // this header's own comment for why this is correct/well-defined
      // for every existing boundary-condition type (Inlet/Outlet/Wall),
      // including why Wall faces are unaffected in practice (their
      // velocity BC already makes u_f.Sf = 0 there).
      const Real faceGaugePressure =
          cfd::discretization::interpolateFace(mesh, face, pressureGauge, pressureBoundaries);
      const Real faceAbsolutePressure = referencePressure + faceGaugePressure;
      const Real faceTemperature = (temperatureBoundaries != nullptr)
                                       ? cfd::discretization::interpolateFace(
                                             mesh, face, temperature, *temperatureBoundaries)
                                       // Isothermal mode: `temperature` is already spatially
                                       // uniform (every cell holds the same configured constant),
                                       // so the owner cell's own value *is* the boundary value --
                                       // no interpolation needed, not a simplification.
                                       : temperature[face.owner()];
      faceDensity[faceId] = thermodynamics.density(faceAbsolutePressure, faceTemperature);
    } else {
      faceDensity[faceId] = cfd::discretization::interpolateInternalFace(mesh, face, density);
    }
  }
  return faceDensity;
}

SurfaceField calculateCompressibleMassFlux(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& density,
    const BoundaryConditionSet& velocityBoundaries, const ScalarField& pressureGauge,
    const BoundaryConditionSet& pressureBoundaries, Real referencePressure,
    const ThermodynamicProperties& thermodynamics, const ScalarField& temperature,
    const BoundaryConditionSet* temperatureBoundaries) {
  cfd::mesh::requireTwoDimensional(mesh, "calculateCompressibleMassFlux");
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: velocity size does not match mesh cell count");
  }
  validateFaceDensityInputs(mesh, density, pressureGauge, temperature,
                            "calculateCompressibleMassFlux");

  // P12-COMP-002: reuses the exact same per-face density
  // evaluateCompressibleMassFlux's own predictor flux and a compressible
  // pressure-correction equation's D_f coefficient must agree on -- see
  // evaluateCompressibleFaceDensity's own header comment.
  const SurfaceField faceDensity = evaluateCompressibleFaceDensity(
      mesh, density, pressureGauge, pressureBoundaries, referencePressure, thermodynamics,
      temperature, temperatureBoundaries);

  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Vector2 faceVelocity =
        cfd::discretization::interpolateFace(mesh, face, velocity, velocityBoundaries);
    massFlux[faceId] = faceDensity[faceId] * dot(faceVelocity, face.areaVector());
  }
  return massFlux;
}

}  // namespace cfd::compressible
