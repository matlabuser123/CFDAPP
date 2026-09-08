// Grid-refinement evidence for the three nontrivial operators (gradient,
// Laplacian, upwind convection), using a genuinely smooth (non-
// polynomial) manufactured field -- see ManufacturedFields.hpp for why
// phi=x/y/x^2+y^2 are unsuitable for an *order* study (this scheme
// reproduces them exactly, so their error is flat noise at every
// resolution). Prints a Grid/h/L2-error/observed-order table for each
// operator (run with --gtest_also_run_disabled_tests or just look at
// stdout from a normal run) as gate evidence, not just pass/fail.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <vector>

#include "ManufacturedFields.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Laplacian.hpp"
#include "cfd/fields/SurfaceField.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

namespace {

struct RefinementPoint {
  Index n;
  Real h;
  Real error;
};

void printTable(const char* title, const std::vector<RefinementPoint>& points) {
  std::printf("\n%s\n", title);
  std::printf("%-10s %-12s %-16s %-12s\n", "Grid", "h", "L2 Error", "Observed p");
  for (std::size_t i = 0; i < points.size(); ++i) {
    char gridLabel[32];
    std::snprintf(gridLabel, sizeof(gridLabel), "%llux%llu",
                  static_cast<unsigned long long>(points[i].n),
                  static_cast<unsigned long long>(points[i].n));
    if (i == 0) {
      std::printf("%-10s %-12g %-16g %-12s\n", gridLabel, points[i].h, points[i].error, "--");
    } else {
      const Real p = cfd::test::observedOrder(points[i - 1].error, points[i].error);
      std::printf("%-10s %-12g %-16g %-12g\n", gridLabel, points[i].h, points[i].error, p);
    }
  }
  std::fflush(stdout);
}

const std::vector<Index> kGridSizes = {8, 16, 32, 64};

}  // namespace

TEST(GridRefinementTest, GradientOfSmoothFieldConvergesAtSecondOrder) {
  std::vector<RefinementPoint> points;

  for (const Index n : kGridSizes) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(n, n, 1.0, 1.0);
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);

    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
    }

    const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
    const Real error = cfd::test::l2CellErrorVector(mesh, grad, cfd::test::gradSmooth);
    points.push_back({n, 1.0 / static_cast<Real>(n), error});
  }

  printTable("Gradient  (phi = sin(pi x) cos(pi y))", points);

  for (std::size_t i = 1; i < points.size(); ++i) {
    EXPECT_LT(points[i].error, points[i - 1].error);
    const Real p = cfd::test::observedOrder(points[i - 1].error, points[i].error);
    EXPECT_GT(p, 1.7) << "gradient observed order too low between " << points[i - 1].n << " and "
                      << points[i].n;
  }
}

TEST(GridRefinementTest, LaplacianOfSmoothFieldConvergesAtSecondOrder) {
  std::vector<RefinementPoint> points;

  for (const Index n : kGridSizes) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(n, n, 1.0, 1.0);
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);

    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
    }

    const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);
    const Real error = cfd::test::l2CellError(mesh, lap, cfd::test::laplacianSmooth);
    points.push_back({n, 1.0 / static_cast<Real>(n), error});
  }

  printTable("Laplacian (phi = sin(pi x) cos(pi y))", points);

  for (std::size_t i = 1; i < points.size(); ++i) {
    EXPECT_LT(points[i].error, points[i - 1].error);
    const Real p = cfd::test::observedOrder(points[i - 1].error, points[i].error);
    EXPECT_GT(p, 1.7) << "laplacian observed order too low between " << points[i - 1].n << " and "
                      << points[i].n;
  }
}

TEST(GridRefinementTest, UpwindConvectionConvergesAtFirstOrder) {
  std::vector<RefinementPoint> points;

  const Vector2 velocity{1.0, 0.0};
  // div(U) = 0 for this constant velocity, so exact conv(phi) =
  // div(U*phi) = U . grad(phi).
  const auto exactConvection = [&](const Vector2& p) {
    return cfd::dot(velocity, cfd::test::gradSmooth(p));
  };

  for (const Index n : kGridSizes) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(n, n, 1.0, 1.0);
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);

    ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
    }

    SurfaceField flux(mesh.numberOfFaces());
    for (const auto& face : mesh.faces()) {
      flux[face.id()] = cfd::dot(velocity, face.areaVector());
    }

    const auto conv = cfd::discretization::convection(mesh, field, flux, boundaries);
    const Real error = cfd::test::l2CellError(mesh, conv, exactConvection);
    points.push_back({n, 1.0 / static_cast<Real>(n), error});
  }

  printTable("Upwind convection (U=(1,0), phi = sin(pi x) cos(pi y))", points);

  for (std::size_t i = 1; i < points.size(); ++i) {
    EXPECT_LT(points[i].error, points[i - 1].error);
    const Real p = cfd::test::observedOrder(points[i - 1].error, points[i].error);
    EXPECT_GT(p, 0.8) << "upwind observed order too low between " << points[i - 1].n << " and "
                      << points[i].n;
    EXPECT_LT(p, 1.3) << "upwind observed order suspiciously high (expected ~1) between "
                      << points[i - 1].n << " and " << points[i].n;
  }
}
