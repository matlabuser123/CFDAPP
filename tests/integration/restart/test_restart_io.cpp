// P2 -- Restart capability, Restart-C/D: RestartWriter writes exactly
// what RestartReader reads back, and the reader rejects every class of
// invalid/corrupt/incompatible restart file this project's own
// validateRestartSnapshot and JSON-parsing conventions define.
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/RestartReader.hpp"
#include "cfd/io/RestartWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/solver/RestartSnapshot.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::LinearSolverSettings;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::io::RestartReader;
using cfd::io::RestartWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::kRestartFormatVersion;
using cfd::solver::makeRestartSnapshot;
using cfd::solver::RestartSnapshot;
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;

namespace {

std::filesystem::path tempPath(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_restart_integration";
  std::filesystem::create_directories(dir);
  return dir / name;
}

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh, Vector2 lidVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(lidVelocity));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

PISOSettings makeProbeSettings() {
  LinearSolverSettings linear;
  linear.maxIterations = 500;
  linear.absoluteTolerance = 1e-12;
  linear.relativeTolerance = 1e-10;
  PISOSettings settings;
  settings.momentumSolver = linear;
  settings.pressureSolver = linear;
  return settings;
}

// A real accepted PISO state -- the same style of fixture
// RestartSnapshotTest's own strongest test uses.
RestartSnapshot makeRealSnapshot(const Mesh& mesh) {
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  TransientState initialState;
  initialState.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  initialState.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  initialState.massFlux = calculateMassFlux(mesh, initialState.velocity, fluid, velocityBoundaries);

  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.03, 0.01, 100));
  if (result.status != TransientStatus::Completed || result.history.empty()) {
    throw std::runtime_error("makeRealSnapshot: fixture run did not complete");
  }
  const auto& lastRecord = result.history.back();
  return makeRestartSnapshot(mesh, result.finalState, lastRecord.time, lastRecord.deltaT,
                             lastRecord.step);
}

std::string readWholeFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void writeWholeFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream out(path);
  out << content;
}

}  // namespace

// --- Round trip -------------------------------------------------------

TEST(RestartIOTest, WriteThenReadRoundTripsExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot original = makeRealSnapshot(mesh);
  const auto path = tempPath("round_trip.json");

  RestartWriter::write(path, original);
  const RestartSnapshot loaded = RestartReader::read(path, mesh);

  EXPECT_EQ(loaded.formatVersion, original.formatVersion);
  EXPECT_EQ(loaded.time, original.time);
  EXPECT_EQ(loaded.step, original.step);
  EXPECT_EQ(loaded.deltaT, original.deltaT);
  EXPECT_EQ(loaded.cellCount, original.cellCount);
  EXPECT_EQ(loaded.faceCount, original.faceCount);
  EXPECT_EQ(loaded.meshFingerprint, original.meshFingerprint);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(loaded.velocity[i].x, original.velocity[i].x) << "cell " << i;
    EXPECT_EQ(loaded.velocity[i].y, original.velocity[i].y) << "cell " << i;
    EXPECT_EQ(loaded.pressure[i], original.pressure[i]) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(loaded.massFlux[i], original.massFlux[i]) << "face " << i;
  }
}

TEST(RestartIOTest, RepeatedWriteIsByteIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto pathA = tempPath("repeat_a.json");
  const auto pathB = tempPath("repeat_b.json");

  RestartWriter::write(pathA, snapshot);
  RestartWriter::write(pathB, snapshot);

  EXPECT_EQ(readWholeFile(pathA), readWholeFile(pathB));
}

// --- Failure paths ------------------------------------------------------

TEST(RestartIOTest, ReadRejectsMissingFile) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto path = tempPath("does_not_exist.json");
  std::filesystem::remove(path);

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::IOError);
}

TEST(RestartIOTest, ReadRejectsMalformedJsonSyntax) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto path = tempPath("malformed.json");
  writeWholeFile(path, "{ this is not valid JSON ");

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::IOError);
}

TEST(RestartIOTest, ReadRejectsTruncatedFile) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("truncate_source.json");
  RestartWriter::write(validPath, snapshot);
  const std::string content = readWholeFile(validPath);

  const auto path = tempPath("truncated.json");
  writeWholeFile(path, content.substr(0, content.size() / 2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::IOError);
}

