// P12-MESH-005 -- 3D legacy-VTK export (VTKWriter::writeCellFields) checked by
// parsing the written file back, never by eye in ParaView
// (results/p12-mesh-005/acceptance_gate.md, items E1-E4).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::VTKWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

std::filesystem::path outputPath(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_vtk3d_test";
  std::filesystem::create_directories(dir);
  return dir / name;
}

// The parsed legacy-VTK file.
struct ParsedVtk {
  std::vector<std::string> header;  // the first four lines
  std::vector<Vector3> points;
  std::vector<std::vector<Index>> cells;
  std::vector<int> cellTypes;
  Index cellDataCount{0};
  std::vector<std::pair<std::string, std::vector<Real>>> scalars;
  std::vector<std::pair<std::string, std::vector<Vector3>>> vectors;
};

ParsedVtk parse(const std::filesystem::path& path) {
  std::ifstream in(path);
  ParsedVtk vtk;
  std::string line;
  for (int k = 0; k < 4 && std::getline(in, line); ++k) vtk.header.push_back(line);
  std::string keyword;
  while (in >> keyword) {
    if (keyword == "POINTS") {
      Index count = 0;
      std::string type;
      in >> count >> type;
      vtk.points.resize(count);
      for (auto& p : vtk.points) in >> p.x >> p.y >> p.z;
    } else if (keyword == "CELLS") {
      Index count = 0;
      Index entries = 0;
      in >> count >> entries;
      Index read = 0;
      vtk.cells.resize(count);
      for (auto& cell : vtk.cells) {
        Index size = 0;
        in >> size;
        cell.resize(size);
        for (auto& id : cell) in >> id;
        read += size + 1;
      }
      EXPECT_EQ(read, entries) << "CELLS entry count";
    } else if (keyword == "CELL_TYPES") {
      Index count = 0;
      in >> count;
      vtk.cellTypes.resize(count);
      for (auto& t : vtk.cellTypes) in >> t;
    } else if (keyword == "CELL_DATA") {
      in >> vtk.cellDataCount;
    } else if (keyword == "SCALARS") {
      std::string name;
      std::string type;
      std::string components;
      std::string lookup;
      std::string table;
      in >> name >> type >> components >> lookup >> table;
      std::vector<Real> values(vtk.cellDataCount);
      for (auto& v : values) in >> v;
      vtk.scalars.emplace_back(name, values);
    } else if (keyword == "VECTORS") {
      std::string name;
      std::string type;
      in >> name >> type;
      std::vector<Vector3> values(vtk.cellDataCount);
      for (auto& v : values) in >> v.x >> v.y >> v.z;
      vtk.vectors.emplace_back(name, values);
    } else {
      ADD_FAILURE() << "unexpected VTK keyword " << keyword;
      break;
    }
  }
  return vtk;
}

// Volume and centroid of a hexahedron in VTK_HEXAHEDRON corner order, by
// splitting it into the six tetrahedra around the diagonal 0-6 (exact for a
// box, and independent of the mesh builder).
void hexahedron(const std::vector<Vector3>& p, Real& volume, Vector3& centroid) {
  static const int tets[6][4] = {{0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
                                 {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6}};
  volume = 0.0;
  centroid = Vector3{};
  for (const auto& t : tets) {
    const Vector3& a = p[static_cast<std::size_t>(t[0])];
    const Real v =
        dot(p[static_cast<std::size_t>(t[1])] - a,
            cross(p[static_cast<std::size_t>(t[2])] - a, p[static_cast<std::size_t>(t[3])] - a)) /
        6.0;
    Vector3 c = a;
    for (int k = 1; k < 4; ++k) c += p[static_cast<std::size_t>(t[k])];
    centroid += c * (0.25 * v);
    volume += v;
  }
  centroid *= 1.0 / volume;
}

}  // namespace

