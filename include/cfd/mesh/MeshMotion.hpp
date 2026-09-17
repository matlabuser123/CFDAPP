#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector3.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::mesh {

// P12-MESH-007 -- topology-preserving mesh motion (results/p12-mesh-007/
// architecture.md sections 3.4-3.5).
//
// A prescribed vertex motion: the position, at elapsed time tau = t - t0, of
// the vertex whose reference position is X. Every motion must leave every
// vertex at its reference position at tau = 0 (MeshMotion checks this).
class PrescribedMotion {
 public:
  virtual ~PrescribedMotion() = default;
  [[nodiscard]] virtual Vector3 position(const Vector3& reference, Real elapsed) const = 0;
  [[nodiscard]] virtual std::string description() const = 0;
};

// x = X at every time.
class StationaryMotion final : public PrescribedMotion {
 public:
  [[nodiscard]] Vector3 position(const Vector3& reference, Real elapsed) const override;
  [[nodiscard]] std::string description() const override;
};

// x = X + tau (G (X - c) + b): a translation (G = 0), a uniform expansion or
// contraction (G = alpha I), a shear (G strictly triangular), an axial
// compression (a piston, G = diag(-V/L, 0, 0)), ... Each vertex moves along a
// straight line at constant velocity G (X - c) + b. Throws
// InvalidArgumentError for a non-finite entry.
class AffineMotion final : public PrescribedMotion {
 public:
  using Matrix = std::array<std::array<Real, 3>, 3>;
  AffineMotion(const Matrix& rate, const Vector3& center, const Vector3& velocity);

  [[nodiscard]] Vector3 position(const Vector3& reference, Real elapsed) const override;
  [[nodiscard]] std::string description() const override;

  // G (X - c) + b: the (constant) velocity of the vertex X.
  [[nodiscard]] Vector3 velocity(const Vector3& reference) const noexcept;

 private:
  Matrix rate_;
  Vector3 center_;
  Vector3 velocity_;
};

// x = X + sin(omega tau) prod_d sin(pi (X_d - lower_d) / (upper_d - lower_d)) A,
// the product over the axes with upper_d > lower_d (two for a 2D box with
// lower.z = upper.z). Exactly zero on and outside the box boundary, so a mesh
// filling the box keeps its boundary fixed while its interior deforms.
// Throws InvalidArgumentError for a non-finite argument or no axis with
// upper_d > lower_d.
class SinusoidalMotion final : public PrescribedMotion {
 public:
  SinusoidalMotion(const Vector3& lower, const Vector3& upper, const Vector3& amplitude,
                   Real angularFrequency);

  [[nodiscard]] Vector3 position(const Vector3& reference, Real elapsed) const override;
  [[nodiscard]] std::string description() const override;

  // prod_d sin(pi (X_d - lower_d) / (upper_d - lower_d)) (0 on and outside the
  // box boundary): the displacement is sin(omega tau) * shape * amplitude.
  [[nodiscard]] Real shape(const Vector3& reference) const noexcept;

 private:
  Vector3 lower_;
  Vector3 upper_;
  Vector3 amplitude_;
  Real angularFrequency_;
};

