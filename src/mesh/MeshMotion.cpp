#include "cfd/mesh/MeshMotion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

// --- Prescribed motions ------------------------------------------------------------

Vector3 StationaryMotion::position(const Vector3& reference, Real /*elapsed*/) const {
  return reference;
}

std::string StationaryMotion::description() const { return "stationary"; }

AffineMotion::AffineMotion(const Matrix& rate, const Vector3& center, const Vector3& velocity)
    : rate_(rate), center_(center), velocity_(velocity) {
  for (const auto& row : rate_) {
    for (const Real value : row) {
      if (!std::isfinite(value)) throw InvalidArgumentError("AffineMotion: non-finite rate");
    }
  }
  if (!isFinite(center_) || !isFinite(velocity_)) {
    throw InvalidArgumentError("AffineMotion: non-finite center or velocity");
  }
}

Vector3 AffineMotion::velocity(const Vector3& reference) const noexcept {
  const Vector3 r = reference - center_;
  return Vector3{(rate_[0][0] * r.x) + (rate_[0][1] * r.y) + (rate_[0][2] * r.z) + velocity_.x,
                 (rate_[1][0] * r.x) + (rate_[1][1] * r.y) + (rate_[1][2] * r.z) + velocity_.y,
                 (rate_[2][0] * r.x) + (rate_[2][1] * r.y) + (rate_[2][2] * r.z) + velocity_.z};
}

Vector3 AffineMotion::position(const Vector3& reference, Real elapsed) const {
  return reference + (velocity(reference) * elapsed);
}

std::string AffineMotion::description() const {
  std::ostringstream out;
  out.precision(17);
  out << "affine: x = X + tau (G (X - c) + b), G = [";
  for (int i = 0; i < 3; ++i) {
    out << (i > 0 ? "; " : "") << rate_[i][0] << ", " << rate_[i][1] << ", " << rate_[i][2];
  }
  out << "], c = (" << center_.x << ", " << center_.y << ", " << center_.z << "), b = ("
      << velocity_.x << ", " << velocity_.y << ", " << velocity_.z << ")";
  return out.str();
}

SinusoidalMotion::SinusoidalMotion(const Vector3& lower, const Vector3& upper,
                                   const Vector3& amplitude, Real angularFrequency)
    : lower_(lower), upper_(upper), amplitude_(amplitude), angularFrequency_(angularFrequency) {
  if (!isFinite(lower_) || !isFinite(upper_) || !isFinite(amplitude_) ||
      !std::isfinite(angularFrequency_)) {
    throw InvalidArgumentError("SinusoidalMotion: non-finite argument");
  }
  if (!(upper_.x > lower_.x) && !(upper_.y > lower_.y) && !(upper_.z > lower_.z)) {
    throw InvalidArgumentError(
        "SinusoidalMotion: the box needs at least one axis with upper > lower");
  }
}

Real SinusoidalMotion::shape(const Vector3& reference) const noexcept {
  Real product = 1.0;
  const Real lo[3] = {lower_.x, lower_.y, lower_.z};
  const Real hi[3] = {upper_.x, upper_.y, upper_.z};
  const Real x[3] = {reference.x, reference.y, reference.z};
  for (int d = 0; d < 3; ++d) {
    if (!(hi[d] > lo[d])) continue;
    const Real s = (x[d] - lo[d]) / (hi[d] - lo[d]);
    // Exactly zero on and outside the box boundary (sin(pi) is 1.2e-16, not 0).
    if (!(s > 0.0) || !(s < 1.0)) return 0.0;
    product *= std::sin(constants::pi * s);
  }
  return product;
}

Vector3 SinusoidalMotion::position(const Vector3& reference, Real elapsed) const {
  return reference + (amplitude_ * (std::sin(angularFrequency_ * elapsed) * shape(reference)));
}

std::string SinusoidalMotion::description() const {
  std::ostringstream out;
  out.precision(17);
  out << "sinusoidal: x = X + sin(" << angularFrequency_ << " tau) prod sin(pi s_d) ("
      << amplitude_.x << ", " << amplitude_.y << ", " << amplitude_.z << "), box (" << lower_.x
      << ", " << lower_.y << ", " << lower_.z << ")-(" << upper_.x << ", " << upper_.y << ", "
      << upper_.z << ")";
  return out.str();
}

// --- MeshMotion --------------------------------------------------------------------

MeshMotion::MeshMotion(Mesh& mesh, std::shared_ptr<const PrescribedMotion> motion, Real startTime)
    : mesh_(mesh),
      motion_(std::move(motion)),
      topology_(MeshGeometry::structuredTopology(mesh)),
      startTime_(startTime),
      time_(startTime) {
  if (!motion_) throw InvalidArgumentError("MeshMotion: the prescribed motion is null");
  if (!std::isfinite(startTime_)) throw InvalidArgumentError("MeshMotion: non-finite start time");
  reference_ = topology_.vertices;
  current_ = reference_;
  const std::vector<Vector3> atStart = positionsAt(startTime_);
  for (std::size_t v = 0; v < atStart.size(); ++v) {
    if (!(atStart[v] == reference_[v])) {
      throw InvalidArgumentError("MeshMotion: the prescribed motion moves vertex " +
                                 std::to_string(v) +
                                 " at its start time (every vertex must start at its "
                                 "reference position)");
    }
  }
}

std::vector<Vector3> MeshMotion::positionsAt(Real time) const {
  std::vector<Vector3> positions(reference_.size());
  const Real elapsed = time - startTime_;
  for (std::size_t v = 0; v < reference_.size(); ++v) {
    positions[v] = motion_->position(reference_[v], elapsed);
    if (!isFinite(positions[v])) {
      throw InvalidArgumentError("MeshMotion: the prescribed motion gives vertex " +
                                 std::to_string(v) + " a non-finite position");
    }
    if (topology_.dimension == 2 && positions[v].z != 0.0) {
      throw InvalidArgumentError("MeshMotion: the prescribed motion moves vertex " +
                                 std::to_string(v) +
                                 " of a two-dimensional mesh out of the xy-plane");
    }
  }
  return positions;
}

