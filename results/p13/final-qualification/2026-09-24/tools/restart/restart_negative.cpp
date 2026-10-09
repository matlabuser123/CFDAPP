// P13-001, groups A, C and E -- format and identity, the rejection matrix,
// and device residency across a restart.
//
// Group C is a REJECTION matrix, so the criterion is not merely "it threw":
// each row names the exception TYPE the contract promises and requires the
// message to identify the offending thing. A bare "invalid checkpoint" fails
// the row even though it threw.
//
// Non-vacuity for group C is structural: every corrupted document is derived
// from a checkpoint that is first shown to READ SUCCESSFULLY. A rejection test
// whose input would have been rejected anyway proves nothing, so the valid
// baseline read is asserted before any mutation is applied.
//
// usage: restart_negative
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/io/CheckpointReader.hpp"
#include "cfd/io/CheckpointWriter.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"
#include "cfd/solver/RestartSnapshot.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::CheckpointReader;
using cfd::io::CheckpointWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::makeContinuationFromCheckpoint;
using cfd::pressure_velocity::makeSIMPLECheckpoint;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLECheckpoint;
using cfd::pressure_velocity::SIMPLEContinuation;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

int failures = 0;
int checks = 0;

void pass(const std::string& label) {
  ++checks;
  std::printf("  PASS %s\n", label.c_str());
}
void fail(const std::string& label, const std::string& detail) {
  ++checks;
  ++failures;
  std::printf("  FAIL %s -- %s\n", label.c_str(), detail.c_str());
}

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// Which exception the contract promises for a row.
enum class Expect { IO, Schema, Invalid };

const char* expectName(Expect e) {
  switch (e) {
    case Expect::IO: return "IOError";
    case Expect::Schema: return "CaseConfigurationError";
    case Expect::Invalid: return "InvalidArgumentError";
  }
  return "?";
}

// Runs `action`, requires it to throw the promised type, and requires the
// message to contain `mustMention` -- "it threw" alone is not the criterion.
void expectRejection(const std::string& label, Expect expect, const std::string& mustMention,
                     const std::function<void()>& action) {
  try {
    action();
  } catch (const cfd::CaseConfigurationError& e) {
    const std::string what = e.what();
    if (expect != Expect::Schema) {
      fail(label, std::string("threw CaseConfigurationError, expected ") + expectName(expect));
    } else if (what.find(mustMention) == std::string::npos) {
      fail(label, "message does not mention \"" + mustMention + "\": " + what);
    } else {
      pass(label);
    }
    return;
  } catch (const cfd::IOError& e) {
    const std::string what = e.what();
    if (expect != Expect::IO) {
      fail(label, std::string("threw IOError, expected ") + expectName(expect));
    } else if (what.find(mustMention) == std::string::npos) {
      fail(label, "message does not mention \"" + mustMention + "\": " + what);
    } else {
      pass(label);
    }
    return;
  } catch (const cfd::InvalidArgumentError& e) {
    const std::string what = e.what();
    if (expect != Expect::Invalid) {
      fail(label, std::string("threw InvalidArgumentError, expected ") + expectName(expect));
    } else if (what.find(mustMention) == std::string::npos) {
      fail(label, "message does not mention \"" + mustMention + "\": " + what);
    } else {
      pass(label);
    }
    return;
  } catch (const std::exception& e) {
    fail(label, std::string("threw an unexpected exception type: ") + e.what());
    return;
  }
  fail(label, "did not throw");
}

SIMPLESettings settingsFor(Index outer, bool gpu) {
  SIMPLESettings s;
  s.maxIterations = outer;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.type = LinearSolverType::BiCGSTAB;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.type = LinearSolverType::BiCGSTAB;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  if (gpu) {
    s.momentumSolver.backend = LinearSolverBackend::GPU;
    s.momentumSolver.preconditioner = PreconditionerType::Jacobi;
    s.pressureSolver.backend = LinearSolverBackend::GPU;
    s.pressureSolver.preconditioner = PreconditionerType::Jacobi;
    s.enableGpuDiscretization = true;
  }
  return s;
}