// Everything one MeshMotion::advance step produced. Time levels: n = before
// the step (previousTime), n+1 = after (time). Within the step each vertex
// moves linearly in time from x^n to x^{n+1} (the discrete path; the swept
// volumes, the mesh velocity and the GCL all refer to it).
struct MeshMotionStep {
  Real previousTime{0.0};
  Real time{0.0};
  Real dt{0.0};
  // False when every vertex stayed exactly where it was: then no geometry
  // was recomputed, V^{n+1} == V^n bit for bit, and every swept volume,
  // mesh flux and vertex velocity below is exactly 0.0.
  bool moved{false};
  // V^n, one per cell (the volumes before the step).
  std::vector<Real> previousVolumes;
  // dV_f, one per face: the signed volume the face swept during the step,
  // positive when it moved along its area vector (MeshGeometry::sweptVolumes).
  std::vector<Real> sweptVolumes;
  // phi_m,f = dV_f / dt: the volumetric mesh flux (u_mesh . S_f averaged over
  // the step), one per face.
  std::vector<Real> meshVolumeFlux;
  // (x^{n+1} - x^n) / dt, one per (welded) vertex of MeshMotion::topology().
  std::vector<Vector3> vertexVelocities;
  // Discrete GCL residual r_P = V_P^{n+1} - V_P^n - sum_f s_Pf dV_f, one per
  // cell (s_Pf = +1 owner, -1 neighbor), and summaries: max |r_P|,
  // max |r_P| / V_P^{n+1}, and the global residual sum V^{n+1} - sum V^n -
  // sum_{boundary} dV_b.
  std::vector<Real> gclResiduals;
  Real maxAbsGclResidual{0.0};
  Real maxRelativeGclResidual{0.0};
  Real globalGclResidual{0.0};
  Real minVolume{0.0};
  Real maxVolume{0.0};
  Real totalVolume{0.0};
  Real previousTotalVolume{0.0};
  // max |x^{n+1} - X| over vertices (displacement from the reference) and
  // max |(x^{n+1} - x^n) / dt|.
  Real maxDisplacement{0.0};
  Real maxVertexSpeed{0.0};
  // The largest absolute vertex coordinate at levels n and n+1 (X of the
  // round-off bounds in results/p12-mesh-007/acceptance_gate.md).
  Real maxCoordinate{0.0};
};

// Moves a caller-owned Mesh by a prescribed motion, one time step at a time,
// updating its geometry in place (Mesh::setGeometry) -- never its topology.
// The Mesh must outlive this object; solvers keep reading the same Mesh and
// see its current geometry. Owns the reference vertex positions (the mesh's
// vertices at construction = the configuration at startTime), the current
// and previous vertices, and the previous geometry (for revert()).
class MeshMotion {
 public:
  // Throws InvalidArgumentError if the mesh has no structured grid
  // (MeshGeometry::structuredTopology), `motion` is null, startTime is not
  // finite, or the motion does not leave every vertex at its reference
  // position at startTime (or moves a 2D mesh out of the xy-plane).
  MeshMotion(Mesh& mesh, std::shared_ptr<const PrescribedMotion> motion, Real startTime = 0.0);

  // Moves every vertex to its prescribed position at `newTime` (> time()),
  // recomputes the geometry (MeshGeometry::computeGeometry), the swept
  // volumes and the GCL residuals, and commits the new geometry to the mesh.
  // Strong guarantee: if a cell would be invalid (the message names it and
  // its defect), a vertex leaves the xy-plane of a 2D mesh, or newTime is
  // not later than time(), InvalidArgumentError is thrown and nothing -- the
  // mesh, the vertices, the time -- changes.
  const MeshMotionStep& advance(Real newTime);

  // Undoes the last advance(): restores the mesh geometry (bit for bit), the
  // vertices and the time of before it. Idempotent: does nothing if there is
  // no step to undo (none yet, or already reverted).
  void revert();
  [[nodiscard]] bool canRevert() const noexcept;

  [[nodiscard]] Real time() const noexcept;
  [[nodiscard]] Real startTime() const noexcept;
  [[nodiscard]] const Mesh& mesh() const noexcept;
  [[nodiscard]] const PrescribedMotion& motion() const noexcept;
  [[nodiscard]] const MeshGeometry::StructuredTopology& topology() const noexcept;
  [[nodiscard]] const std::vector<Vector3>& referenceVertices() const noexcept;
  [[nodiscard]] const std::vector<Vector3>& currentVertices() const noexcept;
  // The last (not reverted) step; a default MeshMotionStep before the first.
  [[nodiscard]] const MeshMotionStep& lastStep() const noexcept;

 private:
  std::vector<Vector3> positionsAt(Real time) const;

  Mesh& mesh_;
  std::shared_ptr<const PrescribedMotion> motion_;
  MeshGeometry::StructuredTopology topology_;
  Real startTime_;
  Real time_;
  std::vector<Vector3> reference_;
  std::vector<Vector3> current_;
  bool canRevert_{false};
  Real revertTime_{0.0};
  std::vector<Vector3> revertVertices_;
  std::optional<MeshGeometryState> revertGeometry_;
  MeshMotionStep lastStep_;
};

}  // namespace cfd::mesh
