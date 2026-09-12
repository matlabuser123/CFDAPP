#include "cfd/compressible/CompressibleMassFlux.hpp"

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
      const Real faceTemperature =
          (temperatureBoundaries != nullptr)
              ? cfd::discretization::interpolateFace(mesh, face, temperature,
                                                     *temperatureBoundaries)
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

SurfaceField calculateCompressibleMassFlux(const Mesh& mesh, const VectorField& velocity,
                                           const ScalarField& density,
                                           const BoundaryConditionSet& velocityBoundaries,
                                           const ScalarField& pressureGauge,
                                           const BoundaryConditionSet& pressureBoundaries,
                                           Real referencePressure,
                                           const ThermodynamicProperties& thermodynamics,
                                           const ScalarField& temperature,
                                           const BoundaryConditionSet* temperatureBoundaries) {
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
