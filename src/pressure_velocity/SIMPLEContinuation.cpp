#include "cfd/pressure_velocity/SIMPLEContinuation.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::pressure_velocity {

namespace {

void requireFinite(Real value, const char* field, Index index) {
  if (!std::isfinite(value)) {
    throw InvalidArgumentError("validateSIMPLEContinuation: non-finite " + std::string(field) +
                               " in history entry " + std::to_string(index));
  }
}

}  // namespace

void validateSIMPLEContinuation(const SIMPLEContinuation& continuation, const cfd::mesh::Mesh& mesh,
                                bool threeDimensional) {
  if (continuation.completedIterations == 0) {
    if (!continuation.massFlux.empty()) {
      throw InvalidArgumentError(
          "validateSIMPLEContinuation: massFlux must be empty when completedIterations is 0");
    }
    if (!continuation.history.empty()) {
      throw InvalidArgumentError(
          "validateSIMPLEContinuation: history must be empty when completedIterations is 0");
    }
    return;
  }

  if (continuation.massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("validateSIMPLEContinuation: massFlux size " +
                               std::to_string(continuation.massFlux.size()) +
                               " does not match mesh face count " +
                               std::to_string(mesh.numberOfFaces()));
  }
  if (continuation.history.size() != continuation.completedIterations) {
    throw InvalidArgumentError(
        "validateSIMPLEContinuation: history size " + std::to_string(continuation.history.size()) +
        " does not match completedIterations " + std::to_string(continuation.completedIterations));
  }

  for (Index i = 0; i < continuation.massFlux.size(); ++i) {
    if (!std::isfinite(continuation.massFlux[i])) {
      throw InvalidArgumentError("validateSIMPLEContinuation: non-finite massFlux at face " +
                                 std::to_string(i));
    }
  }

  for (std::size_t i = 0; i < continuation.history.size(); ++i) {
    const SIMPLEIterationRecord& record = continuation.history[i];
    requireFinite(record.u, "u residual", i);
    requireFinite(record.v, "v residual", i);
    requireFinite(record.pressure, "pressure residual", i);
    requireFinite(record.continuity, "continuity residual", i);
    requireFinite(record.globalImbalance, "global mass imbalance", i);
    // A 3D solve records a w residual every iteration and a 2D solve records
    // none. A history that disagrees with the mesh it is being resumed on is
    // not a history of this problem.
    if (threeDimensional != record.w.has_value()) {
      throw InvalidArgumentError("validateSIMPLEContinuation: history entry " + std::to_string(i) +
                                 (threeDimensional ? " has no w residual but the mesh is 3D"
                                                   : " carries a w residual but the mesh is 2D"));
    }
    if (record.w.has_value()) requireFinite(*record.w, "w residual", i);
    if (record.turbulence.has_value()) requireFinite(*record.turbulence, "turbulence residual", i);
  }
}

namespace {

void requireHistoryLength(const std::vector<Real>& history, Index expected, const char* name) {
  if (history.size() != expected) {
    throw InvalidArgumentError("makeSIMPLEContinuation: " + std::string(name) + " has " +
                               std::to_string(history.size()) + " entries but the result reports " +
                               std::to_string(expected) + " iterations");
  }
}

}  // namespace

SIMPLEContinuation makeSIMPLEContinuation(const SIMPLEResult& result, Index iterationBudget) {
  const Index n = result.iterations;
  requireHistoryLength(result.uResidualHistory, n, "uResidualHistory");
  requireHistoryLength(result.vResidualHistory, n, "vResidualHistory");
  requireHistoryLength(result.pressureResidualHistory, n, "pressureResidualHistory");
  requireHistoryLength(result.continuityHistory, n, "continuityHistory");
  requireHistoryLength(result.globalMassImbalanceHistory, n, "globalMassImbalanceHistory");

  // 3D solves record a w residual every iteration; 2D solves record none.
  // Anything between the two is a corrupted result, not a 2D/3D distinction.
  const bool hasW = !result.wResidualHistory.empty();
  if (hasW) requireHistoryLength(result.wResidualHistory, n, "wResidualHistory");

  // See SIMPLEResult.hpp: empty, or exactly one entry per iteration.
  const bool hasTurbulence = !result.turbulenceResidualHistory.empty();
  if (hasTurbulence)
    requireHistoryLength(result.turbulenceResidualHistory, n, "turbulenceResidualHistory");

  SIMPLEContinuation continuation;
  continuation.completedIterations = n;
  continuation.massFlux = result.massFlux;
  continuation.iterationBudget = iterationBudget;
  continuation.history.reserve(n);
  for (Index i = 0; i < n; ++i) {
    SIMPLEIterationRecord record;
    record.u = result.uResidualHistory[i];
    record.v = result.vResidualHistory[i];
    record.pressure = result.pressureResidualHistory[i];
    record.continuity = result.continuityHistory[i];
    record.globalImbalance = result.globalMassImbalanceHistory[i];
    if (hasW) record.w = result.wResidualHistory[i];
    if (hasTurbulence) record.turbulence = result.turbulenceResidualHistory[i];
    continuation.history.push_back(record);
  }
  return continuation;
}

}  // namespace cfd::pressure_velocity
