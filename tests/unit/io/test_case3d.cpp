// P12-MESH-006 gates G9.1 / G9.2 -- the 3D case format: geometry.json "box" (length, height,
// depth), mesh.json structured_cartesian with "nz", boundaries.json on the six patches xmin ..
// zmax with 3-component velocities, case.json 3-component initial velocity, solver.json
// "face_flux". Parsing, CaseBuilder construction, the lossless CaseWriter round trip, and every
// malformed 3D case rejected before anything is built with a message naming the file and field
// (ProjectRunner: InvalidCase, the CLI's exit code 2).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixture.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"

using cfd::CaseConfigurationError;
using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::io::CaseBuilder;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::testutil::CaseFixture;

namespace {

const char* const kPatches3D[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};

// boundaries.json for the six 3D patches; `extra` is appended to every patch (a temperature,
// species or alpha block), `velocityValue` is the zmax moving-wall value.
std::string boundaries3D(const std::string& extra = "",
                         const std::string& velocityValue = "[0.3, 0.0, 0.4]") {
  std::string s = R"({"patches": {)";
  s +=
      R"("xmin": {"velocity": {"type": "inlet", "value": [1.0, 0.1, -0.2]}, "pressure": {"type": "fixed_gradient", "value": 0.0})" +
      extra + "},";
  s +=
      R"("xmax": {"velocity": {"type": "outlet"}, "pressure": {"type": "fixed_value", "value": 0.0})" +
      extra + "},";
  s +=
      R"("ymin": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0})" +
      extra + "},";
  s +=
      R"("ymax": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0})" +
      extra + "},";
  s +=
      R"("zmin": {"velocity": {"type": "symmetry"}, "pressure": {"type": "fixed_gradient", "value": 0.0})" +
      extra + "},";
  s += R"("zmax": {"velocity": {"type": "moving_wall", "value": )" + velocityValue +
       R"(}, "pressure": {"type": "fixed_gradient", "value": 0.0})" + extra + "}";
  return s + "}}";
}

// A valid 3D case: a 2 x 1 x 0.5 box on 4 x 3 x 2 hexahedra.
void make3D(const CaseFixture& fixture) {
  fixture.write("case.json", R"({
    "name": "3D Test Case",
    "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
    "boundaries": "boundaries.json", "solver": "solver.json",
    "initial_conditions": {"velocity": [0.5, 0.0, 0.1], "pressure": 0.0}
  })");
  fixture.write("geometry.json", R"({"type": "box", "length": 2.0, "height": 1.0, "depth": 0.5})");
  fixture.write("mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 3, "nz": 2})");
  fixture.write("boundaries.json", boundaries3D());
  std::ifstream in(fixture.directory() / "solver.json");
  auto solver = nlohmann::json::parse(in);
  solver["face_flux"] = "rhie_chow";
  fixture.write("solver.json", solver.dump());
}

std::string errorFor(const CaseFixture& fixture) {
  try {
    (void)CaseReader{}.read(fixture.directory());
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "";
}

// Rejected by CaseReader with a message naming `file` and `field`, and by ProjectRunner as
// InvalidCase (exit code 2) without a solver result.
void expectRejected(const CaseFixture& fixture, const std::string& file, const std::string& field,
                    const std::string& label) {
  const std::string message = errorFor(fixture);
  EXPECT_NE(message.find(file), std::string::npos) << label << ": " << message;
  EXPECT_NE(message.find(field), std::string::npos) << label << ": " << message;
  const auto run = cfd::app::ProjectRunner::run(fixture.directory());
  EXPECT_EQ(run.status, cfd::app::ProjectRunStatus::InvalidCase) << label;
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 2) << label;
  EXPECT_FALSE(run.simpleResult.has_value()) << label;
  EXPECT_FALSE(run.mesh.has_value()) << label;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

constexpr const char* kLaminar =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01)";

}  // namespace

// --- G9.1 -------------------------------------------------------------------------------------

