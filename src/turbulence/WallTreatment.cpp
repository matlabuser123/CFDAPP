#include "cfd/turbulence/WallTreatment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::turbulence {

using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

std::vector<WallFace> collectWallFaces(const Mesh& mesh,
                                       const BoundaryConditionSet& velocityBoundaries) {
  std::vector<WallFace> wallFaces;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& bc = velocityBoundaries.get(patch.name());
    const bool isWall =
        bc.type() == BoundaryConditionType::Wall || bc.type() == BoundaryConditionType::MovingWall;
    if (!isWall) continue;

    // A MovingWall's own velocity, so the wall function always sees the
    // velocity RELATIVE to the surface. A lid-driven cavity's moving lid has a
    // large absolute velocity and zero relative velocity at the wall itself;
    // using the absolute value would invent shear that is not there.
    cfd::Vector2 wallVelocity{};
    if (const auto* movingWall = dynamic_cast<const cfd::boundary::MovingWall*>(&bc)) {
      wallVelocity = movingWall->velocity();
    }

    for (const Index faceId : patch.faceIds()) {
      const Face& face = mesh.face(faceId);
      const Index ownerId = face.owner();
      const cfd::Vector2 normal = MeshGeometry::unitNormal(face);

      // The PERPENDICULAR distance, not the centroid-to-centroid distance.
      // They differ whenever the grid lines meet the wall obliquely, and the
      // wall function's y is a wall-NORMAL coordinate -- the same correction
      // computeWallDistance already makes, for the same reason.
      const cfd::Vector2 offset = mesh.cell(ownerId).centroid() - face.centroid();
      const Real wallDistance = std::abs(dot(offset, normal));
      if (!std::isfinite(wallDistance) || !(wallDistance > 0.0)) {
        throw InvalidArgumentError("collectWallFaces: wall face " + std::to_string(faceId) +
                                   " has a non-positive wall-normal distance to its owner cell");
      }

      wallFaces.push_back(
          WallFace{faceId, ownerId, wallDistance, face.area(), normal, wallVelocity});
    }
  }
  if (wallFaces.empty()) {
    throw InvalidArgumentError("collectWallFaces: velocityBoundaries has no Wall/MovingWall patch");
  }
  return wallFaces;
}

