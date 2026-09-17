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

MassBalance computeMassBalance(const Mesh& mesh, const SurfaceField& massFlux) {
  const ContinuityResult continuity = evaluateContinuity(mesh, massFlux);
  MassBalance balance;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      const Real flux = massFlux[faceId];
      if (flux < 0.0) balance.inflow -= flux;
      if (flux > 0.0) balance.outflow += flux;
    }
  }
  balance.net = continuity.globalNetFlux;
  const Real throughFlow = std::max(balance.inflow, balance.outflow);
  balance.relativeImbalance = throughFlow > 0.0 ? std::abs(balance.net) / throughFlow : 0.0;

  Real sumSquares = 0.0;
  for (Index i = 0; i < continuity.cellImbalance.size(); ++i) {
    sumSquares += continuity.cellImbalance[i] * continuity.cellImbalance[i];
  }
  balance.maxCellImbalance = continuity.maxCellImbalance;
  balance.rmsCellImbalance =
      std::sqrt(sumSquares / static_cast<Real>(continuity.cellImbalance.size()));

  Real sumInternal = 0.0;
  Index internalFaces = 0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    sumInternal += std::abs(massFlux[face.id()]);
    ++internalFaces;
  }
  const Real meanInternal =
      internalFaces > 0 ? sumInternal / static_cast<Real>(internalFaces) : 0.0;
  balance.fluxScale = std::max(throughFlow, meanInternal);
  balance.normalizedContinuity =
      balance.fluxScale > 0.0 ? balance.rmsCellImbalance / balance.fluxScale : 0.0;
  return balance;
}

}  // namespace cfd::physics
