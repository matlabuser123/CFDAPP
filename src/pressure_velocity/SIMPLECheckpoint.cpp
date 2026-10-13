#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshFingerprint.hpp"

namespace cfd::pressure_velocity {

namespace {

const char* statusName(SIMPLEStatus status) noexcept {
  switch (status) {
    case SIMPLEStatus::Converged:
      return "Converged";
    case SIMPLEStatus::MaxIterations:
      return "MaxIterations";
    case SIMPLEStatus::MomentumFailure:
      return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState:
      return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration:
      return "InvalidConfiguration";
    case SIMPLEStatus::Cancelled:
      return "Cancelled";
    case SIMPLEStatus::Stagnated:
      return "Stagnated";
    case SIMPLEStatus::Diverging:
      return "Diverging";
  }
  return "Unknown";
}

}  // namespace

void validateSIMPLECheckpoint(const SIMPLECheckpoint& checkpoint) {
  if (checkpoint.formatVersion != kSIMPLECheckpointVersion) {
    throw InvalidArgumentError("validateSIMPLECheckpoint: unsupported checkpoint format version " +
                               std::to_string(checkpoint.formatVersion) + " (this build supports " +
                               std::to_string(kSIMPLECheckpointVersion) + ")");
  }
  if (checkpoint.dimension != 2 && checkpoint.dimension != 3) {
    throw InvalidArgumentError("validateSIMPLECheckpoint: dimension must be 2 or 3, got " +
                               std::to_string(checkpoint.dimension));
  }
  if (checkpoint.completedIterations == 0) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: completedIterations must be > 0 -- a checkpoint of a solve "
        "that never completed an outer iteration carries no state to resume");
  }
  if (checkpoint.velocity.size() != checkpoint.cellCount ||
      checkpoint.pressure.size() != checkpoint.cellCount) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: velocity/pressure size does not match cellCount " +
        std::to_string(checkpoint.cellCount));
  }
  if (checkpoint.massFlux.size() != checkpoint.faceCount) {
    throw InvalidArgumentError("validateSIMPLECheckpoint: massFlux size " +
                               std::to_string(checkpoint.massFlux.size()) +
                               " does not match faceCount " + std::to_string(checkpoint.faceCount));
  }
  if (checkpoint.history.size() != checkpoint.completedIterations) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: history size " + std::to_string(checkpoint.history.size()) +
        " does not match completedIterations " + std::to_string(checkpoint.completedIterations));
  }

  for (Index i = 0; i < checkpoint.velocity.size(); ++i) {
    if (!cfd::isFinite(checkpoint.velocity[i])) {
      throw InvalidArgumentError("validateSIMPLECheckpoint: non-finite velocity at cell " +
                                 std::to_string(i));
    }
  }
  for (Index i = 0; i < checkpoint.pressure.size(); ++i) {
    if (!std::isfinite(checkpoint.pressure[i])) {
      throw InvalidArgumentError("validateSIMPLECheckpoint: non-finite pressure at cell " +
                                 std::to_string(i));
    }
  }
  for (Index i = 0; i < checkpoint.massFlux.size(); ++i) {
    if (!std::isfinite(checkpoint.massFlux[i])) {
      throw InvalidArgumentError("validateSIMPLECheckpoint: non-finite mass flux at face " +
                                 std::to_string(i));
    }
  }

  const bool threeDimensional = checkpoint.dimension == 3;
  for (std::size_t i = 0; i < checkpoint.history.size(); ++i) {
    const SIMPLEIterationRecord& record = checkpoint.history[i];
    const bool finite = std::isfinite(record.u) && std::isfinite(record.v) &&
                        std::isfinite(record.pressure) && std::isfinite(record.continuity) &&
                        std::isfinite(record.globalImbalance) &&
                        (!record.w.has_value() || std::isfinite(*record.w)) &&
                        (!record.turbulence.has_value() || std::isfinite(*record.turbulence));
    if (!finite) {
      throw InvalidArgumentError("validateSIMPLECheckpoint: non-finite residual in history entry " +
                                 std::to_string(i));
    }
    if (record.w.has_value() != threeDimensional) {
      throw InvalidArgumentError("validateSIMPLECheckpoint: history entry " + std::to_string(i) +
                                 (threeDimensional ? " has no w residual but dimension is 3"
                                                   : " carries a w residual but dimension is 2"));
    }
  }
}

void validateSIMPLECheckpoint(const SIMPLECheckpoint& checkpoint, const cfd::mesh::Mesh& mesh) {
  validateSIMPLECheckpoint(checkpoint);

  if (checkpoint.cellCount != mesh.numberOfCells() ||
      checkpoint.faceCount != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: mesh identity mismatch -- checkpoint has " +
        std::to_string(checkpoint.cellCount) + " cells / " + std::to_string(checkpoint.faceCount) +
        " faces, this mesh has " + std::to_string(mesh.numberOfCells()) + " / " +
        std::to_string(mesh.numberOfFaces()));
  }
  if (checkpoint.dimension != mesh.dimension()) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: mesh identity mismatch -- checkpoint is " +
        std::to_string(checkpoint.dimension) + "D, this mesh is " +
        std::to_string(mesh.dimension()) + "D");
  }
  // Cell and face counts alone cannot tell two different meshes apart; the
  // fingerprint can. Same argument, and the same instrument, the P2 restart
  // lineage already uses.
  const std::string fingerprint = cfd::mesh::computeMeshFingerprint(mesh);
  if (checkpoint.meshFingerprint != fingerprint) {
    throw InvalidArgumentError(
        "validateSIMPLECheckpoint: mesh identity mismatch -- checkpoint fingerprint " +
        checkpoint.meshFingerprint + ", this mesh " + fingerprint);
  }
}

SIMPLECheckpoint makeSIMPLECheckpoint(const SIMPLEResult& result, const cfd::mesh::Mesh& mesh,
                                      std::string caseFingerprint, std::string backend,
                                      std::string applicationVersion) {
  SIMPLECheckpoint checkpoint;
  checkpoint.applicationVersion = std::move(applicationVersion);
  checkpoint.backend = std::move(backend);
  checkpoint.caseFingerprint = std::move(caseFingerprint);
  checkpoint.meshFingerprint = cfd::mesh::computeMeshFingerprint(mesh);
  checkpoint.cellCount = mesh.numberOfCells();
  checkpoint.faceCount = mesh.numberOfFaces();
  checkpoint.dimension = mesh.dimension();
  checkpoint.completedIterations = result.iterations;
  checkpoint.status = statusName(result.status);
  checkpoint.velocity = result.velocity;
  checkpoint.pressure = result.pressure;
  checkpoint.massFlux = result.massFlux;
  // Reuses the one place that knows which of SIMPLEResult's histories make up
  // an outer residual sample, so the checkpoint cannot drift from the
  // continuation.
  checkpoint.history = makeSIMPLEContinuation(result).history;

  validateSIMPLECheckpoint(checkpoint, mesh);
  return checkpoint;
}

SIMPLEContinuation makeContinuationFromCheckpoint(const SIMPLECheckpoint& checkpoint,
                                                  Index iterationBudget) {
  SIMPLEContinuation continuation;
  continuation.completedIterations = checkpoint.completedIterations;
  continuation.massFlux = checkpoint.massFlux;
  continuation.history = checkpoint.history;
  continuation.iterationBudget = iterationBudget;
  return continuation;
}

}  // namespace cfd::pressure_velocity