TEST(Case3DTest, ParsesTheThreeDimensionalCaseFormat) {
  CaseFixture fixture;
  make3D(fixture);
  const CaseDefinition d = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(cfd::io::geometryDimension(d.geometry), 3);
  EXPECT_EQ(d.geometry.type, "box");
  EXPECT_EQ(d.geometry.depth, 0.5);
  EXPECT_EQ(d.mesh.nx, 4u);
  EXPECT_EQ(d.mesh.ny, 3u);
  EXPECT_EQ(d.mesh.nz, 2u);
  EXPECT_EQ(cfd::io::meshPatchNames(d.mesh),
            (std::vector<std::string>{"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"}));
  EXPECT_EQ(d.boundaries.patches.size(), 6u);
  EXPECT_EQ(d.boundaries.patches.at("xmin").velocity.value, (Vector3{1.0, 0.1, -0.2}));
  EXPECT_EQ(d.boundaries.patches.at("zmax").velocity.value, (Vector3{0.3, 0.0, 0.4}));
  EXPECT_EQ(d.initialConditions.velocityComponents, 3);
  EXPECT_EQ(d.initialConditions.velocity, (Vector3{0.5, 0.0, 0.1}));
  EXPECT_EQ(d.solver.faceFlux, "rhie_chow");
}

TEST(Case3DTest, FaceFluxKeyParsesEveryValueAndDefaultsToAutomatic) {
  CaseFixture plain;  // 2D, no key
  EXPECT_EQ(CaseReader{}.read(plain.directory()).solver.faceFlux, "automatic");
  for (const char* value : {"automatic", "linear", "rhie_chow"}) {
    CaseFixture fixture;
    std::ifstream in(fixture.directory() / "solver.json");
    auto solver = nlohmann::json::parse(in);
    solver["face_flux"] = value;
    fixture.write("solver.json", solver.dump());
    const CaseDefinition d = CaseReader{}.read(fixture.directory());
    EXPECT_EQ(d.solver.faceFlux, value);
    EXPECT_EQ(
        cfd::pressure_velocity::faceFluxSchemeName(CaseBuilder{}.build(d).solverSettings.faceFlux),
        std::string(value));
  }
  CaseFixture bad;
  std::ifstream in(bad.directory() / "solver.json");
  auto solver = nlohmann::json::parse(in);
  solver["face_flux"] = "rhie-chow";
  bad.write("solver.json", solver.dump());
  const std::string message = errorFor(bad);
  EXPECT_NE(message.find("solver.json"), std::string::npos) << message;
  EXPECT_NE(message.find("face_flux"), std::string::npos) << message;
}

TEST(Case3DTest, CaseBuilderBuildsTheHexahedralMeshAndThreeComponentState) {
  CaseFixture fixture;
  make3D(fixture);
  const auto built = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  EXPECT_EQ(built.mesh.dimension(), 3);
  EXPECT_EQ(built.mesh.numberOfCells(), 24u);
  std::vector<std::string> names;
  for (const auto& patch : built.mesh.boundaryPatches()) names.push_back(patch.name());
  EXPECT_EQ(names, (std::vector<std::string>{"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"}));
  EXPECT_EQ(built.solverSettings.faceFlux, cfd::pressure_velocity::FaceFluxScheme::RhieChow);
  ASSERT_EQ(built.initialVelocity.size(), 24u);
  for (Index i = 0; i < 24; ++i) EXPECT_EQ(built.initialVelocity[i], (Vector3{0.5, 0.0, 0.1}));
  // Every 3D patch has a velocity and a pressure condition.
  for (const char* patch : kPatches3D) {
    EXPECT_TRUE(built.velocityBoundaries.has(patch)) << patch;
    EXPECT_TRUE(built.pressureBoundaries.has(patch)) << patch;
  }
  // Box extent.
  Vector3 extent{};
  for (const auto& cell : built.mesh.cells()) {
    extent.x = std::max(extent.x, cell.centroid().x);
    extent.z = std::max(extent.z, cell.centroid().z);
  }
  EXPECT_DOUBLE_EQ(extent.x, 2.0 - 0.25);
  EXPECT_DOUBLE_EQ(extent.z, 0.5 - 0.125);
}

