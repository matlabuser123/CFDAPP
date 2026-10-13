// P13-001 -- Restart / checkpointing: the production checkpoint of a steady
// SIMPLE solve.
//
// CPU only, deliberately: CI has no GPU, and these are the tests that must run
// on every push. The GPU rows of this item's gate live in
// results/p13/restart/ and are run locally -- see that directory's summary.md
// for what they cover and why they cannot run in CI.
//
// The decisive test here is the same one the gate's group B states:
//
//     one solve of N iterations
//   ==
//     a solve of N1, written to a checkpoint, read back, resumed to N
//
// bitwise. It is the regression that would catch a future change quietly
// reverting to regenerating the face mass flux from the cell velocity.
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/CheckpointReader.hpp"
#include "cfd/io/CheckpointWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"
#include "cfd/solver/RestartSnapshot.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::io::CheckpointReader;
using cfd::io::CheckpointWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::makeContinuationFromCheckpoint;
using cfd::pressure_velocity::makeSIMPLECheckpoint;
using cfd::pressure_velocity::makeSIMPLEContinuation;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLECheckpoint;
using cfd::pressure_velocity::SIMPLEContinuation;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

std::filesystem::path tempPath(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_simple_checkpoint";
  std::filesystem::create_directories(dir);
  return dir / name;
}

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

SIMPLESettings cavitySettings(Index outer) {
  SIMPLESettings settings;
  settings.maxIterations = outer;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  return settings;
}

struct Cavity {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  cfd::physics::FluidProperties fluid{1.0, 0.01};
};

Cavity makeCavity(Mesh mesh) {
  Cavity cavity{std::move(mesh), {}, {}, {}, {}, cfd::physics::FluidProperties{1.0, 0.01}};
  std::size_t index = 0;
  for (const auto& patch : cavity.mesh.boundaryPatches()) {
    const bool lid = index + 1 == cavity.mesh.boundaryPatches().size();
    cavity.velocityBoundaries.set(
        cavity.mesh, patch.name(),
        lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
            : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                  std::make_unique<cfd::boundary::Wall>()));
    cavity.pressureBoundaries.set(cavity.mesh, patch.name(),
                                  std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++index;
  }
  cavity.velocity = VectorField(cavity.mesh.numberOfCells());
  cavity.pressure = ScalarField(cavity.mesh.numberOfCells());
  return cavity;
}

SIMPLEResult solveOnce(const Cavity& cavity, const SIMPLESettings& settings) {
  const SIMPLE simple(settings, /*referenceCell=*/0);
  return simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries,
                      cavity.pressureBoundaries, cavity.velocity, cavity.pressure);
}

void expectSameSolution(const SIMPLEResult& expected, const SIMPLEResult& actual) {
  EXPECT_EQ(expected.status, actual.status);
  EXPECT_EQ(expected.iterations, actual.iterations);
  ASSERT_EQ(expected.velocity.size(), actual.velocity.size());
  ASSERT_EQ(expected.massFlux.size(), actual.massFlux.size());
  for (Index i = 0; i < expected.velocity.size(); ++i) {
    EXPECT_TRUE(sameBits(expected.velocity[i].x, actual.velocity[i].x)) << "velocity.x " << i;
    EXPECT_TRUE(sameBits(expected.velocity[i].y, actual.velocity[i].y)) << "velocity.y " << i;
    EXPECT_TRUE(sameBits(expected.velocity[i].z, actual.velocity[i].z)) << "velocity.z " << i;
    EXPECT_TRUE(sameBits(expected.pressure[i], actual.pressure[i])) << "pressure " << i;
  }
  for (Index i = 0; i < expected.massFlux.size(); ++i) {
    EXPECT_TRUE(sameBits(expected.massFlux[i], actual.massFlux[i])) << "massFlux " << i;
  }
  ASSERT_EQ(expected.uResidualHistory.size(), actual.uResidualHistory.size());
  for (std::size_t i = 0; i < expected.uResidualHistory.size(); ++i) {
    EXPECT_TRUE(sameBits(expected.uResidualHistory[i], actual.uResidualHistory[i])) << "u " << i;
    EXPECT_TRUE(sameBits(expected.vResidualHistory[i], actual.vResidualHistory[i])) << "v " << i;
    EXPECT_TRUE(sameBits(expected.pressureResidualHistory[i], actual.pressureResidualHistory[i]))
        << "p " << i;
    EXPECT_TRUE(sameBits(expected.continuityHistory[i], actual.continuityHistory[i]))
        << "continuity " << i;
    EXPECT_TRUE(
        sameBits(expected.globalMassImbalanceHistory[i], actual.globalMassImbalanceHistory[i]))
        << "imbalance " << i;
  }
}

