#include "cfd/compressible/CompressibleContinuity.hpp"

#include <algorithm>
#include <cmath>

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

  return result;
}

}  // namespace cfd::compressible
