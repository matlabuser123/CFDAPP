// P12-MESH-005 -- scalar and vector fields on a 3D mesh
// (results/p12-mesh-005/acceptance_gate.md, items B1 and B2). The fields are
// the same ScalarField / VectorField types the 2D code uses: a scalar field
// is one value per cell whatever the dimension, and a vector field always
// has three components (u, v, w).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/validation/ErrorNorms.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::Vector3;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// M5 of the gate: 3 x 4 x 5 cells on [-1.2, 0.3] x [0.4, 1.1] x [3.1, 5.4].
Mesh cuboid() {
  return MeshGeometry::createCartesian3D(3, 4, 5, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1});
}

ScalarField sample(const Mesh& mesh, const std::function<Real(const Vector3&)>& f) {
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}

}  // namespace

// B1: constant, x, y, z and x + 2y + 3z on the 3D mesh; the volume-weighted
// mean of a linear field is its value at the domain centre (the midpoint rule
// is exact for linear integrands), which checks centroids and volumes together.
TEST(Fields3DTest, ScalarFieldsSampleLinearFieldsWithExactVolumeMeans) {
  const Mesh mesh = cuboid();
  const Vector3 centre{-1.2 + 0.75, 0.4 + 0.35, 3.1 + 1.15};
  const std::vector<std::pair<const char*, std::function<Real(const Vector3&)>>> fields = {
      {"constant", [](const Vector3&) { return 4.25; }},
      {"x", [](const Vector3& p) { return p.x; }},
      {"y", [](const Vector3& p) { return p.y; }},
      {"z", [](const Vector3& p) { return p.z; }},
      {"x + 2y + 3z", [](const Vector3& p) { return p.x + (2.0 * p.y) + (3.0 * p.z); }},
  };
  for (const auto& [name, f] : fields) {
    SCOPED_TRACE(name);
    const ScalarField field = sample(mesh, f);
    ASSERT_EQ(field.size(), mesh.numberOfCells());
    ASSERT_EQ(field.size(), 60U);
    for (const auto& cell : mesh.cells()) EXPECT_EQ(field[cell.id()], f(cell.centroid()));
    const Real mean = cfd::validation::volumeWeightedMean(mesh, field);
    EXPECT_LE(std::abs(mean - f(centre)), 1e-13 * std::abs(f(centre)));
  }
  // z genuinely varies over the mesh (not a 2D field with z = 0).
  const ScalarField z = sample(mesh, [](const Vector3& p) { return p.z; });
  Real zmin = z[0];
  Real zmax = z[0];
  for (std::size_t i = 0; i < z.size(); ++i) {
    zmin = std::min(zmin, z[i]);
    zmax = std::max(zmax, z[i]);
  }
  EXPECT_NEAR(zmax - zmin, 2.3 - 0.46, 1e-13);
}

// B2: three-component vector fields.
TEST(Fields3DTest, VectorFieldsCarryThreeComponents) {
  const Mesh mesh = cuboid();
  const std::size_t n = mesh.numberOfCells();

  const VectorField zero(n);
  ASSERT_EQ(zero.size(), n);
  for (std::size_t i = 0; i < n; ++i) EXPECT_TRUE(zero[i] == Vector3{});

  VectorField field(n, Vector3{1.0, -2.0, 3.0});
  for (std::size_t i = 0; i < n; ++i) EXPECT_TRUE(field[i] == (Vector3{1.0, -2.0, 3.0}));

  // Component access and assignment of all three components.
  field[7].x = 2.0;
  field[7].y = 3.0;
  field[7].z = 6.0;
  EXPECT_EQ(field[7].x, 2.0);
  EXPECT_EQ(field[7].y, 3.0);
  EXPECT_EQ(field[7].z, 6.0);
  EXPECT_EQ(magnitude(field[7]), 7.0);  // sqrt(4 + 9 + 36)
  field.at(8) = Vector3{0.0, 0.0, -5.0};
  EXPECT_EQ(magnitude(field.at(8)), 5.0);
  EXPECT_THROW((void)field.at(n), std::out_of_range);

  // Copy and move keep w.
  const VectorField copy = field;
  EXPECT_TRUE(copy[7] == (Vector3{2.0, 3.0, 6.0}));
  VectorField moved = std::move(field);
  EXPECT_EQ(moved.size(), n);
  EXPECT_TRUE(moved[7] == (Vector3{2.0, 3.0, 6.0}));
  EXPECT_TRUE(moved[8] == (Vector3{0.0, 0.0, -5.0}));

  // The field arithmetic acts on w.
  const VectorField a(n, Vector3{1.0, 2.0, 3.0});
  const VectorField b(n, Vector3{0.5, -1.0, 4.0});
  EXPECT_TRUE((a + b)[0] == (Vector3{1.5, 1.0, 7.0}));
  EXPECT_TRUE((a - b)[0] == (Vector3{0.5, 3.0, -1.0}));
  EXPECT_TRUE((a * 2.0)[0] == (Vector3{2.0, 4.0, 6.0}));
  EXPECT_TRUE((2.0 * a)[0] == (Vector3{2.0, 4.0, 6.0}));
  EXPECT_TRUE((a / 4.0)[0] == (Vector3{0.25, 0.5, 0.75}));
  VectorField c = a;
  c += b;
  c -= a;
  EXPECT_TRUE(c[0] == b[0]);
  EXPECT_THROW(c += VectorField(n + 1), cfd::InvalidArgumentError);

  // A 2D-constructed value has w = 0.
  const VectorField planar(n, Vector2{1.0, 2.0});
  EXPECT_EQ(planar[0].z, 0.0);

  // The vector error norms see w: an error only in w.
  VectorField numeric(n, Vector3{1.0, 2.0, 3.0});
  numeric[0].z = 3.5;
  const auto norms = cfd::validation::computeVectorErrorNorms(mesh, numeric, a);
  EXPECT_EQ(norms.x.linf, 0.0);
  EXPECT_EQ(norms.y.linf, 0.0);
  EXPECT_EQ(norms.z.linf, 0.5);
  EXPECT_EQ(norms.magnitude.linf, 0.5);
}