// Runs `total` iterations as two segments split at `split`, carrying the state
// through a checkpoint FILE -- the full write/read round trip, not just the
// in-memory continuation.
SIMPLEResult solveThroughCheckpoint(const Cavity& cavity, Index total, Index split,
                                    const std::string& fileName) {
  const SIMPLESettings settings = cavitySettings(total);

  SIMPLEContinuation firstSegment;
  firstSegment.iterationBudget = split;
  SIMPLE first(settings, /*referenceCell=*/0);
  first.setContinuation(&firstSegment);
  const SIMPLEResult partial =
      first.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  cavity.velocity, cavity.pressure);
  EXPECT_EQ(partial.iterations, split);
  EXPECT_FALSE(partial.resumedFromContinuation);

  const auto path = tempPath(fileName);
  CheckpointWriter::write(
      path, makeSIMPLECheckpoint(partial, cavity.mesh, "case-fingerprint", "CPU", "test"));

  const SIMPLECheckpoint loaded = CheckpointReader::read(path, cavity.mesh);
  SIMPLEContinuation secondSegment = makeContinuationFromCheckpoint(loaded);
  SIMPLE second(settings, /*referenceCell=*/0);
  second.setContinuation(&secondSegment);
  const SIMPLEResult resumed =
      second.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   loaded.velocity, loaded.pressure);
  // Non-vacuity: if the second segment did not actually resume, the comparison
  // in the caller is between two fresh solves and proves nothing.
  EXPECT_TRUE(resumed.resumedFromContinuation);
  return resumed;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

// --- the property the whole item exists for --------------------------------

TEST(SIMPLECheckpointTest, ContinuousAndResumedSolvesAgreeBitwise) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0));
  const SIMPLEResult continuous = solveOnce(cavity, cavitySettings(10));
  EXPECT_FALSE(continuous.resumedFromContinuation);
  const SIMPLEResult resumed = solveThroughCheckpoint(cavity, 10, 4, "split_mid.json");
  expectSameSolution(continuous, resumed);
}

TEST(SIMPLECheckpointTest, SplitAtTheFirstAndLastIterationAlsoAgreeBitwise) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
  const SIMPLEResult continuous = solveOnce(cavity, cavitySettings(8));
  expectSameSolution(continuous, solveThroughCheckpoint(cavity, 8, 1, "split_first.json"));
  expectSameSolution(continuous, solveThroughCheckpoint(cavity, 8, 7, "split_last.json"));
}

TEST(SIMPLECheckpointTest, ThreeDimensionalSolveResumesBitwise) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0));
  const SIMPLEResult continuous = solveOnce(cavity, cavitySettings(5));
  ASSERT_FALSE(continuous.wResidualHistory.empty());
  const SIMPLEResult resumed = solveThroughCheckpoint(cavity, 5, 2, "split_3d.json");
  expectSameSolution(continuous, resumed);
  ASSERT_EQ(continuous.wResidualHistory.size(), resumed.wResidualHistory.size());
  for (std::size_t i = 0; i < continuous.wResidualHistory.size(); ++i) {
    EXPECT_TRUE(sameBits(continuous.wResidualHistory[i], resumed.wResidualHistory[i]));
  }
}

