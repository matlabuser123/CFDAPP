#include "cfd/compressible/CompressibleMassFlux.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::compressible {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;

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
  if (density.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: density size does not match mesh cell count");
  }
  if (pressureGauge.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: pressureGauge size does not match mesh cell count");
  }
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: temperature size does not match mesh cell count");
  }

  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Vector2 faceVelocity =
        cfd::discretization::interpolateFace(mesh, face, velocity, velocityBoundaries);

    Real faceDensity{};
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
      faceDensity = thermodynamics.density(faceAbsolutePressure, faceTemperature);
    } else {
      faceDensity = cfd::discretization::interpolateInternalFace(mesh, face, density);
    }
    massFlux[faceId] = faceDensity * dot(faceVelocity, face.areaVector());
  }
  return massFlux;
}

}  // namespace cfd::compressible