WallFaceState evaluateWallFace(const Mesh& mesh, const WallFace& wallFace,
                               const VectorField& velocity, const ScalarField& turbulentEnergy,
                               Real density, Real dynamicViscosity,
                               const WallFunctionCoefficients& coefficients,
                               const WallTreatmentOptions& options) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n || turbulentEnergy.size() != n) {
    throw InvalidArgumentError(
        "evaluateWallFace: velocity/turbulentEnergy size does not match mesh cell count");
  }

  const Index owner = wallFace.ownerCell;

  // Wall-PARALLEL component of the relative velocity. The normal component
  // carries no wall shear and including it would inflate tau_w wherever the
  // solution has a small normal velocity at a wall (which it does, transiently,
  // throughout a SIMPLE solve).
  const cfd::Vector2 relative = velocity[owner] - wallFace.wallVelocity;
  const cfd::Vector2 tangential =
      relative - (wallFace.unitNormal * dot(relative, wallFace.unitNormal));

  WallFaceState state;
  state.tangentialVelocity = magnitude(tangential);
  if (options.mode == WallTreatmentMode::Automatic ||
      options.mode == WallTreatmentMode::AutomaticSpalding ||
      options.mode == WallTreatmentMode::AutomaticMomentumFlux) {
    // Menter automatic treatment. Every quantity comes from the blended
    // state, which is a total function on its domain -- in particular it
    // is defined at tau_w = 0, which is why separated flow needs it.
    const auto automatic =
        options.mode == WallTreatmentMode::AutomaticSpalding
            ? compositeWallState(state.tangentialVelocity, turbulentEnergy[owner],
                                 wallFace.wallDistance, density, dynamicViscosity, options.beta1,
                                 coefficients)
            : automaticWallState(state.tangentialVelocity, turbulentEnergy[owner],
                                 wallFace.wallDistance, density, dynamicViscosity, options.beta1,
                                 coefficients);
    // u_tau*, NOT the log branch of the shear blend -- the equilibrium
    // production below is built on u_tau*, exactly as the high-Re path is.
    state.equilibriumFrictionVelocity = automatic.equilibriumFrictionVelocity;
    state.wallShearStress = automatic.wallShearStress;
    state.frictionVelocity = automatic.frictionVelocity;
    state.yPlus = automatic.yPlus;
    state.resolution = classifyWallResolution(state.yPlus);
    state.regime = automatic.regime;
    state.logLayerWeight = automatic.logLayerWeight;
    state.specificDissipation = automatic.specificDissipation;
    // epsilon stays consistent with the imposed omega through
    // epsilon = C_mu k omega -- the same identity the high-Re path
    // satisfies, so both modes describe the same near-wall state.
    state.dissipation = coefficients.cMu * turbulentEnergy[owner] * automatic.specificDissipation;
    state.production =
        wallAdjacentProduction(state.wallShearStress, state.equilibriumFrictionVelocity,
                               wallFace.wallDistance, coefficients);
    state.effectiveViscosity = automatic.effectiveViscosity;
    if (options.mode == WallTreatmentMode::AutomaticMomentumFlux) {
      // ONLY the momentum wall flux changes. omega, the branch report, the
      // log-layer weight and the production weighting all stay exactly as the
      // blended mode computes them -- the one-cell diagnostic showed the omega
      // crossover is not the dominant channel error, so it is left alone.
      const auto momentum = automaticMomentumWallState(
          state.tangentialVelocity, turbulentEnergy[owner], wallFace.wallDistance, density,
          dynamicViscosity, coefficients);
      state.wallShearStress = momentum.wallShearStress;
      state.frictionVelocity = momentum.wallFrictionVelocity;
      state.yPlus = momentum.yStar;
      state.resolution = classifyWallResolution(state.yPlus);
      state.effectiveViscosity = momentum.effectiveViscosity;
    }
    // The mu_t >= 0 floor is reached naturally by the viscous branch
    // rather than imposed, so it never fires in this mode.
    state.viscosityFloored = false;
    return state;
  }

  state.equilibriumFrictionVelocity =
      frictionVelocityFromTurbulentEnergy(turbulentEnergy[owner], coefficients.cMu);

  state.wallShearStress = wallShearStressLaunderSpalding(
      state.tangentialVelocity, turbulentEnergy[owner], wallFace.wallDistance, density,
      dynamicViscosity, coefficients);
  state.frictionVelocity = frictionVelocityFromWallShear(state.wallShearStress, density);

  // y+ is REPORTED from the actual wall shear, not from the equilibrium
  // estimate the wall function used internally: a user reading y+ wants the
  // resolution of the solution they have, not of the model's internal guess.
  state.yPlus = yPlus(density, state.frictionVelocity, wallFace.wallDistance, dynamicViscosity);
  state.resolution = classifyWallResolution(state.yPlus);

  state.dissipation =
      wallAdjacentDissipation(turbulentEnergy[owner], wallFace.wallDistance, coefficients);
  state.specificDissipation =
      wallAdjacentSpecificDissipation(turbulentEnergy[owner], wallFace.wallDistance, coefficients);
  state.production =
      wallAdjacentProduction(state.wallShearStress, state.equilibriumFrictionVelocity,
                             wallFace.wallDistance, coefficients);

  // The bridge to momentum. effectiveWallViscosity refuses to return less than
  // the molecular value; at a genuinely stagnant wall cell tau_w is zero and it
  // returns mu, which is the correct laminar limit.
  state.effectiveViscosity =
      effectiveWallViscosity(state.wallShearStress, state.tangentialVelocity, wallFace.wallDistance,
                             dynamicViscosity, &state.viscosityFloored);
  return state;
}