// The monitor state a resume has to reconstruct only matters when a robustness
// feature reads it, so this is the configuration that would expose a missing
// or wrong replay.
TEST(SIMPLECheckpointTest, NormalizedCriterionAndAdaptiveRelaxationSurviveARestart) {
  Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
  SIMPLESettings settings = cavitySettings(24);
  settings.robustness.convergenceCriterion = cfd::solver::ConvergenceCriterion::Normalized;
  settings.robustness.adaptiveRelaxation.enabled = true;
  settings.robustness.divergence.enabled = true;
  settings.robustness.divergence.window = 3;
  settings.robustness.divergence.startIteration = 4;

  const SIMPLE simple(settings, /*referenceCell=*/0);
  const SIMPLEResult continuous =
      simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   cavity.velocity, cavity.pressure);

  SIMPLEContinuation firstSegment;
  firstSegment.iterationBudget = 9;
  SIMPLE first(settings, /*referenceCell=*/0);
  first.setContinuation(&firstSegment);
  const SIMPLEResult partial =
      first.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  cavity.velocity, cavity.pressure);
  SIMPLEContinuation secondSegment = makeSIMPLEContinuation(partial);
  SIMPLE second(settings, /*referenceCell=*/0);
  second.setContinuation(&secondSegment);
  const SIMPLEResult resumed =
      second.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   partial.velocity, partial.pressure);

  expectSameSolution(continuous, resumed);
  ASSERT_EQ(continuous.robustness.velocityRelaxationHistory.size(),
            resumed.robustness.velocityRelaxationHistory.size());
  for (std::size_t i = 0; i < continuous.robustness.velocityRelaxationHistory.size(); ++i) {
    EXPECT_TRUE(sameBits(continuous.robustness.velocityRelaxationHistory[i],
                         resumed.robustness.velocityRelaxationHistory[i]))
        << "relaxation " << i;
    EXPECT_TRUE(sameBits(continuous.robustness.convergenceDistanceHistory[i],
                         resumed.robustness.convergenceDistanceHistory[i]))
        << "distance " << i;
  }
}

// --- the regression that catches a revert to regenerating F ----------------

TEST(SIMPLECheckpointTest, TheCheckpointedFaceFluxIsUsedRatherThanRegenerated) {
  // A resumed solve that regenerated the face mass flux from the cell velocity
  // -- the pre-P13 behaviour -- would produce a DIFFERENT first iteration. The
  // control here is the continuation with its flux replaced by exactly that
  // regeneration, which must NOT reproduce the continuous run.
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
  const SIMPLESettings settings = cavitySettings(8);
  const SIMPLEResult continuous = solveOnce(cavity, settings);

  SIMPLEContinuation firstSegment;
  firstSegment.iterationBudget = 4;
  SIMPLE first(settings, /*referenceCell=*/0);
  first.setContinuation(&firstSegment);
  const SIMPLEResult partial =
      first.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  cavity.velocity, cavity.pressure);

  SIMPLEContinuation broken = makeSIMPLEContinuation(partial);
  broken.massFlux = cfd::physics::calculateMassFlux(cavity.mesh, partial.velocity, cavity.fluid,
                                                    cavity.velocityBoundaries);
  SIMPLE second(settings, /*referenceCell=*/0);
  second.setContinuation(&broken);
  const SIMPLEResult wrong =
      second.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   partial.velocity, partial.pressure);

  ASSERT_EQ(continuous.velocity.size(), wrong.velocity.size());
  bool anyDifference = false;
  for (Index i = 0; i < continuous.velocity.size() && !anyDifference; ++i) {
    anyDifference = !sameBits(continuous.velocity[i].x, wrong.velocity[i].x) ||
                    !sameBits(continuous.pressure[i], wrong.pressure[i]);
  }
  EXPECT_TRUE(anyDifference)
      << "regenerating the face mass flux made no difference, so the bitwise equivalence tests "
         "above would pass even if the checkpointed flux were ignored";
}