// E1-E3 on M5 (3 x 4 x 5, translated, non-cubic cells).
TEST(VTK3DTest, HexahedralMeshAndFieldsRoundTrip) {
  const Index nx = 3;
  const Index ny = 4;
  const Index nz = 5;
  const Vector3 origin{-1.2, 0.4, 3.1};
  const Real dx = 1.5 / 3.0;
  const Real dy = 0.7 / 4.0;
  const Real dz = 2.3 / 5.0;
  const Mesh mesh = MeshGeometry::createCartesian3D(nx, ny, nz, 1.5, 0.7, 2.3, origin);
  const Index n = mesh.numberOfCells();

  ScalarField temperature(n);
  ScalarField pressure(n);
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    const Vector3& c = cell.centroid();
    temperature[cell.id()] = std::sin(c.x) + (c.y * c.z);
    pressure[cell.id()] = 1.0 / 3.0 + static_cast<Real>(cell.id());
    velocity[cell.id()] = Vector3{c.x, -c.y / 7.0, std::exp(c.z) / 3.0};
  }
  const auto path = outputPath("m5.vtk");
  VTKWriter::writeCellFields(path, mesh, {{"temperature", temperature}, {"p", pressure}},
                             {{"velocity", velocity}});
  const ParsedVtk vtk = parse(path);

  // E1: header and points.
  ASSERT_EQ(vtk.header.size(), 4U);
  EXPECT_EQ(vtk.header[0], "# vtk DataFile Version 3.0");
  EXPECT_EQ(vtk.header[2], "ASCII");
  EXPECT_EQ(vtk.header[3], "DATASET UNSTRUCTURED_GRID");
  ASSERT_EQ(vtk.points.size(), (nx + 1) * (ny + 1) * (nz + 1));
  const auto pointId = [&](Index i, Index j, Index k) {
    return (((k * (ny + 1)) + j) * (nx + 1)) + i;
  };
  for (Index k = 0; k <= nz; ++k) {
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        const Vector3 expected{origin.x + (static_cast<Real>(i) * dx),
                               origin.y + (static_cast<Real>(j) * dy),
                               origin.z + (static_cast<Real>(k) * dz)};
        const Vector3& p = vtk.points[pointId(i, j, k)];
        EXPECT_LE(std::abs(p.x - expected.x), 1e-15 * std::abs(expected.x));
        EXPECT_LE(std::abs(p.y - expected.y), 1e-15 * std::abs(expected.y));
        EXPECT_LE(std::abs(p.z - expected.z), 1e-15 * std::abs(expected.z));
      }
    }
  }

  // E2: connectivity, cell types, and each hexahedron's geometry.
  ASSERT_EQ(vtk.cells.size(), n);
  ASSERT_EQ(vtk.cellTypes.size(), n);
  for (const int type : vtk.cellTypes) EXPECT_EQ(type, 12);
  for (Index k = 0; k < nz; ++k) {
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        const Index id = (((k * ny) + j) * nx) + i;
        const std::vector<Index> expected = {pointId(i, j, k),
                                             pointId(i + 1, j, k),
                                             pointId(i + 1, j + 1, k),
                                             pointId(i, j + 1, k),
                                             pointId(i, j, k + 1),
                                             pointId(i + 1, j, k + 1),
                                             pointId(i + 1, j + 1, k + 1),
                                             pointId(i, j + 1, k + 1)};
        EXPECT_EQ(vtk.cells[id], expected) << "cell " << id;
        std::vector<Vector3> corners;
        for (const Index p : vtk.cells[id]) corners.push_back(vtk.points[p]);
        Real volume = 0.0;
        Vector3 centroid;
        hexahedron(corners, volume, centroid);
        const auto& cell = mesh.cell(id);
        EXPECT_LE(std::abs(volume - cell.volume()), 1e-14 * cell.volume()) << "cell " << id;
        EXPECT_LE(magnitude(centroid - cell.centroid()), 1e-14 * magnitude(cell.centroid()));
      }
    }
  }

  // E3: cell data round-trips bitwise.
  EXPECT_EQ(vtk.cellDataCount, n);
  ASSERT_EQ(vtk.scalars.size(), 2U);
  EXPECT_EQ(vtk.scalars[0].first, "temperature");
  EXPECT_EQ(vtk.scalars[1].first, "p");
  ASSERT_EQ(vtk.vectors.size(), 1U);
  EXPECT_EQ(vtk.vectors[0].first, "velocity");
  for (Index id = 0; id < n; ++id) {
    EXPECT_EQ(vtk.scalars[0].second[id], temperature[id]);
    EXPECT_EQ(vtk.scalars[1].second[id], pressure[id]);
    EXPECT_TRUE(vtk.vectors[0].second[id] == velocity[id]) << "cell " << id;
  }
}

// E2 by hand on 2 x 1 x 1 (unit cube): 12 points, two hexahedra.
TEST(VTK3DTest, TwoCellConnectivityByHand) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  const auto path = outputPath("two.vtk");
  VTKWriter::writeCellFields(path, mesh, {}, {});
  const ParsedVtk vtk = parse(path);
  ASSERT_EQ(vtk.points.size(), 12U);
  // Points: i fastest, then j, then k: (0,0,0) (0.5,0,0) (1,0,0) (0,1,0) ... (1,1,1).
  EXPECT_TRUE(vtk.points[1] == (Vector3{0.5, 0.0, 0.0}));
  EXPECT_TRUE(vtk.points[5] == (Vector3{1.0, 1.0, 0.0}));
  EXPECT_TRUE(vtk.points[11] == (Vector3{1.0, 1.0, 1.0}));
  ASSERT_EQ(vtk.cells.size(), 2U);
  EXPECT_EQ(vtk.cells[0], (std::vector<Index>{0, 1, 4, 3, 6, 7, 10, 9}));
  EXPECT_EQ(vtk.cells[1], (std::vector<Index>{1, 2, 5, 4, 7, 8, 11, 10}));
  EXPECT_EQ(vtk.cellTypes, (std::vector<int>{12, 12}));
  EXPECT_EQ(vtk.cellDataCount, 2U);
}

// E4: invalid input is rejected before anything is written.
TEST(VTK3DTest, InvalidFieldsAreRejectedBeforeWriting) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  const auto path = outputPath("rejected.vtk");
  std::filesystem::remove(path);
  ScalarField bad(2, 1.0);
  bad[1] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(VTKWriter::writeCellFields(path, mesh, {{"t", bad}}), cfd::NumericalError);
  VectorField badVector(2, Vector3{1.0, 2.0, 3.0});
  badVector[0].z = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(VTKWriter::writeCellFields(path, mesh, {}, {{"u", badVector}}), cfd::NumericalError);
  EXPECT_THROW(VTKWriter::writeCellFields(path, mesh, {{"t", ScalarField(3, 1.0)}}),
               cfd::InvalidArgumentError);
  EXPECT_THROW(VTKWriter::writeCellFields(path, mesh, {{"two words", ScalarField(2, 1.0)}}),
               cfd::InvalidArgumentError);
  EXPECT_FALSE(std::filesystem::exists(path));
  // A mesh without a single structured grid (e.g. re-patched) cannot be exported this way.
  const Mesh bare(mesh.cells(), mesh.faces(), mesh.boundaryPatches());
  EXPECT_THROW(VTKWriter::writeCellFields(path, bare, {}), cfd::InvalidArgumentError);
}