WallTreatment computeWallTreatment(const Mesh& mesh, const std::vector<WallFace>& wallFaces,
                                   const VectorField& velocity, const ScalarField& turbulentEnergy,
                                   Real density, Real dynamicViscosity,
                                   const WallFunctionCoefficients& coefficients,
                                   const WallTreatmentOptions& options) {
  const Index cellCount = mesh.numberOfCells();

  WallTreatment treatment;
  treatment.faces.reserve(wallFaces.size());
  treatment.wallViscosity = SurfaceField(mesh.numberOfFaces(), 0.0);
  treatment.yPlus = ScalarField(cellCount, 0.0);
  treatment.wallShearStress = ScalarField(cellCount, 0.0);
  treatment.minimumYPlus = std::numeric_limits<Real>::infinity();
  treatment.maximumYPlus = 0.0;

  // A corner cell touches two wall faces. It must be imposed ONCE, from its
  // nearest wall -- imposing two values in one cell is not a choice between
  // them, it is a bug. Tracked by remembering which wall distance won.
  //
  // cfd::Index is std::size_t, i.e. UNSIGNED, so "not yet assigned" needs an
  // explicit sentinel rather than -1 with a `< 0` test -- that test is never
  // true for an unsigned type, which silently sent every face down the
  // already-assigned branch and indexed at SIZE_MAX.
  constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
  std::vector<std::size_t> slotForCell(static_cast<std::size_t>(cellCount), kNoSlot);
  std::vector<Real> winningDistance(static_cast<std::size_t>(cellCount),
                                    std::numeric_limits<Real>::infinity());

  for (const WallFace& wallFace : wallFaces) {
    const WallFaceState state = evaluateWallFace(mesh, wallFace, velocity, turbulentEnergy, density,
                                                 dynamicViscosity, coefficients, options);
    treatment.faces.push_back(state);
    treatment.wallViscosity[wallFace.faceId] = state.effectiveViscosity;

    if (state.resolution != WallResolution::LogLayer) {
      ++treatment.facesOutsideValidBand;
    }
    if (state.viscosityFloored) {
      ++treatment.facesWithFlooredViscosity;
    }
    switch (state.regime) {
      case WallRegime::ViscousSublayer:
        ++treatment.viscousSublayerFaces;
        break;
      case WallRegime::Blended:
        ++treatment.blendedFaces;
        break;
      case WallRegime::LogLayer:
        ++treatment.logLayerFaces;
        break;
    }
    treatment.minimumYPlus = std::min(treatment.minimumYPlus, state.yPlus);
    treatment.maximumYPlus = std::max(treatment.maximumYPlus, state.yPlus);

    const auto owner = static_cast<std::size_t>(wallFace.ownerCell);
    if (wallFace.wallDistance < winningDistance[owner]) {
      winningDistance[owner] = wallFace.wallDistance;
      if (slotForCell[owner] == kNoSlot) {
        slotForCell[owner] = treatment.wallCells.size();
        treatment.wallCells.push_back(wallFace.ownerCell);
        treatment.wallCellDissipation.push_back(state.dissipation);
        treatment.wallCellSpecificDissipation.push_back(state.specificDissipation);
        treatment.wallCellProduction.push_back(state.production);
        treatment.wallCellLogLayerWeight.push_back(state.logLayerWeight);
      } else {
        const std::size_t slot = slotForCell[owner];
        treatment.wallCellDissipation[slot] = state.dissipation;
        treatment.wallCellSpecificDissipation[slot] = state.specificDissipation;
        treatment.wallCellProduction[slot] = state.production;
        treatment.wallCellLogLayerWeight[slot] = state.logLayerWeight;
      }
      treatment.yPlus[wallFace.ownerCell] = state.yPlus;
      treatment.wallShearStress[wallFace.ownerCell] = state.wallShearStress;
    }
  }

  if (!std::isfinite(treatment.minimumYPlus)) treatment.minimumYPlus = 0.0;
  return treatment;
}

std::string describeWallResolution(const WallTreatment& treatment) {
  const auto faceCount = static_cast<Index>(treatment.faces.size());
  std::string message = "wall resolution: y+ in [" + std::to_string(treatment.minimumYPlus) + ", " +
                        std::to_string(treatment.maximumYPlus) + "] over " +
                        std::to_string(faceCount) + " wall faces";
  if (treatment.facesOutsideValidBand == 0) {
    return message + "; all inside the wall function's valid band [" +
           std::to_string(kWallFunctionMinimumYPlus) + ", " +
           std::to_string(kWallFunctionMaximumYPlus) + "]";
  }
  return message + "; " + std::to_string(treatment.facesOutsideValidBand) +
         " face(s) OUTSIDE the valid band [" + std::to_string(kWallFunctionMinimumYPlus) + ", " +
         std::to_string(kWallFunctionMaximumYPlus) +
         "] -- the high-Re wall function is being applied where its own derivation does not "
         "hold, so the result is not trustworthy. Refine or coarsen the wall-normal spacing.";
}

}  // namespace cfd::turbulence
