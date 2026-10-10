#include "DriverSeegmillerCase.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::validation::driver_seegmiller {

namespace {

constexpr const char* kInletPrefix = "inlet_";

}  // namespace

Index cellsWallNormal(const MeshSpec& spec) {
  // 9 step heights of channel.
  return 9 * spec.cellsPerStepHeight;
}

Index cellsStreamwise(const MeshSpec& spec) {
  return static_cast<Index>(kDomainLength / kStepHeight) * spec.cellsPerStepHeight;
}

mesh::Mesh makeMesh(const MeshSpec& spec) {
  if (spec.cellsPerStepHeight == 0) {
    throw std::invalid_argument("makeMesh: cellsPerStepHeight must be positive");
  }
  const Index ny = cellsWallNormal(spec);
  const Index nx = cellsStreamwise(spec);
  const mesh::Mesh base =
      mesh::MeshGeometry::createCartesian2D(nx, ny, kDomainLength, kChannelHeight);

  std::vector<mesh::BoundaryPatch> patches;
  std::vector<Index> stepFaces;
  std::vector<mesh::BoundaryPatch> inletPatches;
  for (const auto& patch : base.boundaryPatches()) {
    if (patch.name() != "left") {
      patches.push_back(patch);
      continue;
    }
    for (const Index faceId : patch.faceIds()) {
      // Below the step lip is the step face; above it is inflow. The lip sits
      // exactly on a face line because ny is a multiple of cellsPerStepHeight.
      if (base.face(faceId).centroid().y < kStepHeight) {
        stepFaces.push_back(faceId);
      } else {
        inletPatches.emplace_back(kInletPrefix + std::to_string(faceId),
                                  std::vector<Index>{faceId});
      }
    }
  }
  patches.emplace_back("step", std::move(stepFaces));
  for (auto& patch : inletPatches) patches.push_back(std::move(patch));
  return mesh::Mesh(base.cells(), base.faces(), std::move(patches));
}

Real averagedInletVelocity(Real y0, Real y1) {
  // The 1/7 power law integrated exactly, so the inflow rate does not depend
  // on the mesh -- a midpoint sample would make the total inflow drift with
  // resolution and quietly change the Reynolds number between meshes.
  //
  //   u(y') = U (y'/delta)^(1/7)   for y' < delta,   U above it
  //   integral of (y'/delta)^(1/7) dy' = (7/8) delta (y'/delta)^(8/7)
  const auto primitive = [](Real y) {
    const Real yp = y - kStepHeight;
    if (yp <= 0.0) return 0.0;
    if (yp >= kInletBoundaryLayerThickness) {
      // Boundary-layer part, then free stream.
      const Real blPart = (7.0 / 8.0) * kInletBoundaryLayerThickness * kReferenceVelocity;
      return blPart + kReferenceVelocity * (yp - kInletBoundaryLayerThickness);
    }
    return (7.0 / 8.0) * kInletBoundaryLayerThickness * kReferenceVelocity *
           std::pow(yp / kInletBoundaryLayerThickness, 8.0 / 7.0);
  };
  return (primitive(y1) - primitive(y0)) / (y1 - y0);
}

Boundaries makeBoundaries(const mesh::Mesh& mesh) {
  Boundaries boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const std::string& name = patch.name();
    if (name.rfind(kInletPrefix, 0) == 0) {
      // One face per inlet patch, so the face average is the patch value.
      const mesh::Face& face = mesh.face(patch.faceIds().front());
      const Real half = 0.5 * face.area();
      const Real y0 = face.centroid().y - half;
      const Real y1 = face.centroid().y + half;
      boundaries.velocity.set(
          mesh, name,
          std::make_unique<boundary::Inlet>(Vector2{averagedInletVelocity(y0, y1), 0.0}));
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedGradient>(0.0));
    } else if (name == "right") {
      boundaries.velocity.set(mesh, name, std::make_unique<boundary::Outlet>());
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedValue>(0.0));
    } else {
      // "step", "bottom", "top".
      boundaries.velocity.set(mesh, name, std::make_unique<boundary::Wall>());
      boundaries.pressure.set(mesh, name, std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  return boundaries;
}

WallDistribution bottomWallDistribution(const mesh::Mesh& mesh, const MeshSpec& spec,
                                        const fields::VectorField& velocity,
                                        const fields::ScalarField& pressure, Real dynamicViscosity,
                                        const fields::ScalarField* modelWallShear) {
  const Index nx = cellsStreamwise(spec);
  const Index ny = cellsWallNormal(spec);
  const Real dy = kChannelHeight / static_cast<Real>(ny);
  const Real y1 = 0.5 * dy;  // first-cell centre height
  const Real dynamicHead = 0.5 * kDensity * kReferenceVelocity * kReferenceVelocity;

  WallDistribution distribution;
  distribution.x.reserve(nx);
  distribution.cf.reserve(nx);
  distribution.cp.reserve(nx);

  const Real referencePressure = pressure[(0 * nx) + (nx - 1)];

  for (Index i = 0; i < nx; ++i) {
    const Index cell = (0 * nx) + i;  // bottom row
    distribution.x.push_back(mesh.cell(cell).centroid().x / kStepHeight);

    const Real u = velocity[cell].x;
    Real tauW = 0.0;
    if (modelWallShear != nullptr && (*modelWallShear)[cell] > 0.0) {
      // The model's wall shear is a MAGNITUDE. Reattachment is read from the
      // SIGN, so the direction comes from the wall-adjacent tangential
      // velocity -- inside the bubble u < 0 and the shear opposes it.
      tauW = std::copysign((*modelWallShear)[cell], u);
    } else {
      // No model wall shear (laminar, or a cell the treatment did not reach):
      // the one-sided secant, which is what the laminar harness uses.
      tauW = dynamicViscosity * u / y1;
    }
    distribution.cf.push_back(tauW / dynamicHead);
    distribution.cp.push_back((pressure[cell] - referencePressure) / dynamicHead);
  }
  return distribution;
}

std::vector<std::pair<Real, Real>> outletProfile(const mesh::Mesh& mesh, const MeshSpec& spec,
                                                 const fields::VectorField& velocity) {
  const Index nx = cellsStreamwise(spec);
  const Index ny = cellsWallNormal(spec);
  std::vector<std::pair<Real, Real>> profile;
  profile.reserve(ny);
  for (Index j = 0; j < ny; ++j) {
    const Index cell = (j * nx) + (nx - 1);
    profile.emplace_back(mesh.cell(cell).centroid().y / kStepHeight,
                         velocity[cell].x / kReferenceVelocity);
  }
  return profile;
}

}  // namespace cfd::validation::driver_seegmiller