// --- format ----------------------------------------------------------------

TEST(SIMPLECheckpointTest, FormatVersionIsIndependentOfTheTransientRestartFormat) {
  // Two formats, two version numbers. The P2 transient restart is unchanged by
  // this work and must stay free to version itself.
  EXPECT_EQ(cfd::pressure_velocity::kSIMPLECheckpointVersion, 1u);
  EXPECT_EQ(cfd::solver::kRestartFormatVersion, 1u);
}

TEST(SIMPLECheckpointTest, RoundTripIsBitwise) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLEResult result = solveOnce(cavity, cavitySettings(4));
  const SIMPLECheckpoint original =
      makeSIMPLECheckpoint(result, cavity.mesh, "fp-case", "CPU", "test-version");
  const auto path = tempPath("roundtrip.json");
  CheckpointWriter::write(path, original);
  const SIMPLECheckpoint loaded = CheckpointReader::read(path, cavity.mesh);

  EXPECT_EQ(original.completedIterations, loaded.completedIterations);
  EXPECT_EQ(original.caseFingerprint, loaded.caseFingerprint);
  EXPECT_EQ(original.meshFingerprint, loaded.meshFingerprint);
  EXPECT_EQ(original.dimension, loaded.dimension);
  EXPECT_EQ(original.backend, loaded.backend);
  EXPECT_EQ(original.applicationVersion, loaded.applicationVersion);
  ASSERT_EQ(original.history.size(), loaded.history.size());
  for (Index i = 0; i < original.velocity.size(); ++i) {
    EXPECT_TRUE(sameBits(original.velocity[i].x, loaded.velocity[i].x));
    EXPECT_TRUE(sameBits(original.velocity[i].y, loaded.velocity[i].y));
    EXPECT_TRUE(sameBits(original.velocity[i].z, loaded.velocity[i].z));
    EXPECT_TRUE(sameBits(original.pressure[i], loaded.pressure[i]));
  }
  for (Index i = 0; i < original.massFlux.size(); ++i) {
    EXPECT_TRUE(sameBits(original.massFlux[i], loaded.massFlux[i]));
  }
  for (std::size_t i = 0; i < original.history.size(); ++i) {
    EXPECT_TRUE(sameBits(original.history[i].u, loaded.history[i].u));
    EXPECT_TRUE(sameBits(original.history[i].globalImbalance, loaded.history[i].globalImbalance));
  }
}

TEST(SIMPLECheckpointTest, RepeatedWritesAreByteIdentical) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLECheckpoint checkpoint = makeSIMPLECheckpoint(
      solveOnce(cavity, cavitySettings(3)), cavity.mesh, "fp-case", "CPU", "test-version");
  const auto a = tempPath("det_a.json");
  const auto b = tempPath("det_b.json");
  CheckpointWriter::write(a, checkpoint);
  CheckpointWriter::write(b, checkpoint);
  EXPECT_EQ(readFile(a), readFile(b));
}

TEST(SIMPLECheckpointTest, NoDeviceStateAppearsInTheFile) {
  // The format serializes a model that contains no device state, so this is a
  // statement about the model as much as about the writer -- but a future edit
  // that started storing a handle would be caught here.
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const auto path = tempPath("nodevice.json");
  CheckpointWriter::write(path, makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)),
                                                     cavity.mesh, "fp", "CPU", "v"));
  const std::string text = readFile(path);
  for (const char* forbidden : {"device", "cuda", "stream", "pointer", "krylov", "0x"}) {
    EXPECT_EQ(text.find(forbidden), std::string::npos) << "file mentions " << forbidden;
  }
}

// --- rejection --------------------------------------------------------------

