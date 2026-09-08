#include "cfd/solver/CFL.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::solver {

using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

CFLResult calculateCFL(const Mesh& mesh, const SurfaceField& faceMassFlux, Real density, Real dt) {
  if (faceMassFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("calculateCFL: faceMassFlux size does not match mesh face count");
  }
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("calculateCFL: dt must be finite and > 0");
  }
  if (!std::isfinite(density) || !(density > 0.0)) {
    throw InvalidArgumentError("calculateCFL: density must be finite and > 0");
  }

  Real maxCFL = 0.0;
  Index maxCFLCell = 0;
  Real volumeWeightedSum = 0.0;
  Real totalVolume = 0.0;

  for (const auto& cell : mesh.cells()) {
    Real absFluxSum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      absFluxSum += std::abs(faceMassFlux[faceId]);
    }
    // See CFL.hpp for the 1/2 factor's derivation.
    const Real cellCFL = (dt / (2.0 * cell.volume())) * (absFluxSum / density);

    if (cellCFL > maxCFL) {
      maxCFL = cellCFL;
      maxCFLCell = cell.id();
    }
    volumeWeightedSum += cellCFL * cell.volume();
    totalVolume += cell.volume();
  }

  return CFLResult{maxCFL, volumeWeightedSum / totalVolume, maxCFLCell};
}

}  // namespace cfd::solver
