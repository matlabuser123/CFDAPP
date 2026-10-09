#include "cfd/compressible/CompressibleContinuity.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::compressible {

using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

CompressibleContinuityResult evaluateCompressibleContinuity(const Mesh& mesh,
                                                            const ScalarField& densityOld,
                                                            const ScalarField& densityNew,
                                                            const SurfaceField& massFlux, Real dt) {
  if (densityOld.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluateCompressibleContinuity: densityOld size does not match mesh cell count");
  }
  if (densityNew.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "evaluateCompressibleContinuity: densityNew size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "evaluateCompressibleContinuity: massFlux size does not match mesh face count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("evaluateCompressibleContinuity: dt must be finite and > 0");
  }
  // P17.3: the VALUES, not only the array lengths and dt.
  //
  // Measured on the pre-P17.3 tree (results/p17/compressible_continuity/logs/00_dryrun.log
  // group H), an invalid input did not merely propagate -- it produced a *reassuring*
  // answer:
  //
  //   rhoNew[0] = NaN     -> maxCellImbalance 0    "perfectly conserved"
  //   massFlux[0] = NaN   -> maxCellImbalance 0    "perfectly conserved"
  //   rhoNew[0] = -2.0    -> totalMassNew 0        finite, plausible, catastrophic
  //
  // The NaN cases are the dangerous ones and the reason this check is here rather than
  // downstream: std::max(a, b) is (a < b) ? b : a, so with a running maximum of 0 and
  // b = NaN the comparison 0 < NaN is false and the NaN is SILENTLY SKIPPED. The max
  // reduction therefore reports zero imbalance for a NaN field, while
  // totalAbsoluteImbalance becomes NaN -- the two metrics disagree, and a solver
  // watching the max would declare convergence on a non-finite state.
  //
  // Rejected, never clamped, matching the policy P17.1 and P17.2 established and
  // thermal/Enthalpy.hpp records: a clamped state yields numbers no physical state
  // corresponds to.
  //
  // massFlux is checked for FINITENESS ONLY. A face flux is legitimately negative
  // (inflow) and legitimately zero (a wall), so a positivity rule would be wrong for it
  // -- the same distinction P17.1 drew for gauge pressure.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!std::isfinite(densityOld[i]) || !(densityOld[i] > 0.0)) {
      throw InvalidArgumentError("evaluateCompressibleContinuity: cell " + std::to_string(i) +
                                 ": densityOld must be finite and > 0");
    }
    if (!std::isfinite(densityNew[i]) || !(densityNew[i] > 0.0)) {
      throw InvalidArgumentError("evaluateCompressibleContinuity: cell " + std::to_string(i) +
                                 ": densityNew must be finite and > 0");
    }
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    if (!std::isfinite(massFlux[f])) {
      throw InvalidArgumentError("evaluateCompressibleContinuity: face " + std::to_string(f) +
                                 ": massFlux must be finite");
    }
  }

  const Index n = mesh.numberOfCells();
  CompressibleContinuityResult result;
  result.cellImbalance = ScalarField(n, 0.0);

  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    result.cellImbalance[id] = (densityNew[id] - densityOld[id]) * cell.volume() / dt;
    result.totalMassOld += densityOld[id] * cell.volume();
    result.totalMassNew += densityNew[id] * cell.volume();
  }

  // Owner-oriented net outward face flux, same convention as
  // physics::evaluateContinuity: +F_f for the owner, -F_f for the
  // neighbor.
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const auto& face = mesh.face(faceId);
    const Real flux = massFlux[faceId];
    result.cellImbalance[face.owner()] += flux;
    if (!face.isBoundary()) {
      result.cellImbalance[*face.neighbor()] -= flux;
    }
  }

  for (Index i = 0; i < n; ++i) {
    result.totalAbsoluteImbalance += std::abs(result.cellImbalance[i]);
    result.maxCellImbalance = std::max(result.maxCellImbalance, std::abs(result.cellImbalance[i]));
  }

  // P17.3: the domain's net mass in/outflow. BOUNDARY faces only -- internal faces cancel
  // pairwise and contribute nothing to the domain balance. Accumulated over
  // mesh.boundaryPatches() in the same order and with the same outward-positive
  // convention as physics::evaluateContinuity, so the two are directly comparable.
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      result.globalNetFlux += massFlux[faceId];
    }
  }

  return result;
}

}  // namespace cfd::compressible
