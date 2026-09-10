// P3-PHYS-006 sections 11-12: the canonical compressible mass flux --
// uniform-density equivalence to physics::calculateMassFlux, and face-
// density interpolation for a non-uniform field.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::compressible::calculateCompressibleMassFlux;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

Mesh makeChannelMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeChannelBoundaries(const Mesh& mesh, Vector2 inletVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(inletVelocity));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

}  // namespace

TEST(CompressibleMassFluxTest, UniformDensityMatchesIncompressibleMassFluxExactly) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{3.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{3.0, 0.0});
  const Real rho = 2.0;

  const FluidProperties fluid(rho, 1.0);
  const SurfaceField incompressible = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const ScalarField density(mesh.numberOfCells(), rho);
  const SurfaceField compressible = calculateCompressibleMassFlux(mesh, velocity, density, boundaries);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    EXPECT_DOUBLE_EQ(compressible[faceId], incompressible[faceId]) << "face " << faceId;
  }
}

TEST(CompressibleMassFluxTest, InternalFaceUsesArithmeticMeanDensity) {
  // Same setup as physics::MassFluxTest.InternalFaceValue, but with
  // per-cell density instead of one constant: rho_P=1, rho_N=3 ->
  // rho_f=2 (arithmetic mean on this uniform grid), U_P=(1,0), U_N=(3,0)
  // -> U_f=(2,0), internal face area=1, Sf=(1,0) -> F = 2*2*1 = 4.
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  VectorField velocity(mesh.numberOfCells());
  velocity[0] = Vector2{1.0, 0.0};
  velocity[1] = Vector2{3.0, 0.0};
  ScalarField density(mesh.numberOfCells());
  density[0] = 1.0;
  density[1] = 3.0;

  const SurfaceField massFlux = calculateCompressibleMassFlux(mesh, velocity, density, boundaries);

  bool found = false;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_NEAR(massFlux[face.id()], 4.0, 1e-12);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(CompressibleMassFluxTest, BoundaryFaceUsesOwnerCellsOwnDensity) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  ScalarField density(mesh.numberOfCells());
  density[0] = 5.0;  // owner of the "left" boundary face.
  density[1] = 100.0;  // must have no effect on the left boundary face.

  const SurfaceField massFlux = calculateCompressibleMassFlux(mesh, velocity, density, boundaries);
  const Index leftFaceId = mesh.boundaryPatch("left").faceIds().front();
  // Sf=(-area,0), U=(1,0) -> U.Sf = -area; F = rho_owner * U.Sf.
  const Real area = mesh.face(leftFaceId).area();
  EXPECT_NEAR(massFlux[leftFaceId], 5.0 * (-area), 1e-12);
}

TEST(CompressibleMassFluxTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells(), 1.0);
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, boundaries),
               InvalidArgumentError);
}

TEST(CompressibleMassFluxTest, MismatchedDensitySizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField density(mesh.numberOfCells() + 1, 1.0);
  EXPECT_THROW((void)calculateCompressibleMassFlux(mesh, velocity, density, boundaries),
               InvalidArgumentError);
}