TEST(Case3DTest, CaseWriterRoundTripIsLossless) {
  CaseFixture source;
  make3D(source);
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  const CaseDefinition reread = CaseReader{}.read(target.directory());
  EXPECT_EQ(reread.geometry.type, "box");
  EXPECT_EQ(reread.geometry.length, original.geometry.length);
  EXPECT_EQ(reread.geometry.height, original.geometry.height);
  EXPECT_EQ(reread.geometry.depth, original.geometry.depth);
  EXPECT_EQ(reread.mesh.nx, original.mesh.nx);
  EXPECT_EQ(reread.mesh.ny, original.mesh.ny);
  EXPECT_EQ(reread.mesh.nz, original.mesh.nz);
  ASSERT_EQ(reread.boundaries.patches.size(), 6u);
  for (const auto& [name, patch] : original.boundaries.patches) {
    const auto& other = reread.boundaries.patches.at(name);
    EXPECT_EQ(other.velocity.type, patch.velocity.type) << name;
    EXPECT_EQ(other.velocity.value, patch.velocity.value) << name;
    EXPECT_EQ(other.pressure.type, patch.pressure.type) << name;
    EXPECT_EQ(other.pressure.value, patch.pressure.value) << name;
  }
  EXPECT_EQ(reread.initialConditions.velocityComponents, 3);
  EXPECT_EQ(reread.initialConditions.velocity, original.initialConditions.velocity);
  EXPECT_EQ(reread.solver.faceFlux, "rhie_chow");
  // The written files carry the 3D keys.
  const auto geometry = nlohmann::json::parse(readFile(target.directory() / "geometry.json"));
  EXPECT_EQ(geometry.at("type"), "box");
  EXPECT_EQ(geometry.at("depth").get<Real>(), 0.5);
  const auto mesh = nlohmann::json::parse(readFile(target.directory() / "mesh.json"));
  EXPECT_EQ(mesh.at("nz").get<Index>(), 2u);
  const auto boundaries = nlohmann::json::parse(readFile(target.directory() / "boundaries.json"));
  EXPECT_EQ(boundaries.at("patches").at("zmax").at("velocity").at("value").size(), 3u);
  // Writing the reread definition again gives the same files (a fixed point).
  CaseFixture again;
  CaseWriter::write(again.directory(), reread);
  for (const char* file : {"case.json", "geometry.json", "mesh.json", "physics.json",
                           "boundaries.json", "solver.json"}) {
    EXPECT_EQ(readFile(again.directory() / file), readFile(target.directory() / file)) << file;
  }
}

TEST(Case3DTest, TwoDimensionalCaseWritesNoThreeDimensionalKeys) {
  CaseFixture source;  // the default 2D fixture
  CaseFixture target;
  CaseWriter::write(target.directory(), CaseReader{}.read(source.directory()));
  const auto geometry = nlohmann::json::parse(readFile(target.directory() / "geometry.json"));
  EXPECT_FALSE(geometry.contains("depth"));
  const auto mesh = nlohmann::json::parse(readFile(target.directory() / "mesh.json"));
  EXPECT_FALSE(mesh.contains("nz"));
  const auto solver = nlohmann::json::parse(readFile(target.directory() / "solver.json"));
  EXPECT_FALSE(solver.contains("face_flux"));
  const auto boundaries = nlohmann::json::parse(readFile(target.directory() / "boundaries.json"));
  EXPECT_EQ(boundaries.at("patches").at("top").at("velocity").at("value").size(), 2u);
}

// --- G9.2: every malformed 3D case is rejected before anything is built -------------------------

