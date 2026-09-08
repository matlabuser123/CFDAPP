#include "cfd/solver/RestartSnapshot.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::solver {

namespace {

bool allFinite(const cfd::fields::ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

bool allFinite(const cfd::fields::SurfaceField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

bool allFinite(const cfd::fields::VectorField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i].x) || !std::isfinite(field[i].y)) return false;
  }
  return true;
}

}  // namespace

RestartSnapshot makeRestartSnapshot(const cfd::mesh::Mesh& mesh, const TransientState& state,
                                    Real time, Real deltaTUsedToReachThisState, Index step) {
  RestartSnapshot snapshot;
  snapshot.formatVersion = kRestartFormatVersion;
  snapshot.time = time;
  snapshot.step = step;
  snapshot.deltaT = deltaTUsedToReachThisState;
  snapshot.velocity = state.velocity;
  snapshot.pressure = state.pressure;
  snapshot.massFlux = state.massFlux;
  snapshot.cellCount = mesh.numberOfCells();
  snapshot.faceCount = mesh.numberOfFaces();
  // meshFingerprint deliberately left empty -- Restart-B's own scope,
  // see this struct's own doc comment.

  validateRestartSnapshot(snapshot, mesh);
  return snapshot;
}

void validateRestartSnapshot(const RestartSnapshot& snapshot, const cfd::mesh::Mesh& mesh) {
  if (snapshot.formatVersion != kRestartFormatVersion) {
    throw InvalidArgumentError("validateRestartSnapshot: unsupported restart format version");
  }
  if (!std::isfinite(snapshot.time)) {
    throw InvalidArgumentError("validateRestartSnapshot: time must be finite");
  }
  if (!std::isfinite(snapshot.deltaT) || !(snapshot.deltaT > 0.0)) {
    throw InvalidArgumentError("validateRestartSnapshot: deltaT must be finite and > 0");
  }
  if (snapshot.velocity.size() != snapshot.cellCount ||
      snapshot.pressure.size() != snapshot.cellCount) {
    throw InvalidArgumentError(
        "validateRestartSnapshot: velocity/pressure size does not match cellCount");
  }
  if (snapshot.massFlux.size() != snapshot.faceCount) {
    throw InvalidArgumentError("validateRestartSnapshot: massFlux size does not match faceCount");
  }
  if (!allFinite(snapshot.velocity) || !allFinite(snapshot.pressure) ||
      !allFinite(snapshot.massFlux)) {
    throw InvalidArgumentError(
        "validateRestartSnapshot: non-finite value in velocity/pressure/massFlux");
  }
  // Mesh-identity check -- count-only for now (meshFingerprint is not
  // yet populated or compared, see RestartSnapshot's own doc comment).
  if (snapshot.cellCount != mesh.numberOfCells() || snapshot.faceCount != mesh.numberOfFaces()) {
    throw InvalidArgumentError("validateRestartSnapshot: mesh identity mismatch");
  }
}

}  // namespace cfd::solver
