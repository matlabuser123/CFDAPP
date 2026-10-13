// P16 -- the mesh-aware wall-treatment layer, and the fixed-row primitive it
// needs.
//
// WallFunctions' own tests prove the formulas reproduce the log law. These
// prove the layer that applies them to a mesh picks the right cells, the right
// distances and the right velocities -- the part where a wall treatment usually
// goes wrong without any formula being incorrect.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::WallFunctionCoefficients;
using cfd::turbulence::WallResolution;

namespace {

constexpr WallFunctionCoefficients kCoefficients{};

// SparseMatrix is immutable and exposes no element accessor, so a row is probed
// the way the solver itself reads it: multiply by a unit vector.
Real entry(const cfd::algebra::SparseMatrix& matrix, Index row, Index column) {
  cfd::algebra::Vector e(matrix.columns(), 0.0);
  e[column] = 1.0;
  return matrix.multiply(e)[row];
}

// A channel-like box: bottom and top are walls, left inlet, right outlet.
struct Channel {
  cfd::mesh::Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
};

Channel makeChannel(Index nx, Index ny, Real length, Real height) {
  Channel channel{MeshGeometry::createCartesian2D(nx, ny, length, height), {}};
  channel.velocityBoundaries.set(channel.mesh, "left",
                                 std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{1.0, 0.0}));
  channel.velocityBoundaries.set(channel.mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  channel.velocityBoundaries.set(channel.mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  channel.velocityBoundaries.set(channel.mesh, "top", std::make_unique<cfd::boundary::Wall>());
  return channel;
}

}  // namespace

// --- the fixed-row primitive -------------------------------------------------

TEST(SparseMatrixFixedRow, ReplacesTheRowWithAUnitDiagonal) {
  SparseMatrixBuilder builder(3, 3);
  builder.add(0, 0, 5.0);
  builder.add(0, 1, -2.0);
  builder.add(1, 1, 7.0);
  builder.fixRow(0);
  const auto matrix = builder.build();

  EXPECT_DOUBLE_EQ(matrix.diagonal(0), 1.0);
  // The row's own off-diagonal is gone.
  Real rowSum = 0.0;
  for (Index c = 0; c < 3; ++c) rowSum += entry(matrix, 0, c);
  EXPECT_DOUBLE_EQ(rowSum, 1.0) << "a fixed row must contain nothing but its unit diagonal";
  // Other rows are untouched.
  EXPECT_DOUBLE_EQ(matrix.diagonal(1), 7.0);
}

TEST(SparseMatrixFixedRow, OrderOfFixAndAddDoesNotMatter) {
  // The wall treatment identifies its cells before the face loop runs, so
  // contributions keep arriving after fixRow is called. Both orders must give
  // the same matrix.
  SparseMatrixBuilder before(2, 2);
  before.fixRow(0);
  before.add(0, 0, 9.0);
  before.add(0, 1, 3.0);

  SparseMatrixBuilder after(2, 2);
  after.add(0, 0, 9.0);
  after.add(0, 1, 3.0);
  after.fixRow(0);

  EXPECT_DOUBLE_EQ(before.build().diagonal(0), after.build().diagonal(0));
  EXPECT_DOUBLE_EQ(entry(before.build(), 0, 1), entry(after.build(), 0, 1));
  EXPECT_DOUBLE_EQ(entry(before.build(), 0, 1), 0.0);
}

TEST(SparseMatrixFixedRow, LeavesOtherRowsCouplingToTheFixedUnknown) {
  // The point of a fixed value: neighbours still see it. If fixRow zeroed the
  // COLUMN as well, the imposed value would not reach anything.
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(1, 0, -1.5);
  builder.add(1, 1, 4.0);
  builder.fixRow(0);
  const auto matrix = builder.build();

  EXPECT_DOUBLE_EQ(entry(matrix, 1, 0), -1.5)
      << "the neighbour must still couple to the fixed cell";
}

TEST(SparseMatrixFixedRow, IsIdempotentAndRangeChecked) {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 3.0);
  builder.fixRow(0);
  builder.fixRow(0);
  EXPECT_DOUBLE_EQ(builder.build().diagonal(0), 1.0) << "fixing twice must not write two diagonals";
  EXPECT_THROW(builder.fixRow(5), InvalidArgumentError);
  EXPECT_THROW(builder.fixRow(-1), InvalidArgumentError);
}