TEST(Case3DTest, RejectsMalformedMeshAndGeometry) {
  {
    CaseFixture f;  // box without nz
    make3D(f);
    f.write("mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 3})");
    expectRejected(f, "mesh.json", "nz", "box without nz");
  }
  {
    CaseFixture f;  // nz with a rectangle (2D fixture)
    f.write("mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 4, "nz": 2})");
    expectRejected(f, "mesh.json", "nz", "nz with a rectangle");
  }
  for (const char* nz : {"0", "-2", "1.5", "\"2\""}) {
    CaseFixture f;
    make3D(f);
    f.write("mesh.json",
            std::string(R"({"type": "structured_cartesian", "nx": 4, "ny": 3, "nz": )") + nz + "}");
    expectRejected(f, "mesh.json", "nz", std::string("nz = ") + nz);
  }
  {
    CaseFixture f;  // depth missing
    make3D(f);
    f.write("geometry.json", R"({"type": "box", "length": 2.0, "height": 1.0})");
    expectRejected(f, "geometry.json", "depth", "depth missing");
  }
  for (const char* depth : {"0.0", "-0.5", "\"0.5\""}) {
    CaseFixture f;
    make3D(f);
    f.write(
        "geometry.json",
        std::string(R"({"type": "box", "length": 2.0, "height": 1.0, "depth": )") + depth + "}");
    expectRejected(f, "geometry.json", "depth", std::string("depth = ") + depth);
  }
  {
    CaseFixture f;  // depth with a rectangle
    f.write("geometry.json",
            R"({"type": "rectangle", "length": 1.0, "height": 1.0, "depth": 1.0})");
    expectRejected(f, "geometry.json", "depth", "depth with a rectangle");
  }
  {
    CaseFixture f;  // grading with a box
    make3D(f);
    f.write("mesh.json",
            R"({"type": "structured_cartesian", "nx": 4, "ny": 3, "nz": 2,
                "grading": {"y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}}})");
    expectRejected(f, "mesh.json", "grading", "grading with a box");
  }
  {
    CaseFixture f;  // structured_quad with a box
    make3D(f);
    f.write("mesh.json", R"({"type": "structured_quad", "nx": 1, "ny": 1,
                             "vertices": [[0, 0], [2, 0], [0, 1], [2, 1]]})");
    expectRejected(f, "mesh.json", "type", "structured_quad with a box");
  }
  {
    CaseFixture f;  // multiblock with a box
    make3D(f);
    f.write("mesh.json", R"({"type": "multiblock", "blocks": [{"name": "a", "nx": 1, "ny": 1,
                             "vertices": [[0, 0], [2, 0], [0, 1], [2, 1]]}],
                             "patches": [{"name": "xmin", "sides": [{"block": "a", "side": "left"}]},
                                         {"name": "xmax", "sides": [{"block": "a", "side": "right"}]},
                                         {"name": "ymin", "sides": [{"block": "a", "side": "bottom"}]},
                                         {"name": "ymax", "sides": [{"block": "a", "side": "top"}]}]})");
    expectRejected(f, "mesh.json", "type", "multiblock with a box");
  }
}

