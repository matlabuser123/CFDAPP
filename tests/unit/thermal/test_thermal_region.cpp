#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/ThermalInterface.hpp"
#include "cfd/thermal/ThermalRegionMap.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedTemperature;
using cfd::fields::ScalarField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::assembleRegionAwareThermalDiffusionContribution;
using cfd::thermal::assembleThermalDiffusionContribution;
using cfd::thermal::interfaceConductance;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalRegion;
using cfd::thermal::ThermalRegionMap;
using cfd::thermal::ThermalRegionType;

// ---------------------------------------------------------------------
// ThermalRegionMap
// ---------------------------------------------------------------------

TEST(ThermalRegionMapTest, ConstructsAndReportsCellRegions) {
  std::vector<ThermalRegion> regions{
      ThermalRegion{"fluid", ThermalRegionType::Fluid, ThermalProperties(0.6, 4180.0)},
      ThermalRegion{"solid", ThermalRegionType::Solid, ThermalProperties(15.0, 500.0)},
  };
  const ThermalRegionMap map(regions, {0, 0, 1, 1});

  EXPECT_EQ(map.numberOfCells(), 4u);
  EXPECT_EQ(map.regionForCell(0).name, "fluid");
  EXPECT_EQ(map.regionForCell(1).name, "fluid");
  EXPECT_EQ(map.regionForCell(2).name, "solid");
  EXPECT_EQ(map.regionForCell(3).name, "solid");
  EXPECT_TRUE(map.sameRegion(0, 1));
  EXPECT_TRUE(map.sameRegion(2, 3));
  EXPECT_FALSE(map.sameRegion(1, 2));
  EXPECT_EQ(map.regions().size(), 2u);
}

TEST(ThermalRegionMapTest, RejectsEmptyRegions) {
  EXPECT_THROW((ThermalRegionMap({}, {0})), InvalidArgumentError);
}

TEST(ThermalRegionMapTest, RejectsDuplicateRegionNames) {
  std::vector<ThermalRegion> regions{
      ThermalRegion{"a", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
      ThermalRegion{"a", ThermalRegionType::Solid, ThermalProperties(2.0, 2.0)},
  };
  EXPECT_THROW((ThermalRegionMap(regions, {0, 1})), InvalidArgumentError);
}

TEST(ThermalRegionMapTest, RejectsEmptyCellRegionIndex) {
  std::vector<ThermalRegion> regions{
      ThermalRegion{"a", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
  };
  EXPECT_THROW((ThermalRegionMap(regions, {})), InvalidArgumentError);
}

TEST(ThermalRegionMapTest, RejectsOutOfRangeRegionIndex) {
  std::vector<ThermalRegion> regions{
      ThermalRegion{"a", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
  };
  // Only region index 0 exists -- 1 is unknown.
  EXPECT_THROW((ThermalRegionMap(regions, {0, 1})), InvalidArgumentError);
}

TEST(ThermalRegionMapTest, RegionForCellOutOfRangeThrows) {
  std::vector<ThermalRegion> regions{
      ThermalRegion{"a", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
  };
  const ThermalRegionMap map(regions, {0, 0});
  EXPECT_THROW((void)map.regionForCell(2), InvalidArgumentError);
}

// ---------------------------------------------------------------------
// interfaceConductance -- hand-derived (this file's own worked example,
// exactly the one in the task spec): k1=2, d1=0.25, k2=8, d2=0.25, A=1
// -> R = 0.25/2 + 0.25/8 = 0.125 + 0.03125 = 0.15625 -> G = 6.4.
// ---------------------------------------------------------------------

TEST(InterfaceConductanceTest, MatchesHandDerivedExample) {
  const Real g = interfaceConductance(/*k1=*/2.0, /*d1=*/0.25, /*k2=*/8.0, /*d2=*/0.25,
                                      /*area=*/1.0);
  EXPECT_NEAR(g, 6.4, 1e-12);
}

TEST(InterfaceConductanceTest, EqualConductivityReducesToPlainFormula) {
  // k1==k2==k, d1==d2==d: G = A/(d/k+d/k) = A*k/(2d) = k*A/(d1+d2), the
  // exact single-material k*A/dPN formula with dPN=d1+d2.
  const Real k = 5.0, d = 0.3, area = 2.0;
  const Real g = interfaceConductance(k, d, k, d, area);
  EXPECT_NEAR(g, k * area / (2.0 * d), 1e-12);
}

TEST(InterfaceConductanceTest, RejectsNonPositiveConductivity) {
  EXPECT_THROW((void)interfaceConductance(0.0, 0.25, 8.0, 0.25, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)interfaceConductance(2.0, 0.25, -8.0, 0.25, 1.0), InvalidArgumentError);
}

TEST(InterfaceConductanceTest, RejectsNonPositiveDistance) {
  EXPECT_THROW((void)interfaceConductance(2.0, 0.0, 8.0, 0.25, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)interfaceConductance(2.0, 0.25, 8.0, -0.1, 1.0), InvalidArgumentError);
}

TEST(InterfaceConductanceTest, RejectsNonFiniteInputs) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)interfaceConductance(nan, 0.25, 8.0, 0.25, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)interfaceConductance(2.0, inf, 8.0, 0.25, 1.0), InvalidArgumentError);
}

// ---------------------------------------------------------------------
// assembleRegionAwareThermalDiffusionContribution
// ---------------------------------------------------------------------

namespace {

BoundaryConditionSet makeFourPatchTemperatureBoundaries(const Mesh& mesh, Real value) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedTemperature>(value));
  }
  return boundaries;
}

