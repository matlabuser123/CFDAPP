#include "cfd/physics/ContinuityEquation.hpp"

#include <algorithm>
#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::physics {

using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;

ContinuityResult evaluateContinuity(const Mesh& mesh, const SurfaceField& massFlux) {
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("evaluateContinuity: massFlux size does not match mesh face count");
  }

  ContinuityResult result;
  result.cellImbalance = ScalarField(mesh.numberOfCells(), 0.0);

  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real flux = massFlux[faceId];
      // Owner-oriented: +flux for the owner, -flux for the neighbor
      // (mirrors cfd::discretization::divergence's face-once
      // accumulation, so internal contributions cancel exactly when
      // summed over the whole mesh).
      sum += (face.owner() == cell.id()) ? flux : -flux;
    }
    result.cellImbalance[cell.id()] = sum;
  }

  Real globalNetFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      globalNetFlux += massFlux[faceId];
    }
  }
  result.globalNetFlux = globalNetFlux;

  Real totalAbsoluteImbalance = 0.0;
  Real maxCellImbalance = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real absImbalance = std::abs(result.cellImbalance[cell.id()]);
    totalAbsoluteImbalance += absImbalance;
    maxCellImbalance = std::max(maxCellImbalance, absImbalance);
  }
  result.totalAbsoluteImbalance = totalAbsoluteImbalance;
  result.maxCellImbalance = maxCellImbalance;

  return result;
}

}  // namespace cfd::physics
