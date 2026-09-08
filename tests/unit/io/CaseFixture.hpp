#pragma once

// Shared helper for tests/unit/io/*.cpp: builds a fresh, valid case
// directory under a unique temporary path, letting each test overwrite
// exactly the one file/field it wants to break before calling
// CaseReader::read() (TODO.md P1 section 35: temporary directories, not
// mutated production/fixture cases, for one-off invalid variants --
// tests/data/cases/* stays for a handful of committed, named scenarios).

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace cfd::testutil {

// A case directory deleted (recursively) when the fixture goes out of
// scope -- RAII rather than leaving temp directories behind on every
// test run.
class CaseFixture {
 public:
  CaseFixture() {
    directory_ =
        std::filesystem::temp_directory_path() /
        ("cfdapp_case_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory_);
    writeValidFiles();
  }

  ~CaseFixture() {
    std::error_code ec;
    std::filesystem::remove_all(directory_, ec);
  }

  CaseFixture(const CaseFixture&) = delete;
  CaseFixture& operator=(const CaseFixture&) = delete;

  [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

  // Overwrites one file in the case directory (e.g. "physics.json") with
  // arbitrary content -- valid JSON with a deliberate defect, or
  // deliberately malformed text.
  void write(const std::string& fileName, const std::string& content) const {
    std::ofstream out(directory_ / fileName);
    out << content;
  }

  void remove(const std::string& fileName) const { std::filesystem::remove(directory_ / fileName); }

 private:
  void writeValidFiles() {
    write("case.json", R"({
      "name": "Test Case",
      "geometry": "geometry.json",
      "mesh": "mesh.json",
      "physics": "physics.json",
      "boundaries": "boundaries.json",
      "solver": "solver.json"
    })");
    write("geometry.json", R"({"type": "rectangle", "length": 1.0, "height": 1.0})");
    write("mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 4})");
    write("physics.json",
          R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01})");
    write("boundaries.json", R"({
      "patches": {
        "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
        "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
        "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
        "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
                   "pressure": {"type": "fixed_gradient", "value": 0.0}}
      }
    })");
    write("solver.json", R"({
      "type": "SIMPLE",
      "max_iterations": 1000,
      "velocity_relaxation": 0.7,
      "pressure_relaxation": 0.3,
      "velocity_tolerance": 1e-6,
      "pressure_tolerance": 1e-6,
      "continuity_tolerance": 1e-6,
      "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                  "relative_tolerance": 1e-8, "max_iterations": 500},
      "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                                  "relative_tolerance": 1e-8, "max_iterations": 2000}
    })");
  }

  std::filesystem::path directory_;
};

}  // namespace cfd::testutil