Index internalFaceBetween(const Mesh& mesh, Index cellA, Index cellB) {
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Index owner = face.owner();
    const Index neighbor = *face.neighbor();
    if ((owner == cellA && neighbor == cellB) || (owner == cellB && neighbor == cellA)) {
      return face.id();
    }
  }
  ADD_FAILURE() << "no internal face between cells " << cellA << " and " << cellB;
  return 0;
}

}  // namespace

TEST(RegionAwareThermalDiffusionTest, InterfaceCoefficientMatchesHandDerivedExample) {
  // 4x1 mesh, length 2.0 x 1.0 -> dx=0.5, dy=1.0. Cells 0,1 in region A
  // (k=2), cells 2,3 in region B (k=8). The cell1-cell2 interior face has
  // d1=d2=dx/2=0.25, area=dy=1.0 -- exactly this file's own worked
  // example above: G=6.4.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 1, 2.0, 1.0);
  std::vector<ThermalRegion> regionList{
      ThermalRegion{"A", ThermalRegionType::Solid, ThermalProperties(2.0, 1.0)},
      ThermalRegion{"B", ThermalRegionType::Solid, ThermalProperties(8.0, 1.0)},
  };
  const ThermalRegionMap regions(regionList, {0, 0, 1, 1});
  const auto boundaries = makeFourPatchTemperatureBoundaries(mesh, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  cfd::algebra::SparseMatrixBuilder builder(n, n);
  cfd::algebra::Vector rhs(n, 0.0);
  assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature, boundaries, builder,
                                                  rhs);
  const auto matrix = builder.build();

  const Index interfaceFace = internalFaceBetween(mesh, 1, 2);
  const Index owner = mesh.face(interfaceFace).owner();
  const Index neighbor = *mesh.face(interfaceFace).neighbor();

  cfd::algebra::Vector eOwner(n, 0.0);
  eOwner[owner] = 1.0;
  cfd::algebra::Vector eNeighbor(n, 0.0);
  eNeighbor[neighbor] = 1.0;
  // Off-diagonal coupling magnitude is exactly the interface conductance.
  EXPECT_NEAR(-matrix.multiply(eOwner)[neighbor], 6.4, 1e-10);
  EXPECT_NEAR(-matrix.multiply(eNeighbor)[owner], 6.4, 1e-10);
  // Owner-neighbour symmetry.
  EXPECT_NEAR(matrix.multiply(eOwner)[neighbor], matrix.multiply(eNeighbor)[owner], 1e-12);
}