TEST(SparseMatrixFixedRow, UnusedLeavesTheMatrixUnchanged) {
  // The property that keeps every existing assembly byte-identical.
  SparseMatrixBuilder plain(2, 2);
  plain.add(0, 0, 2.5);
  plain.add(0, 1, 1.25);
  plain.add(1, 1, -3.0);
  const auto matrix = plain.build();
  EXPECT_DOUBLE_EQ(matrix.diagonal(0), 2.5);
  EXPECT_DOUBLE_EQ(entry(matrix, 0, 1), 1.25);
  EXPECT_DOUBLE_EQ(matrix.diagonal(1), -3.0);
}

// --- collecting wall faces ---------------------------------------------------

TEST(WallTreatment, CollectsOnlyWallPatches) {
  const Channel channel = makeChannel(8, 4, 4.0, 1.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);

  // bottom + top, 8 faces each.
  EXPECT_EQ(wallFaces.size(), 16u) << "inlet and outlet faces must not be treated as walls";
  for (const auto& wallFace : wallFaces) {
    EXPECT_GT(wallFace.wallDistance, 0.0);
    EXPECT_GT(wallFace.area, 0.0);
  }
}

TEST(WallTreatment, WallDistanceIsHalfTheCellHeightOnACartesianMesh) {
  // ny = 4 over a height of 1.0 -> cells 0.25 tall -> first centre at 0.125.
  const Channel channel = makeChannel(8, 4, 4.0, 1.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  for (const auto& wallFace : wallFaces) {
    EXPECT_NEAR(wallFace.wallDistance, 0.125, 1e-12);
  }
}

TEST(WallTreatment, RefusesAMeshWithNoWall) {
  Channel channel = makeChannel(4, 4, 1.0, 1.0);
  channel.velocityBoundaries.replace(channel.mesh, "bottom",
                                     std::make_unique<cfd::boundary::Outlet>());
  channel.velocityBoundaries.replace(channel.mesh, "top",
                                     std::make_unique<cfd::boundary::Outlet>());
  EXPECT_THROW((void)cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries),
               InvalidArgumentError);
}

TEST(WallTreatment, MovingWallVelocityIsCarriedSoShearIsRelative) {
  Channel channel = makeChannel(4, 4, 1.0, 1.0);
  channel.velocityBoundaries.replace(
      channel.mesh, "top", std::make_unique<cfd::boundary::MovingWall>(cfd::Vector2{2.0, 0.0}));
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);

  bool sawMoving = false;
  for (const auto& wallFace : wallFaces) {
    if (wallFace.wallVelocity.x != 0.0) {
      sawMoving = true;
      EXPECT_DOUBLE_EQ(wallFace.wallVelocity.x, 2.0);
    }
  }
  EXPECT_TRUE(sawMoving) << "a MovingWall must carry its own velocity into the treatment";
}

// --- evaluating a wall face --------------------------------------------------

TEST(WallTreatment, OnlyTheWallParallelVelocityProducesShear) {
  // A cell moving purely NORMAL to the wall carries no wall shear. Including
  // the normal component would invent shear wherever a transient solution has a
  // small normal velocity at a wall -- which it does throughout a SIMPLE solve.
  const Channel channel = makeChannel(4, 4, 1.0, 1.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();

  VectorField normalOnly(n, cfd::Vector2{0.0, 3.0});  // bottom/top walls are y-normal
  const ScalarField k(n, 0.01);

  const auto state = cfd::turbulence::evaluateWallFace(channel.mesh, wallFaces[0], normalOnly, k,
                                                       1.0, 1e-5, kCoefficients);
  EXPECT_NEAR(state.tangentialVelocity, 0.0, 1e-14);
  EXPECT_DOUBLE_EQ(state.wallShearStress, 0.0);
}

TEST(WallTreatment, ReproducesTheLogLawThroughTheMeshLayer) {
  // The round trip again, but driven THROUGH the mesh layer -- so it also
  // exercises the wall-distance extraction and the tangential projection, not
  // just the formula.
  const Real density = 1.0;
  const Real dynamicViscosity = 1e-4;
  const Real uTau = 0.2;
  const Real height = 2.0;
  const Index ny = 8;
  const Real firstCentre = 0.5 * (height / static_cast<Real>(ny));

  const Channel channel = makeChannel(4, ny, 4.0, height);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();

  const Real expectedYPlus = cfd::turbulence::yPlus(density, uTau, firstCentre, dynamicViscosity);
  ASSERT_TRUE(cfd::turbulence::isWallFunctionResolutionValid(expectedYPlus))
      << "y+ = " << expectedYPlus;

  const Real logLawVelocity = uTau * cfd::turbulence::uPlusLogLaw(expectedYPlus, kCoefficients);
  const VectorField velocity(n, cfd::Vector2{logLawVelocity, 0.0});
  const ScalarField k(n, cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients));

  const auto state = cfd::turbulence::evaluateWallFace(channel.mesh, wallFaces[0], velocity, k,
                                                       density, dynamicViscosity, kCoefficients);

  EXPECT_NEAR(state.wallShearStress, density * uTau * uTau, 1e-12);
  EXPECT_NEAR(state.frictionVelocity, uTau, 1e-12);
  EXPECT_NEAR(state.yPlus, expectedYPlus, 1e-10);
  EXPECT_EQ(state.resolution, WallResolution::LogLayer);
  // In equilibrium the two friction velocities coincide -- that is what makes
  // this configuration an equilibrium one.
  EXPECT_NEAR(state.equilibriumFrictionVelocity, state.frictionVelocity, 1e-12);
}

