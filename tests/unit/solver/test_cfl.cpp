#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/solver/CFL.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::solver::calculateCFL;
using cfd::solver::CFLResult;

namespace {

constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();
constexpr Real kInf = std::numeric_limits<Real>::infinity();

// Uniform-velocity face mass flux over `mesh`, the same construction
// every discretization test in this project already uses (e.g.
// test_grid_refinement.cpp): flux_f = rho * dot(velocity, Sf). CFL needs
// no boundary conditions at all -- unlike convection/diffusion, it reads
// raw face flux magnitudes regardless of whether a face happens to be a
// domain boundary, so a 1x1 mesh (every face a boundary face) is just as
// valid a probe as a larger one.
SurfaceField uniformFlowFlux(const Mesh& mesh, const Vector2& velocity, Real density) {
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = density * cfd::dot(velocity, face.areaVector());
  }
  return flux;
}

}  // namespace

// TODO.md P2 section 27's worked example: U=1, dx=0.1, dt=0.02 ->
// Co ~= U*dt/dx = 0.2. A 1x1, dx=dy=0.1 mesh's single cell has this
// value *exactly* (see CFL.hpp for the derivation of why the 1/2-summed-
// absolute-flux convention reduces to this simple form for a uniform
// flow), not just approximately -- checked to machine precision.
TEST(CFLTest, ExactValueForOneDEquivalentCase) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 0.1, 0.1);
  const SurfaceField flux = uniformFlowFlux(mesh, Vector2{1.0, 0.0}, /*density=*/1.0);

  const CFLResult result = calculateCFL(mesh, flux, /*density=*/1.0, /*dt=*/0.02);

  EXPECT_NEAR(result.maxCFL, 0.2, 1e-12);
  EXPECT_NEAR(result.meanCFL, 0.2, 1e-12);
  EXPECT_EQ(result.maxCFLCell, 0u);
}

TEST(CFLTest, ZeroVelocityGivesZeroCFL) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const SurfaceField flux = uniformFlowFlux(mesh, Vector2{0.0, 0.0}, 1.0);

  const CFLResult result = calculateCFL(mesh, flux, 1.0, 0.1);

  EXPECT_DOUBLE_EQ(result.maxCFL, 0.0);
  EXPECT_DOUBLE_EQ(result.meanCFL, 0.0);
}

TEST(CFLTest, HalvingDtHalvesCFL) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const SurfaceField flux = uniformFlowFlux(mesh, Vector2{2.0, -1.0}, 1.0);

  const CFLResult full = calculateCFL(mesh, flux, 1.0, 0.1);
  const CFLResult halved = calculateCFL(mesh, flux, 1.0, 0.05);

  EXPECT_NEAR(halved.maxCFL, full.maxCFL / 2.0, 1e-12);
  EXPECT_NEAR(halved.meanCFL, full.meanCFL / 2.0, 1e-12);
}

TEST(CFLTest, DoublingVelocityDoublesCFL) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const SurfaceField flux = uniformFlowFlux(mesh, Vector2{1.0, 0.5}, 1.0);
  const SurfaceField doubledFlux = uniformFlowFlux(mesh, Vector2{2.0, 1.0}, 1.0);

  const CFLResult base = calculateCFL(mesh, flux, 1.0, 0.1);
  const CFLResult doubled = calculateCFL(mesh, doubledFlux, 1.0, 0.1);

  EXPECT_NEAR(doubled.maxCFL, base.maxCFL * 2.0, 1e-12);
  EXPECT_NEAR(doubled.meanCFL, base.meanCFL * 2.0, 1e-12);
}

// A uniform flow gives every cell the same CFL (max == mean, trivially).
// Build a genuinely non-uniform flux by hand -- not from a single global
// velocity -- so max/mean/maxCFLCell are independently meaningful.
TEST(CFLTest, MaxMeanAndMaxCellForNonUniformFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);  // 3 equal-volume cells
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = 0.0;
  }
  // Cell 1 (the middle cell) gets a much larger through-flux than cells 0
  // and 2: its west/east faces carry 10x the flow of the mesh's outer
  // boundary faces.
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    flux[face.id()] = 10.0;  // every internal face borders cell 1
  }

  const CFLResult result = calculateCFL(mesh, flux, 1.0, 1.0);

  EXPECT_EQ(result.maxCFLCell, 1u);
  EXPECT_GT(result.maxCFL, result.meanCFL);
  // Cell 1: two internal faces at |10|, volume 1.0 -> Co = (1/(2*1))*(20/1) = 10.
  EXPECT_NEAR(result.maxCFL, 10.0, 1e-12);
  // Cells 0 and 2: one internal face at |10|, volume 1.0 -> Co = 5 each;
  // cell 1: Co = 10. Equal volumes -> plain mean = (5+10+5)/3.
  EXPECT_NEAR(result.meanCFL, (5.0 + 10.0 + 5.0) / 3.0, 1e-12);
}

TEST(CFLTest, RejectsFaceFluxSizeMismatch) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField wrongSize(mesh.numberOfFaces() - 1, 0.0);
  EXPECT_THROW((void)calculateCFL(mesh, wrongSize, 1.0, 0.1), cfd::InvalidArgumentError);
}

TEST(CFLTest, RejectsNonPositiveDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)calculateCFL(mesh, flux, 1.0, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)calculateCFL(mesh, flux, 1.0, -0.1), cfd::InvalidArgumentError);
}

TEST(CFLTest, RejectsNonFiniteDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)calculateCFL(mesh, flux, 1.0, kNaN), cfd::InvalidArgumentError);
  EXPECT_THROW((void)calculateCFL(mesh, flux, 1.0, kInf), cfd::InvalidArgumentError);
}

TEST(CFLTest, RejectsNonFiniteOrNonPositiveDensity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)calculateCFL(mesh, flux, kNaN, 0.1), cfd::InvalidArgumentError);
  EXPECT_THROW((void)calculateCFL(mesh, flux, kInf, 0.1), cfd::InvalidArgumentError);
  EXPECT_THROW((void)calculateCFL(mesh, flux, 0.0, 0.1), cfd::InvalidArgumentError);
  EXPECT_THROW((void)calculateCFL(mesh, flux, -1.0, 0.1), cfd::InvalidArgumentError);
}

TEST(CFLTest, RepeatedCallsAreDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const SurfaceField flux = uniformFlowFlux(mesh, Vector2{1.3, -0.7}, 1.0);

  const CFLResult a = calculateCFL(mesh, flux, 1.0, 0.1);
  const CFLResult b = calculateCFL(mesh, flux, 1.0, 0.1);

  EXPECT_EQ(a.maxCFL, b.maxCFL);
  EXPECT_EQ(a.meanCFL, b.meanCFL);
  EXPECT_EQ(a.maxCFLCell, b.maxCFLCell);
}
