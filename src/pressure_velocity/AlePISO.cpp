#include "cfd/pressure_velocity/AlePISO.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "PisoStep.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/MassFlux.hpp"

namespace cfd::pressure_velocity {

using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshMotion;
using cfd::mesh::MeshMotionStep;
using cfd::physics::FluidProperties;
using cfd::solver::TransientState;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepStatus;

std::vector<std::string> checkBoundaryMotion(const Mesh& mesh,
                                             const BoundaryConditionSet& velocityBoundaries,
                                             const MeshMotionStep& step) {
  if (step.meshVolumeFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("checkBoundaryMotion: the motion step does not match the mesh");
  }
  std::vector<std::string> problems;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (!velocityBoundaries.has(patch.name())) continue;
    const auto& condition = velocityBoundaries.get(patch.name());
    const BoundaryConditionType type = condition.type();
    if (type != BoundaryConditionType::Wall && type != BoundaryConditionType::MovingWall &&
        type != BoundaryConditionType::Symmetry) {
      continue;
    }
    Vector3 wallVelocity{};
    if (type == BoundaryConditionType::MovingWall) {
      wallVelocity = dynamic_cast<const cfd::boundary::MovingWall&>(condition).velocity();
    }
    const Real tolerance = 1e-8 * (magnitude(wallVelocity) + step.maxVertexSpeed);
    Real worstMismatch = 0.0;
    Index worstFace = 0;
    Real worstMeshNormal = 0.0;
    Real worstWallNormal = 0.0;
    for (const Index faceId : patch.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real area = face.area();
      const Real meshNormal = step.meshVolumeFlux[faceId] / area;
      const Real wallNormal = dot(wallVelocity, face.areaVector()) / area;
      const Real mismatch = std::abs(wallNormal - meshNormal);
      if (mismatch > worstMismatch) {
        worstMismatch = mismatch;
        worstFace = faceId;
        worstMeshNormal = meshNormal;
        worstWallNormal = wallNormal;
      }
    }
    if (worstMismatch > tolerance) {
      std::ostringstream out;
      out.precision(6);
      out << "patch '" << patch.name() << "' (" << condition.name() << "): the boundary moves "
          << "normally at " << worstMeshNormal << " (face " << worstFace << ") but the "
          << (type == BoundaryConditionType::MovingWall
                  ? "prescribed wall velocity's normal component is "
                  : (type == BoundaryConditionType::Wall ? "stationary wall allows "
                                                         : "symmetry plane allows "))
          << worstWallNormal << " (mismatch " << worstMismatch << " > tolerance " << tolerance
          << "); the fluid cannot cross this boundary, so its normal velocity must equal the "
             "boundary's";
      problems.push_back(out.str());
    }
  }
  return problems;
}

AleConservation evaluateAleConservation(const Mesh& mesh, const MeshMotionStep& step,
                                        const SurfaceField& massFlux, Real density) {
  if (massFlux.size() != mesh.numberOfFaces() ||
      step.meshVolumeFlux.size() != mesh.numberOfFaces() ||
      step.previousVolumes.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("evaluateAleConservation: sizes do not match the mesh");
  }
  SurfaceField meshFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) meshFlux[f] = step.meshVolumeFlux[f];
  const SurfaceField relative = cfd::physics::relativeMassFlux(massFlux, meshFlux, density);

  AleConservation result;
  result.cellMassResidual.assign(mesh.numberOfCells(), 0.0);
  Real totalNow = 0.0;
  Real totalBefore = 0.0;
  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const Real flux = relative[faceId];
      sum += (mesh.face(faceId).owner() == cell.id()) ? flux : -flux;
    }
    if (step.moved) {
      sum += (density * (cell.volume() - step.previousVolumes[cell.id()])) / step.dt;
    }
    result.cellMassResidual[cell.id()] = sum;
    result.maxCellMassResidual = std::max(result.maxCellMassResidual, std::abs(sum));
    totalNow += cell.volume();
    totalBefore += step.previousVolumes[cell.id()];
  }
  Real boundary = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) boundary += relative[faceId];
  }
  result.boundaryRelativeFlux = boundary;
  result.globalMassResidual =
      step.moved ? boundary + ((density * (totalNow - totalBefore)) / step.dt) : boundary;
  return result;
}

AlePISO::AlePISO(MeshMotion& motion, const FluidProperties& fluid,
                 const BoundaryConditionSet& velocityBoundaries,
                 const BoundaryConditionSet& pressureBoundaries, PISOSettings settings,
                 Index referenceCell)
    : motion_(motion),
      fluid_(fluid),
      velocityBoundaries_(velocityBoundaries),
      pressureBoundaries_(pressureBoundaries),
      settings_(settings),
      referenceCell_(referenceCell) {}

TransientStepResult AlePISO::solveTimeStep(const TransientState& previousState, Real dt) const {
  const Mesh& mesh = motion_.mesh();
  cfd::mesh::requireTwoDimensional(mesh, "AlePISO");
  // What can be checked before moving the mesh is checked first: a bad state
  // or dt leaves the mesh untouched.
  if (previousState.velocity.size() != mesh.numberOfCells() ||
      previousState.pressure.size() != mesh.numberOfCells() ||
      previousState.massFlux.size() != mesh.numberOfFaces() || !std::isfinite(dt) || !(dt > 0.0)) {
    TransientStepResult invalid;
    invalid.status = TransientStepStatus::InvalidConfiguration;
    invalid.state = previousState;
    return invalid;
  }

  // Move the mesh to t^{n+1}; an invalid cell throws with the mesh unchanged.
  const MeshMotionStep& step = motion_.advance(motion_.time() + dt);
  const std::vector<std::string> problems = checkBoundaryMotion(mesh, velocityBoundaries_, step);
  if (!problems.empty()) {
    motion_.revert();
    std::string message =
        "AlePISO: the prescribed mesh motion is inconsistent with the velocity boundary "
        "conditions:";
    for (const auto& problem : problems) message += " " + problem + ";";
    throw InvalidArgumentError(message);
  }

  ScalarField previousVolume(mesh.numberOfCells());
  for (Index c = 0; c < mesh.numberOfCells(); ++c) previousVolume[c] = step.previousVolumes[c];
  SurfaceField meshFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) meshFlux[f] = step.meshVolumeFlux[f];
  const SurfaceField convectingFlux =
      cfd::physics::relativeMassFlux(previousState.massFlux, meshFlux, fluid_.density());
  const detail::AleStepTerms ale{&previousVolume, &convectingFlux};

  TransientStepResult result;
  try {
    result = detail::solvePisoStep(mesh, fluid_, velocityBoundaries_, pressureBoundaries_,
                                   settings_, referenceCell_, nullptr, previousState, dt, &ale);
  } catch (...) {
    motion_.revert();  // never leave the mesh at a time whose step did not complete
    throw;
  }
  if (result.status != TransientStepStatus::Converged) motion_.revert();
  return result;
}

void AlePISO::onStepRejected() const { motion_.revert(); }

const PISOSettings& AlePISO::settings() const noexcept { return settings_; }
Index AlePISO::referenceCell() const noexcept { return referenceCell_; }
const MeshMotion& AlePISO::motion() const noexcept { return motion_; }

}  // namespace cfd::pressure_velocity