// --- the whole treatment -----------------------------------------------------

TEST(WallTreatment, ImposesEachWallCellExactlyOnce) {
  const Channel channel = makeChannel(6, 4, 3.0, 1.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{1.0, 0.0});
  const ScalarField k(n, 0.01);

  const auto treatment = cfd::turbulence::computeWallTreatment(channel.mesh, wallFaces, velocity, k,
                                                               1.0, 1e-4, kCoefficients);

  // 6 bottom + 6 top wall cells, each listed once.
  EXPECT_EQ(treatment.wallCells.size(), 12u);
  std::vector<Index> sorted = treatment.wallCells;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end())
      << "a cell must be imposed once, never twice";

  EXPECT_EQ(treatment.wallCellDissipation.size(), treatment.wallCells.size());
  EXPECT_EQ(treatment.wallCellProduction.size(), treatment.wallCells.size());
}

TEST(WallTreatment, WallViscosityIsSetOnWallFacesAndNowhereElse) {
  const Channel channel = makeChannel(6, 4, 3.0, 1.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{1.0, 0.0});
  const ScalarField k(n, 0.01);

  const auto treatment = cfd::turbulence::computeWallTreatment(channel.mesh, wallFaces, velocity, k,
                                                               1.0, 1e-4, kCoefficients);

  Index nonZero = 0;
  for (Index f = 0; f < channel.mesh.numberOfFaces(); ++f) {
    if (treatment.wallViscosity[f] != 0.0) ++nonZero;
  }
  EXPECT_EQ(nonZero, static_cast<Index>(wallFaces.size()))
      << "zero elsewhere is what lets a non-wall face keep using its cell viscosity";

  for (const auto& wallFace : wallFaces) {
    EXPECT_GE(treatment.wallViscosity[wallFace.faceId], 1e-4)
        << "a turbulent wall face must be at least as dissipative as the molecular value";
  }
}

TEST(WallTreatment, ReportsOutOfBandResolutionInsteadOfAcceptingIt) {
  // Tuned to land in the BUFFER layer -- the same regime every pre-P16 run sat
  // in (y+ 7.5-8.2), not an absurd extreme. That is the case the reporting has
  // to catch, because it is the one that silently produced a 33% error.
  const Channel channel = makeChannel(4, 200, 4.0, 2.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{1.0, 0.0});
  const ScalarField k(n, 0.01);

  const auto treatment = cfd::turbulence::computeWallTreatment(channel.mesh, wallFaces, velocity, k,
                                                               1.0, 3.4e-5, kCoefficients);

  EXPECT_GT(treatment.facesOutsideValidBand, 0);
  EXPECT_LT(treatment.maximumYPlus, cfd::turbulence::kWallFunctionMinimumYPlus);
  EXPECT_GT(treatment.maximumYPlus, 5.0) << "this test is about the buffer layer specifically";
  // The buffer layer is where the log law under-predicts the laminar shear, so
  // the mu_t >= 0 floor fires -- and must be counted, not swallowed.
  EXPECT_GT(treatment.facesWithFlooredViscosity, 0);

  const std::string description = cfd::turbulence::describeWallResolution(treatment);
  EXPECT_NE(description.find("OUTSIDE"), std::string::npos) << description;
  EXPECT_NE(description.find("not trustworthy"), std::string::npos) << description;
}

TEST(WallTreatment, ReportsAValidBandAsValid) {
  const Real density = 1.0;
  const Real dynamicViscosity = 1e-4;
  const Real uTau = 0.2;
  const Real height = 2.0;
  const Index ny = 8;

  const Channel channel = makeChannel(4, ny, 4.0, height);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const Real firstCentre = 0.5 * (height / static_cast<Real>(ny));
  const Real expectedYPlus = cfd::turbulence::yPlus(density, uTau, firstCentre, dynamicViscosity);
  const VectorField velocity(
      n, cfd::Vector2{uTau * cfd::turbulence::uPlusLogLaw(expectedYPlus, kCoefficients), 0.0});
  const ScalarField k(n, cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients));

  const auto treatment = cfd::turbulence::computeWallTreatment(
      channel.mesh, wallFaces, velocity, k, density, dynamicViscosity, kCoefficients);

  EXPECT_EQ(treatment.facesOutsideValidBand, 0);
  const std::string description = cfd::turbulence::describeWallResolution(treatment);
  EXPECT_NE(description.find("all inside"), std::string::npos) << description;
}