TEST(Case3DTest, RejectsVelocitiesOfTheWrongDimensionAndTwoDimensionalPatchNames) {
  {
    CaseFixture f;  // 2-component boundary velocity in 3D
    make3D(f);
    f.write("boundaries.json", boundaries3D("", "[0.3, 0.0]"));
    expectRejected(f, "boundaries.json", "patches.zmax.velocity", "2-component velocity in 3D");
  }
  {
    CaseFixture f;  // 3-component boundary velocity in 2D
    f.write("boundaries.json", R"({"patches": {
      "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
      "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0}}}})");
    expectRejected(f, "boundaries.json", "patches.top.velocity", "3-component velocity in 2D");
  }
  {
    CaseFixture f;  // 2D patch names on a box
    make3D(f);
    CaseFixture plain;
    f.write("boundaries.json", readFile(plain.directory() / "boundaries.json"));
    expectRejected(f, "boundaries.json", "patches.", "2D patch names in 3D");
  }
  {
    CaseFixture f;  // a 3D patch missing
    make3D(f);
    auto b = nlohmann::json::parse(boundaries3D());
    b["patches"].erase("zmin");
    f.write("boundaries.json", b.dump());
    expectRejected(f, "boundaries.json", "patches.zmin", "zmin missing");
  }
  {
    CaseFixture f;  // 2-component initial velocity in 3D
    make3D(f);
    f.write("case.json", R"({
      "name": "3D Test Case",
      "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
      "boundaries": "boundaries.json", "solver": "solver.json",
      "initial_conditions": {"velocity": [0.5, 0.0], "pressure": 0.0}})");
    expectRejected(f, "case.json", "initial_conditions.velocity",
                   "2-component initial velocity in 3D");
  }
  {
    CaseFixture f;  // 3-component initial velocity in 2D
    f.write("case.json", R"({
      "name": "Test Case",
      "geometry": "geometry.json", "mesh": "mesh.json", "physics": "physics.json",
      "boundaries": "boundaries.json", "solver": "solver.json",
      "initial_conditions": {"velocity": [0.5, 0.0, 0.1], "pressure": 0.0}})");
    expectRejected(f, "case.json", "initial_conditions.velocity",
                   "3-component initial velocity in 2D");
  }
}

TEST(Case3DTest, RejectsPhysicsThat3DDoesNotSupport) {
  struct Variant {
    const char* label;
    std::string physics;
    std::string patchExtra;
    const char* field;
  };
  const std::vector<Variant> variants = {
      {"thermal",
       std::string(kLaminar) +
           R"(, "thermal": {"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0}})",
       R"(, "temperature": {"type": "adiabatic"})", "thermal"},
      {"thermal + buoyancy",
       std::string(kLaminar) +
           R"(, "thermal": {"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0},
              "buoyancy": {"model": "boussinesq", "beta": 0.0034, "reference_temperature": 300.0,
                           "gravity": [0.0, -9.81]}})",
       R"(, "temperature": {"type": "adiabatic"})", "buoyancy"},
      {"turbulence k_epsilon",
       std::string(kLaminar) +
           R"(, "turbulence": {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005}})",
       "", "turbulence"},
      {"turbulence sst",
       std::string(kLaminar) +
           R"(, "turbulence": {"model": "sst", "initial_k": 0.02, "initial_omega": 12.0}})",
       "", "turbulence"},
      {"species",
       std::string(kLaminar) +
           R"(, "species": [{"name": "CO2", "diffusivity": 1.6e-5, "initial_concentration": 0.0}]})",
       R"(, "species": {"CO2": {"type": "fixed_gradient", "value": 0.0}})", "species"},
      {"multiphase",
       std::string(
           R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0e-5)") +
           R"(, "multiphase": {
          "phase1": {"name": "water", "density": 1000.0, "viscosity": 0.001},
          "phase2": {"name": "air", "density": 1.0, "viscosity": 1.8e-5},
          "initial_alpha": 0.5, "transport_time_step": 0.01}})",
       R"(, "alpha": {"type": "fixed_gradient", "value": 0.0})", "multiphase"},
      {"compressible",
       std::string(kLaminar) +
           R"(, "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                          "reference_pressure": 101325.0, "temperature": 300.0}})",
       "", "compressible"},
  };
  for (const Variant& v : variants) {
    CaseFixture f;
    make3D(f);
    f.write("physics.json", v.physics);
    f.write("boundaries.json", boundaries3D(v.patchExtra));
    const std::string message = errorFor(f);
    EXPECT_NE(message.find("3D"), std::string::npos) << v.label << ": " << message;
    expectRejected(f, "physics.json", v.field, v.label);
  }
  // A laminar "turbulence" block is laminar flow: accepted.
  CaseFixture laminar;
  make3D(laminar);
  laminar.write(
      "physics.json",
      std::string(kLaminar) +
          R"(, "turbulence": {"model": "laminar", "initial_k": 0.02, "initial_epsilon": 0.005}})");
  EXPECT_NO_THROW((void)CaseReader{}.read(laminar.directory()));
}