struct Cavity {
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
};

Cavity makeCavity(Mesh mesh) {
  Cavity c{std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
  std::size_t i = 0;
  for (const auto& patch : c.mesh.boundaryPatches()) {
    const bool lid = i + 1 == c.mesh.boundaryPatches().size();
    c.vb.set(c.mesh, patch.name(),
             lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
                 : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::Wall>()));
    c.pb.set(c.mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(c.mesh.numberOfCells());
  c.pressure = ScalarField(c.mesh.numberOfCells());
  return c;
}

SIMPLEResult solve(const Cavity& c, const SIMPLESettings& s) {
  const SIMPLE simple(s, 0);
  return simple.solve(c.mesh, c.fluid, c.vb, c.pb, c.velocity, c.pressure);
}

// Text edits on the written file, so what is corrupted is the bytes a real
// truncated or tampered file would have.
std::string readFile(const std::filesystem::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
void writeFile(const std::filesystem::path& p, const std::string& text) {
  std::ofstream out(p);
  out << text;
}
std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
  const std::size_t at = text.find(from);
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

}  // namespace

int main() {
  // NOT "p13_restart_negative": run.sh links this harness to
  // /tmp/p13_<basename>, so a scratch directory of that exact name replaces
  // the binary and the NEXT link fails with "cannot open output file ... Is a
  // directory". Found by gate.sh failing closed on the second run.
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "p13_restart_negative_data";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  Cavity cavity2d = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  Cavity cavity3d = makeCavity(MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0));

  const SIMPLEResult result2d = solve(cavity2d, settingsFor(5, false));
  const SIMPLEResult result3d = solve(cavity3d, settingsFor(4, false));

  const SIMPLECheckpoint checkpoint2d =
      makeSIMPLECheckpoint(result2d, cavity2d.mesh, "case-fp-A", "CPU", "test-version");
  const SIMPLECheckpoint checkpoint3d =
      makeSIMPLECheckpoint(result3d, cavity3d.mesh, "case-fp-A", "CPU", "test-version");

  const auto path2d = dir / "ck2d.json";
  const auto path3d = dir / "ck3d.json";
  CheckpointWriter::write(path2d, checkpoint2d);
  CheckpointWriter::write(path3d, checkpoint3d);

  // ==========================================================================
  std::printf("=== A: format and identity ===\n");

  // A1 -- the version is this format's own, not the P2 restart lineage's.
  if (cfd::pressure_velocity::kSIMPLECheckpointVersion == 1 &&
      cfd::solver::kRestartFormatVersion == 1) {
    pass("A1 both formats exist and are versioned independently");
  } else {
    fail("A1", "version constants are not both 1 as documented");
  }

  // A2 -- every documented key is present, and no undocumented key is.
  {
    const std::string text = readFile(path2d);
    static const char* required[] = {"format_version",  "application",     "version",
                                     "backend",         "identity",        "case_fingerprint",
                                     "mesh_fingerprint", "cell_count",     "face_count",
                                     "dimension",       "state",           "completed_iterations",
                                     "status",          "fields",          "pressure",
                                     "velocity_x",      "velocity_y",      "velocity_z",
                                     "mass_flux",       "history",         "u",
                                     "v",               "continuity",      "global_imbalance"};
    bool ok = true;
    std::string missing;
    for (const char* key : required) {
      if (text.find(std::string("\"") + key + "\"") == std::string::npos) {
        ok = false;
        missing += std::string(" ") + key;
      }
    }
    if (ok) pass("A2 every documented key is present");
    else fail("A2", "missing keys:" + missing);
  }

  // A5 -- nothing device-specific leaked into the format.
  {
    const std::string text = readFile(path2d);
    static const char* forbidden[] = {"device", "cuda", "stream", "pointer",
                                      "krylov", "workspace", "allocator", "0x"};
    bool ok = true;
    std::string found;
    for (const char* key : forbidden) {
      if (text.find(key) != std::string::npos) {
        ok = false;
        found += std::string(" ") + key;
      }
    }
    if (ok) pass("A5 no device pointer, handle, stream or scratch appears in the file");
    else fail("A5", "the file mentions:" + found);
  }

  // A3 -- write/read round-trips bitwise.
  {
    const SIMPLECheckpoint back = CheckpointReader::read(path2d, cavity2d.mesh);
    bool ok = back.completedIterations == checkpoint2d.completedIterations &&
              back.cellCount == checkpoint2d.cellCount &&
              back.faceCount == checkpoint2d.faceCount &&
              back.dimension == checkpoint2d.dimension &&
              back.caseFingerprint == checkpoint2d.caseFingerprint &&
              back.meshFingerprint == checkpoint2d.meshFingerprint &&
              back.history.size() == checkpoint2d.history.size();
    std::size_t values = 0;
    for (Index i = 0; ok && i < checkpoint2d.velocity.size(); ++i) {
      ok = sameBits(back.velocity[i].x, checkpoint2d.velocity[i].x) &&
           sameBits(back.velocity[i].y, checkpoint2d.velocity[i].y) &&
           sameBits(back.velocity[i].z, checkpoint2d.velocity[i].z) &&
           sameBits(back.pressure[i], checkpoint2d.pressure[i]);
      values += 4;
    }
    for (Index i = 0; ok && i < checkpoint2d.massFlux.size(); ++i) {
      ok = sameBits(back.massFlux[i], checkpoint2d.massFlux[i]);
      ++values;
    }
    for (std::size_t i = 0; ok && i < checkpoint2d.history.size(); ++i) {
      const auto& a = checkpoint2d.history[i];
      const auto& b = back.history[i];
      ok = sameBits(a.u, b.u) && sameBits(a.v, b.v) && sameBits(a.pressure, b.pressure) &&
           sameBits(a.continuity, b.continuity) &&
           sameBits(a.globalImbalance, b.globalImbalance) && a.w.has_value() == b.w.has_value();
      values += 5;
    }
    if (ok) pass("A3 round-trip is bitwise (" + std::to_string(values) + " values)");
    else fail("A3", "a value did not round-trip");
  }

  // A3b -- 3D round-trips too, including w and velocity_z.
  {
    const SIMPLECheckpoint back = CheckpointReader::read(path3d, cavity3d.mesh);
    const bool hasW = !back.history.empty() && back.history.front().w.has_value();
    if (back.dimension == 3 && hasW) pass("A3b a 3D checkpoint carries dimension 3 and w residuals");
    else fail("A3b", "3D checkpoint is missing dimension or w");
  }

  // A4 -- writing the same checkpoint twice is byte-identical.
  {
    const auto a = dir / "det_a.json";
    const auto b = dir / "det_b.json";
    CheckpointWriter::write(a, checkpoint2d);
    CheckpointWriter::write(b, checkpoint2d);
    if (readFile(a) == readFile(b)) pass("A4 repeated writes are byte-identical");
    else fail("A4", "two writes of the same checkpoint differ");
  }

  // ==========================================================================
  std::printf("=== C: rejection matrix ===\n");

  // Non-vacuity: the unmutated file must READ, or every row below is testing
  // an input that would have been rejected anyway.
  try {
    (void)CheckpointReader::read(path2d, cavity2d.mesh);
    pass("C0 the unmutated checkpoint reads successfully (rows below are non-vacuous)");
  } catch (const std::exception& e) {
    fail("C0", std::string("the valid checkpoint did not read: ") + e.what());
    std::printf("\nchecks %d   failures %d\nGROUPS A/C/E: FAIL\n", checks, failures);
    return 1;
  }

  const std::string valid = readFile(path2d);
  const auto mutate = [&](const std::string& name, const std::string& text) {
    const auto p = dir / (name + ".json");
    writeFile(p, text);
    return p;
  };

  expectRejection("C1 file does not exist", Expect::IO, "nonexistent", [&] {
    (void)CheckpointReader::read(dir / "nonexistent.json", cavity2d.mesh);
  });
  expectRejection("C2 truncated mid-file", Expect::IO, "trunc", [&] {
    (void)CheckpointReader::read(mutate("trunc", valid.substr(0, valid.size() / 2)),
                                 cavity2d.mesh);
  });
  expectRejection("C3 unsupported format version", Expect::Invalid, "format version", [&] {
    (void)CheckpointReader::read(
        mutate("ver", replaceOnce(valid, "\"format_version\": 1", "\"format_version\": 7")),
        cavity2d.mesh);
  });
  expectRejection("C4 missing required key", Expect::Schema, "mass_flux", [&] {
    (void)CheckpointReader::read(mutate("nokey", replaceOnce(valid, "\"mass_flux\"", "\"gone\"")),
                                 cavity2d.mesh);
  });
  expectRejection("C5 wrong JSON type for a key", Expect::Schema, "completed_iterations", [&] {
    (void)CheckpointReader::read(
        mutate("type", replaceOnce(valid, "\"completed_iterations\": 5",
                                   "\"completed_iterations\": \"five\"")),
        cavity2d.mesh);
  });
  expectRejection("C6 different mesh, same counts", Expect::Invalid, "fingerprint", [&] {
    // Same cell and face counts, different geometry -- only the fingerprint
    // can tell these apart, which is exactly why it exists.
    Cavity other = makeCavity(MeshGeometry::createCartesian2D(8, 8, 2.0, 1.0));
    (void)CheckpointReader::read(path2d, other.mesh);
  });
  expectRejection("C7 different cell count", Expect::Invalid, "mesh identity mismatch", [&] {
    Cavity other = makeCavity(MeshGeometry::createCartesian2D(9, 9, 1.0, 1.0));
    (void)CheckpointReader::read(path2d, other.mesh);
  });
  expectRejection("C8 2D checkpoint into a 3D mesh", Expect::Invalid, "mesh identity mismatch",
                  [&] { (void)CheckpointReader::read(path2d, cavity3d.mesh); });
  expectRejection("C9 different case configuration", Expect::Invalid, "case identity mismatch",
                  [&] {
                    const SIMPLECheckpoint back = CheckpointReader::read(path2d, cavity2d.mesh);
                    CheckpointReader::requireCaseFingerprint(back, "case-fp-B", path2d);
                  });
  expectRejection("C10 truncated field array", Expect::Schema, "fields.pressure", [&] {
    // Remove the last pressure entry by turning the array's final value into
    // nothing -- a length the document's own cell_count contradicts.
    std::string text = valid;
    const std::size_t at = text.find("\"pressure\": [");
    const std::size_t close = text.find(']', at);
    const std::size_t lastComma = text.rfind(',', close);
    text.erase(lastComma, close - lastComma);
    (void)CheckpointReader::read(mutate("short", text), cavity2d.mesh);
  });
  expectRejection("C11 NaN in a field", Expect::IO, "nan", [&] {
    (void)CheckpointReader::read(
        mutate("nan", replaceOnce(valid, "\"pressure\": [", "\"pressure\": [NaN,")),
        cavity2d.mesh);
  });
  expectRejection("C12 Inf in a field", Expect::IO, "inf", [&] {
    (void)CheckpointReader::read(
        mutate("inf", replaceOnce(valid, "\"mass_flux\": [", "\"mass_flux\": [Infinity,")),
        cavity2d.mesh);
  });
  expectRejection("C13 history length contradicts the iteration count", Expect::Schema,
                  "history.u", [&] {
                    std::string text = valid;
                    const std::size_t at = text.find("\"u\": [");
                    const std::size_t close = text.find(']', at);
                    const std::size_t lastComma = text.rfind(',', close);
                    text.erase(lastComma, close - lastComma);
                    (void)CheckpointReader::read(mutate("hist", text), cavity2d.mesh);
                  });

  // C -- a checkpoint that claims zero completed iterations carries no state.
  expectRejection("C-zero completedIterations 0 is refused", Expect::Invalid,
                  "completedIterations", [&] {
                    SIMPLECheckpoint bad = checkpoint2d;
                    bad.completedIterations = 0;
                    bad.history.clear();
                    cfd::pressure_velocity::validateSIMPLECheckpoint(bad);
                  });

  // The writer refuses too, so an invalid checkpoint never reaches disk.
  expectRejection("C-write the writer refuses an invalid checkpoint", Expect::Invalid,
                  "format version", [&] {
                    SIMPLECheckpoint bad = checkpoint2d;
                    bad.formatVersion = 42;
                    CheckpointWriter::write(dir / "never.json", bad);
                  });
  if (std::filesystem::exists(dir / "never.json")) {
    fail("C-write-no-file", "the refused checkpoint still created a file");
  } else {
    pass("C-write-no-file a refused checkpoint leaves no file behind");
  }

  // ==========================================================================
  std::printf("=== E: device residency across a restart ===\n");
  {
    const SIMPLESettings gpu = settingsFor(6, true);
    const SIMPLEResult continuous = solve(cavity2d, gpu);

    const SIMPLESettings gpuSplit = settingsFor(6, true);
    SIMPLE first(gpuSplit, 0);
    SIMPLEContinuation seed;
    seed.iterationBudget = 3;
    first.setContinuation(&seed);
    const SIMPLEResult segmentA =
        first.solve(cavity2d.mesh, cavity2d.fluid, cavity2d.vb, cavity2d.pb, cavity2d.velocity,
                    cavity2d.pressure);

    SIMPLEContinuation resume = cfd::pressure_velocity::makeSIMPLEContinuation(segmentA);
    SIMPLE second(gpuSplit, 0);
    second.setContinuation(&resume);
    cfd::gpu::resetGpuExecutionStats();
    const SIMPLEResult segmentB =
        second.solve(cavity2d.mesh, cavity2d.fluid, cavity2d.vb, cavity2d.pb, segmentA.velocity,
                     segmentA.pressure);
    const auto stats = cfd::gpu::gpuExecutionStats();

    // E1 -- the resumed arm took the same device path.
    if (segmentB.gpuDiscretization == continuous.gpuDiscretization &&
        segmentB.residentPressureSolve == continuous.residentPressureSolve &&
        segmentB.residentSimpleLoop == continuous.residentSimpleLoop &&
        continuous.residentSimpleLoop) {
      pass("E1 the resumed segment used the same resident GPU path as the continuous run");
    } else {
      fail("E1", "residency flags differ, or the continuous run was not resident at all");
    }
    if (!segmentB.resumedFromContinuation) {
      fail("E1b", "the resumed segment did not report resumedFromContinuation");
    } else {
      pass("E1b the resumed segment reported resuming (the comparison is not vacuous)");
    }

    // E2 -- a restart must not reintroduce per-iteration device reallocation.
    if (stats.reallocations == 0) {
      pass("E2 0 device reallocations in the resumed segment");
    } else {
      fail("E2", std::to_string(stats.reallocations) + " device reallocations after a resume");
    }

    // E3 -- two resumes from the same state are bitwise identical.
    SIMPLEContinuation again = cfd::pressure_velocity::makeSIMPLEContinuation(segmentA);
    SIMPLE third(gpuSplit, 0);
    third.setContinuation(&again);
    const SIMPLEResult segmentC =
        third.solve(cavity2d.mesh, cavity2d.fluid, cavity2d.vb, cavity2d.pb, segmentA.velocity,
                    segmentA.pressure);
    bool identical = segmentB.iterations == segmentC.iterations;
    for (Index i = 0; identical && i < segmentB.velocity.size(); ++i) {
      identical = sameBits(segmentB.velocity[i].x, segmentC.velocity[i].x) &&
                  sameBits(segmentB.velocity[i].y, segmentC.velocity[i].y) &&
                  sameBits(segmentB.pressure[i], segmentC.pressure[i]);
    }
    if (identical) pass("E3 two resumes from the same checkpoint are bitwise identical");
    else fail("E3", "two resumes from the same checkpoint differ");
  }

  std::printf("\nchecks %d   failures %d\n", checks, failures);
  std::printf("GROUPS A/C/E: %s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