// --- P16b: the mode actually reaches the mesh layer --------------------------
//
// WallFunctions' own tests prove the blended formulas. These prove the mode
// selector is not dead code: that the default is bitwise the pre-P16b path, and
// that asking for Automatic changes the governing wall physics rather than
// clamping a diagnostic.

namespace {

// A channel deliberately refined until the wall cell sits in the buffer layer
// -- exactly the state the Driver & Seegmiller refinement walked into, and the
// state the high-Re treatment has no valid answer for.
cfd::turbulence::WallTreatment treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode mode) {
  const Channel channel = makeChannel(4, 200, 4.0, 2.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{1.0, 0.0});
  const ScalarField k(n, 0.01);
  const cfd::turbulence::WallTreatmentOptions options{mode, 0.075};
  return cfd::turbulence::computeWallTreatment(channel.mesh, wallFaces, velocity, k, 1.0, 3.4e-5,
                                               kCoefficients, options);
}

}  // namespace

TEST(WallTreatmentMode, DefaultIsBitwiseTheHighReWallFunctionPath) {
  // The guarantee every preserved P16 result depends on: adding the mode
  // changed nothing for anyone who did not ask for it. Bitwise, not "close".
  const Channel channel = makeChannel(4, 200, 4.0, 2.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{1.0, 0.0});
  const ScalarField k(n, 0.01);

  // The pre-P16b call: no options argument at all.
  const auto implicitDefault = cfd::turbulence::computeWallTreatment(
      channel.mesh, wallFaces, velocity, k, 1.0, 3.4e-5, kCoefficients);
  const auto explicitHighRe =
      treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::HighReWallFunction);

  ASSERT_EQ(implicitDefault.faces.size(), explicitHighRe.faces.size());
  for (std::size_t i = 0; i < implicitDefault.faces.size(); ++i) {
    EXPECT_EQ(implicitDefault.faces[i].wallShearStress, explicitHighRe.faces[i].wallShearStress);
    EXPECT_EQ(implicitDefault.faces[i].specificDissipation,
              explicitHighRe.faces[i].specificDissipation);
    EXPECT_EQ(implicitDefault.faces[i].dissipation, explicitHighRe.faces[i].dissipation);
    EXPECT_EQ(implicitDefault.faces[i].production, explicitHighRe.faces[i].production);
    EXPECT_EQ(implicitDefault.faces[i].effectiveViscosity,
              explicitHighRe.faces[i].effectiveViscosity);
    EXPECT_EQ(implicitDefault.faces[i].yPlus, explicitHighRe.faces[i].yPlus);
  }
  EXPECT_EQ(implicitDefault.facesOutsideValidBand, explicitHighRe.facesOutsideValidBand);
  EXPECT_EQ(implicitDefault.facesWithFlooredViscosity, explicitHighRe.facesWithFlooredViscosity);
}

TEST(WallTreatmentMode, AutomaticChangesTheGoverningWallPhysics) {
  // NON-VACUITY. If the two modes produced the same wall state on a buffer-layer
  // mesh, the mode would be a label and nothing would have been fixed.
  const auto highRe =
      treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::HighReWallFunction);
  const auto automatic = treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::Automatic);

  ASSERT_GT(highRe.faces.size(), 0u);
  ASSERT_EQ(highRe.faces.size(), automatic.faces.size());

  // Every wall quantity that feeds the solve must differ -- shear into momentum,
  // omega imposed in the wall cell, production into the k equation.
  for (std::size_t i = 0; i < highRe.faces.size(); ++i) {
    EXPECT_NE(highRe.faces[i].wallShearStress, automatic.faces[i].wallShearStress);
    EXPECT_NE(highRe.faces[i].specificDissipation, automatic.faces[i].specificDissipation);
  }

  // And the high-Re path is the one that was wrong here: on this mesh it floors
  // mu_t because the log law under-predicts the laminar shear in the buffer
  // layer. The automatic path reaches the molecular value through its own
  // viscous branch, so nothing is floored.
  EXPECT_GT(highRe.facesWithFlooredViscosity, 0);
  EXPECT_EQ(automatic.facesWithFlooredViscosity, 0);
}