TEST(RestartIOTest, ReadRejectsMissingRequiredField) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("missing_field_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  doc.erase("state");  // required top-level object removed entirely

  const auto path = tempPath("missing_field.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::CaseConfigurationError);
}

TEST(RestartIOTest, ReadRejectsWrongFormatVersion) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("wrong_version_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  doc["format_version"] = kRestartFormatVersion + 1;

  const auto path = tempPath("wrong_version.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::InvalidArgumentError);
}

TEST(RestartIOTest, ReadRejectsDifferentMeshWithSameCounts) {
  const Mesh meshA = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh meshB = MeshGeometry::createCartesian2D(4, 4, 2.0, 1.0);
  ASSERT_EQ(meshA.numberOfCells(), meshB.numberOfCells());
  ASSERT_EQ(meshA.numberOfFaces(), meshB.numberOfFaces());

  const RestartSnapshot snapshot = makeRealSnapshot(meshA);
  const auto path = tempPath("wrong_mesh_fingerprint.json");
  RestartWriter::write(path, snapshot);

  EXPECT_THROW(RestartReader::read(path, meshB), cfd::InvalidArgumentError);
}

TEST(RestartIOTest, ReadRejectsCellCountMismatch) {
  const Mesh smallMesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Mesh bigMesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);

  const RestartSnapshot snapshot = makeRealSnapshot(smallMesh);
  const auto path = tempPath("cell_count_mismatch.json");
  RestartWriter::write(path, snapshot);

  EXPECT_THROW(RestartReader::read(path, bigMesh), cfd::InvalidArgumentError);
}

TEST(RestartIOTest, ReadRejectsIncompatiblePressureArrayLength) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("bad_pressure_length_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  auto& pressure = doc["fields"]["pressure"];
  pressure.erase(pressure.begin());  // one element short of cellCount

  const auto path = tempPath("bad_pressure_length.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::InvalidArgumentError);
}

TEST(RestartIOTest, ReadRejectsOutOfRangeJsonNumber) {
  // Standard JSON has no way to encode IEEE NaN/Infinity directly, and
  // (discovered while writing this test) nlohmann::json does not
  // silently overflow a too-large literal like "1e400" to +Infinity
  // either -- it throws json::out_of_range while parsing the document,
  // before any field is ever read. That exception happens inside
  // readJsonFile's own parse step, so this is a malformed-document
  // rejection (IOError), not a validateRestartSnapshot finite-value
  // rejection (InvalidArgumentError) -- confirmed a real gap in
  // JsonUtil.cpp::readJsonFile (only catching json::parse_error, not the
  // sibling json::out_of_range) and fixed it there, shared by both
  // RestartReader and the pre-existing CaseReader pipeline.
  //
  // Since the failure happens at the whole-document parse step, this
  // does not need a schema-matching document at all -- any JSON text
  // containing an out-of-range number fails to parse.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto path = tempPath("overflow.json");
  writeWholeFile(path, R"({"fields": {"pressure": [1e400]}})");

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::IOError);
}

TEST(RestartIOTest, ReadRejectsNonNumericFieldValue) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("non_numeric_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  doc["fields"]["pressure"][0] = "not a number";

  const auto path = tempPath("non_numeric.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::CaseConfigurationError);
}

TEST(RestartIOTest, ReadRejectsInvalidDeltaT) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("bad_deltat_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  doc["state"]["delta_t"] = 0.0;

  const auto path = tempPath("bad_deltat.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::InvalidArgumentError);
}

TEST(RestartIOTest, ReadRejectsMismatchedVelocityComponentArrayLengths) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const RestartSnapshot snapshot = makeRealSnapshot(mesh);
  const auto validPath = tempPath("mismatched_velocity_source.json");
  RestartWriter::write(validPath, snapshot);

  nlohmann::json doc;
  {
    std::ifstream in(validPath);
    in >> doc;
  }
  auto& velocityY = doc["fields"]["velocity_y"];
  velocityY.erase(velocityY.begin());

  const auto path = tempPath("mismatched_velocity.json");
  writeWholeFile(path, doc.dump(2));

  EXPECT_THROW(RestartReader::read(path, mesh), cfd::CaseConfigurationError);
}