TEST(SIMPLECheckpointTest, RejectsAMissingOrMalformedFile) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  EXPECT_THROW((void)CheckpointReader::read(tempPath("does_not_exist.json"), cavity.mesh),
               cfd::IOError);

  const auto valid = tempPath("valid.json");
  CheckpointWriter::write(valid, makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)),
                                                      cavity.mesh, "fp", "CPU", "v"));
  // Non-vacuity: the unmutated file must read, or the mutations below prove
  // nothing about what the mutation did.
  EXPECT_NO_THROW((void)CheckpointReader::read(valid, cavity.mesh));

  const std::string text = readFile(valid);
  const auto truncated = tempPath("truncated.json");
  { std::ofstream(truncated) << text.substr(0, text.size() / 2); }
  EXPECT_THROW((void)CheckpointReader::read(truncated, cavity.mesh), cfd::IOError);
}

TEST(SIMPLECheckpointTest, RejectsAnUnsupportedFormatVersion) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  SIMPLECheckpoint checkpoint =
      makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)), cavity.mesh, "fp", "CPU", "v");
  checkpoint.formatVersion = 99;
  EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(checkpoint),
               cfd::InvalidArgumentError);
  // And the writer refuses it, so it never reaches disk to be read later.
  EXPECT_THROW(CheckpointWriter::write(tempPath("never_written.json"), checkpoint),
               cfd::InvalidArgumentError);
}

TEST(SIMPLECheckpointTest, RejectsADifferentMeshWithTheSameCellAndFaceCount) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const auto path = tempPath("meshid.json");
  CheckpointWriter::write(path, makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)),
                                                     cavity.mesh, "fp", "CPU", "v"));

  // Same counts, different geometry: only the fingerprint separates these.
  const Cavity other = makeCavity(MeshGeometry::createCartesian2D(8, 8, 2.0, 1.0));
  ASSERT_EQ(cavity.mesh.numberOfCells(), other.mesh.numberOfCells());
  ASSERT_EQ(cavity.mesh.numberOfFaces(), other.mesh.numberOfFaces());
  EXPECT_THROW((void)CheckpointReader::read(path, other.mesh), cfd::InvalidArgumentError);

  const Cavity bigger = makeCavity(MeshGeometry::createCartesian2D(9, 9, 1.0, 1.0));
  EXPECT_THROW((void)CheckpointReader::read(path, bigger.mesh), cfd::InvalidArgumentError);

  const Cavity threeD = makeCavity(MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0));
  EXPECT_THROW((void)CheckpointReader::read(path, threeD.mesh), cfd::InvalidArgumentError);
}

TEST(SIMPLECheckpointTest, RejectsADifferentCaseFingerprint) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLECheckpoint checkpoint =
      makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)), cavity.mesh, "fp-A", "CPU", "v");
  EXPECT_NO_THROW(CheckpointReader::requireCaseFingerprint(checkpoint, "fp-A", tempPath("x.json")));
  EXPECT_THROW(CheckpointReader::requireCaseFingerprint(checkpoint, "fp-B", tempPath("x.json")),
               cfd::InvalidArgumentError);
}