TEST(WallTreatmentMode, AutomaticReportsBranchPopulationsInsteadOfAViolationCount) {
  // The G1 replacement (section 6). On this buffer-layer mesh the high-Re mode
  // can only report "every face is invalid"; the automatic mode reports WHICH
  // branch is handling each face, which is information rather than a refusal.
  const auto highRe =
      treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::HighReWallFunction);
  const auto automatic = treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::Automatic);

  const Index total = static_cast<Index>(automatic.faces.size());
  EXPECT_EQ(automatic.viscousSublayerFaces + automatic.blendedFaces + automatic.logLayerFaces,
            total)
      << "every face must be accounted for by exactly one branch";

  // The high-Re mode has no branches, so it reports all faces as log layer --
  // which is precisely why its counts carry no information and its y+ band
  // diagnostics have to be read instead.
  EXPECT_EQ(highRe.logLayerFaces, total);
  EXPECT_EQ(highRe.viscousSublayerFaces, 0);
  EXPECT_EQ(highRe.blendedFaces, 0);

  // This mesh is in the buffer layer, so the automatic mode must actually place
  // faces outside the log branch -- otherwise this test would pass on a mesh
  // where the distinction does not arise.
  EXPECT_GT(automatic.viscousSublayerFaces + automatic.blendedFaces, 0)
      << "the buffer-layer mesh did not populate a non-log branch, so this "
         "test cannot discriminate";
}

TEST(WallTreatmentMode, AutomaticSurvivesAWallWhereTheHighRePathCannot) {
  // The property the whole mode exists for, at the MESH level rather than the
  // formula level: a wall with zero tangential velocity -- a separation or
  // reattachment line -- must still produce a finite, positive omega and a
  // molecular wall viscosity.
  const Channel channel = makeChannel(4, 40, 4.0, 2.0);
  const auto wallFaces =
      cfd::turbulence::collectWallFaces(channel.mesh, channel.velocityBoundaries);
  const Index n = channel.mesh.numberOfCells();
  const VectorField velocity(n, cfd::Vector2{0.0, 0.0});
  const ScalarField k(n, 0.0);

  const cfd::turbulence::WallTreatmentOptions options{cfd::turbulence::WallTreatmentMode::Automatic,
                                                      0.075};
  const auto automatic = cfd::turbulence::computeWallTreatment(channel.mesh, wallFaces, velocity, k,
                                                               1.0, 1e-5, kCoefficients, options);

  ASSERT_GT(automatic.faces.size(), 0u);
  for (const auto& face : automatic.faces) {
    EXPECT_TRUE(std::isfinite(face.wallShearStress));
    EXPECT_EQ(face.wallShearStress, 0.0) << "no velocity means no shear";
    EXPECT_TRUE(std::isfinite(face.specificDissipation));
    EXPECT_GT(face.specificDissipation, 0.0)
        << "omega must stay positive at a separation point -- it is what keeps "
           "the omega equation well posed there";
    EXPECT_EQ(face.effectiveViscosity, 1e-5) << "a stagnant wall cell is molecular";
    EXPECT_EQ(face.regime, cfd::turbulence::WallRegime::ViscousSublayer);
  }
  for (std::size_t i = 0; i < automatic.wallCells.size(); ++i) {
    EXPECT_TRUE(std::isfinite(automatic.wallCellSpecificDissipation[i]));
    EXPECT_GT(automatic.wallCellSpecificDissipation[i], 0.0);
  }
}

TEST(WallTreatmentMode, HighReReportsAUnitLogLayerWeight) {
  // This is WHY the high-Re path stays bitwise unchanged once SSTModel blends
  // the production on this weight: w = 1 makes the blend the identity.
  const auto highRe =
      treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::HighReWallFunction);
  ASSERT_GT(highRe.faces.size(), 0u);
  for (const auto& face : highRe.faces) {
    EXPECT_EQ(face.logLayerWeight, 1.0);
  }
  for (const Real weight : highRe.wallCellLogLayerWeight) {
    EXPECT_EQ(weight, 1.0);
  }

  // And under the automatic treatment on this buffer-layer mesh it must NOT be
  // 1, or the blend would be the identity there too and nothing would change.
  const auto automatic = treatBufferLayerChannel(cfd::turbulence::WallTreatmentMode::Automatic);
  ASSERT_EQ(automatic.wallCellLogLayerWeight.size(), automatic.wallCells.size());
  for (const Real weight : automatic.wallCellLogLayerWeight) {
    EXPECT_GT(weight, 0.0);
    EXPECT_LT(weight, 1.0) << "the automatic weight is saturated, so the blend does nothing";
  }
}