const MeshMotionStep& MeshMotion::advance(Real newTime) {
  if (!std::isfinite(newTime) || !(newTime > time_)) {
    throw InvalidArgumentError(
        "MeshMotion::advance: the new time must be finite and later than the current time");
  }
  const Real dt = newTime - time_;
  const std::vector<Vector3> next = positionsAt(newTime);
  MeshGeometryState before = mesh_.geometry();

  MeshMotionStep step;
  step.previousTime = time_;
  step.time = newTime;
  step.dt = dt;
  step.previousVolumes = before.cellVolumes;
  step.moved = next != current_;
  const std::size_t faceCount = mesh_.numberOfFaces();
  const std::size_t cellCount = mesh_.numberOfCells();
  step.gclResiduals.assign(cellCount, 0.0);
  if (!step.moved) {
    step.sweptVolumes.assign(faceCount, 0.0);
    step.meshVolumeFlux.assign(faceCount, 0.0);
    step.vertexVelocities.assign(next.size(), Vector3{});
  } else {
    // Everything that can fail runs before the mesh is touched.
    MeshGeometryState after = MeshGeometry::computeGeometry(topology_, next);
    step.sweptVolumes = MeshGeometry::sweptVolumes(topology_, current_, next);
    mesh_.setGeometry(after);
    step.meshVolumeFlux.resize(faceCount);
    for (std::size_t f = 0; f < faceCount; ++f) step.meshVolumeFlux[f] = step.sweptVolumes[f] / dt;
    step.vertexVelocities.resize(next.size());
    for (std::size_t v = 0; v < next.size(); ++v) {
      step.vertexVelocities[v] = (next[v] - current_[v]) * (1.0 / dt);
    }
    // GCL: r_P = V^{n+1} - V^n - sum_f s_Pf dV_f.
    std::vector<Real> net(cellCount, 0.0);
    Real boundarySwept = 0.0;
    for (std::size_t f = 0; f < faceCount; ++f) {
      const Face& face = mesh_.face(f);
      net[face.owner()] += step.sweptVolumes[f];
      if (face.neighbor().has_value()) {
        net[*face.neighbor()] -= step.sweptVolumes[f];
      } else {
        boundarySwept += step.sweptVolumes[f];
      }
    }
    Real totalAfter = 0.0;
    Real totalBefore = 0.0;
    for (std::size_t c = 0; c < cellCount; ++c) {
      const Real volume = mesh_.cell(c).volume();
      step.gclResiduals[c] = (volume - before.cellVolumes[c]) - net[c];
      step.maxAbsGclResidual = std::max(step.maxAbsGclResidual, std::abs(step.gclResiduals[c]));
      step.maxRelativeGclResidual =
          std::max(step.maxRelativeGclResidual, std::abs(step.gclResiduals[c]) / volume);
      totalAfter += volume;
      totalBefore += before.cellVolumes[c];
    }
    step.globalGclResidual = (totalAfter - totalBefore) - boundarySwept;
  }

  // Summaries (both branches).
  step.minVolume = std::numeric_limits<Real>::infinity();
  step.maxVolume = 0.0;
  for (std::size_t c = 0; c < cellCount; ++c) {
    const Real volume = mesh_.cell(c).volume();
    step.minVolume = std::min(step.minVolume, volume);
    step.maxVolume = std::max(step.maxVolume, volume);
    step.totalVolume += volume;
    step.previousTotalVolume += before.cellVolumes[c];
  }
  for (std::size_t v = 0; v < next.size(); ++v) {
    step.maxDisplacement = std::max(step.maxDisplacement, magnitude(next[v] - reference_[v]));
    step.maxVertexSpeed = std::max(step.maxVertexSpeed, magnitude(step.vertexVelocities[v]));
    for (const Vector3* p : std::array<const Vector3*, 2>{&next[v], &current_[v]}) {
      step.maxCoordinate =
          std::max({step.maxCoordinate, std::abs(p->x), std::abs(p->y), std::abs(p->z)});
    }
  }

  canRevert_ = true;
  revertTime_ = time_;
  revertVertices_ = current_;
  if (step.moved) {
    revertGeometry_ = std::move(before);
  } else {
    revertGeometry_.reset();
  }
  current_ = next;
  time_ = newTime;
  lastStep_ = std::move(step);
  return lastStep_;
}

void MeshMotion::revert() {
  if (!canRevert_) return;
  if (revertGeometry_.has_value()) mesh_.setGeometry(*revertGeometry_);
  current_ = revertVertices_;
  time_ = revertTime_;
  canRevert_ = false;
  revertGeometry_.reset();
  lastStep_ = MeshMotionStep{};
}

bool MeshMotion::canRevert() const noexcept { return canRevert_; }
Real MeshMotion::time() const noexcept { return time_; }
Real MeshMotion::startTime() const noexcept { return startTime_; }
const Mesh& MeshMotion::mesh() const noexcept { return mesh_; }
const PrescribedMotion& MeshMotion::motion() const noexcept { return *motion_; }
const MeshGeometry::StructuredTopology& MeshMotion::topology() const noexcept { return topology_; }
const std::vector<Vector3>& MeshMotion::referenceVertices() const noexcept { return reference_; }
const std::vector<Vector3>& MeshMotion::currentVertices() const noexcept { return current_; }
const MeshMotionStep& MeshMotion::lastStep() const noexcept { return lastStep_; }

}  // namespace cfd::mesh