TEST(SIMPLECheckpointTest, RejectsInconsistentOrNonFiniteContent) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLECheckpoint good =
      makeSIMPLECheckpoint(solveOnce(cavity, cavitySettings(3)), cavity.mesh, "fp", "CPU", "v");
  EXPECT_NO_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(good, cavity.mesh));

  {  // a truncated field
    SIMPLECheckpoint bad = good;
    bad.pressure = ScalarField(bad.pressure.size() - 1);
    EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(bad), cfd::InvalidArgumentError);
  }
  {  // a history whose length contradicts the iteration count
    SIMPLECheckpoint bad = good;
    bad.history.pop_back();
    EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(bad), cfd::InvalidArgumentError);
  }
  {  // NaN and Inf
    for (Real value :
         {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
      SIMPLECheckpoint bad = good;
      bad.pressure[0] = value;
      EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(bad),
                   cfd::InvalidArgumentError);
      SIMPLECheckpoint badFlux = good;
      badFlux.massFlux[0] = value;
      EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(badFlux),
                   cfd::InvalidArgumentError);
      SIMPLECheckpoint badHistory = good;
      badHistory.history[0].globalImbalance = value;
      EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(badHistory),
                   cfd::InvalidArgumentError);
    }
  }
  {  // a checkpoint of a solve that never completed an iteration
    SIMPLECheckpoint bad = good;
    bad.completedIterations = 0;
    bad.history.clear();
    EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(bad), cfd::InvalidArgumentError);
  }
  {  // a 2D history claiming to be 3D
    SIMPLECheckpoint bad = good;
    bad.dimension = 3;
    EXPECT_THROW(cfd::pressure_velocity::validateSIMPLECheckpoint(bad), cfd::InvalidArgumentError);
  }
}

// --- the continuation contract ----------------------------------------------

TEST(SIMPLEContinuationTest, ADefaultContinuationIsAFreshSolve) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLESettings settings = cavitySettings(5);
  const SIMPLEResult plain = solveOnce(cavity, settings);

  SIMPLEContinuation fresh;  // completedIterations 0, no flux, no history.
  SIMPLE simple(settings, /*referenceCell=*/0);
  simple.setContinuation(&fresh);
  const SIMPLEResult withEmptyContinuation =
      simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   cavity.velocity, cavity.pressure);
  EXPECT_FALSE(withEmptyContinuation.resumedFromContinuation);
  expectSameSolution(plain, withEmptyContinuation);
}

TEST(SIMPLEContinuationTest, AnInconsistentContinuationIsAnInvalidConfiguration) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLESettings settings = cavitySettings(5);
  const SIMPLEResult partial = solveOnce(cavity, cavitySettings(2));

  {  // history length disagrees with completedIterations
    SIMPLEContinuation bad = makeSIMPLEContinuation(partial);
    bad.history.pop_back();
    SIMPLE simple(settings, /*referenceCell=*/0);
    simple.setContinuation(&bad);
    const SIMPLEResult result =
        simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries,
                     cavity.pressureBoundaries, cavity.velocity, cavity.pressure);
    EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
  }
  {  // a flux sized for a different mesh
    SIMPLEContinuation bad = makeSIMPLEContinuation(partial);
    bad.massFlux = SurfaceField(bad.massFlux.size() + 1, 0.0);
    SIMPLE simple(settings, /*referenceCell=*/0);
    simple.setContinuation(&bad);
    const SIMPLEResult result =
        simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries,
                     cavity.pressureBoundaries, cavity.velocity, cavity.pressure);
    EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
  }
  {  // already past the configured budget
    SIMPLEContinuation bad = makeSIMPLEContinuation(partial);
    SIMPLE simple(cavitySettings(1), /*referenceCell=*/0);
    simple.setContinuation(&bad);
    const SIMPLEResult result =
        simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries,
                     cavity.pressureBoundaries, cavity.velocity, cavity.pressure);
    EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
  }
}

TEST(SIMPLEContinuationTest, ASegmentBudgetStopsShortOfTheConfiguredBudget) {
  const Cavity cavity = makeCavity(MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
  const SIMPLESettings settings = cavitySettings(10);
  SIMPLEContinuation segment;
  segment.iterationBudget = 3;
  SIMPLE simple(settings, /*referenceCell=*/0);
  simple.setContinuation(&segment);
  const SIMPLEResult result =
      simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   cavity.velocity, cavity.pressure);
  // The documented way to tell a segment boundary from a real MaxIterations:
  // a genuine one always has iterations == maxIterations.
  EXPECT_EQ(result.status, SIMPLEStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 3u);
  EXPECT_LT(result.iterations, settings.maxIterations);
  EXPECT_EQ(result.uResidualHistory.size(), 3u);
}