TEST(RegionAwareThermalDiffusionTest, EqualConductivityMatchesSingleMaterialPathExactly) {
  // Two DIFFERENT regions that happen to share the same conductivity
  // must reproduce the exact same matrix as the plain single-material
  // assembleThermalDiffusionContribution -- the legacy-equivalence
  // regression the task spec requires.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real k = 3.0;
  std::vector<ThermalRegion> regionList{
      ThermalRegion{"A", ThermalRegionType::Fluid, ThermalProperties(k, 1.0)},
      ThermalRegion{"B", ThermalRegionType::Solid, ThermalProperties(k, 1.0)},
  };
  std::vector<Index> cellRegionIndex(mesh.numberOfCells());
  for (Index i = 0; i < cellRegionIndex.size(); ++i) cellRegionIndex[i] = i % 2;  // interleaved.
  const ThermalRegionMap regions(regionList, cellRegionIndex);
  const auto boundaries = makeFourPatchTemperatureBoundaries(mesh, 7.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  cfd::algebra::SparseMatrixBuilder regionBuilder(n, n);
  cfd::algebra::Vector regionRhs(n, 0.0);
  assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature, boundaries,
                                                  regionBuilder, regionRhs);
  const auto regionMatrix = regionBuilder.build();

  cfd::algebra::SparseMatrixBuilder plainBuilder(n, n);
  cfd::algebra::Vector plainRhs(n, 0.0);
  assembleThermalDiffusionContribution(mesh, k, temperature, boundaries, plainBuilder, plainRhs);
  const auto plainMatrix = plainBuilder.build();

  for (Index i = 0; i < n; ++i) {
    cfd::algebra::Vector e(n, 0.0);
    e[i] = 1.0;
    const auto regionColumn = regionMatrix.multiply(e);
    const auto plainColumn = plainMatrix.multiply(e);
    for (Index j = 0; j < n; ++j) {
      EXPECT_DOUBLE_EQ(regionColumn[j], plainColumn[j]) << "cell " << i << " -> " << j;
    }
    EXPECT_DOUBLE_EQ(regionRhs[i], plainRhs[i]) << "cell " << i;
  }
}

TEST(RegionAwareThermalDiffusionTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  std::vector<ThermalRegion> regionList{
      ThermalRegion{"A", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
  };
  const ThermalRegionMap regions(regionList, {0, 0});
  const auto boundaries = makeFourPatchTemperatureBoundaries(mesh, 0.0);
  const ScalarField temperature(mesh.numberOfCells() + 1, 0.0);
  cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  cfd::algebra::Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature,
                                                                boundaries, builder, rhs),
               InvalidArgumentError);
}

TEST(RegionAwareThermalDiffusionTest, MismatchedRegionSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  std::vector<ThermalRegion> regionList{
      ThermalRegion{"A", ThermalRegionType::Fluid, ThermalProperties(1.0, 1.0)},
  };
  const ThermalRegionMap regions(regionList, {0});  // one too few.
  const auto boundaries = makeFourPatchTemperatureBoundaries(mesh, 0.0);
  const ScalarField temperature(mesh.numberOfCells(), 0.0);
  cfd::algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  cfd::algebra::Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature,
                                                                boundaries, builder, rhs),
               InvalidArgumentError);
}

TEST(RegionAwareThermalDiffusionTest, RepeatedAssemblyIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  std::vector<ThermalRegion> regionList{
      ThermalRegion{"A", ThermalRegionType::Fluid, ThermalProperties(2.0, 1.0)},
      ThermalRegion{"B", ThermalRegionType::Solid, ThermalProperties(8.0, 1.0)},
  };
  std::vector<Index> cellRegionIndex(mesh.numberOfCells());
  for (Index i = 0; i < cellRegionIndex.size(); ++i) cellRegionIndex[i] = (i < 8) ? 0 : 1;
  const ThermalRegionMap regions(regionList, cellRegionIndex);
  const auto boundaries = makeFourPatchTemperatureBoundaries(mesh, 5.0);
  const Index n = mesh.numberOfCells();
  const ScalarField temperature(n, 0.0);

  cfd::algebra::SparseMatrixBuilder builderA(n, n);
  cfd::algebra::Vector rhsA(n, 0.0);
  assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature, boundaries,
                                                  builderA, rhsA);
  cfd::algebra::SparseMatrixBuilder builderB(n, n);
  cfd::algebra::Vector rhsB(n, 0.0);
  assembleRegionAwareThermalDiffusionContribution(mesh, regions, temperature, boundaries,
                                                  builderB, rhsB);

  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(rhsA[i], rhsB[i]);
  }
}
