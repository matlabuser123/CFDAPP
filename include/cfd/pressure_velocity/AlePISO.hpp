#pragma once

#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/solver/TransientSolver.hpp"

namespace cfd::pressure_velocity {

// P12-MESH-007 -- ALE PISO on a prescribed moving mesh
// (results/p12-mesh-007/architecture.md section 3.7).

// Boundary-motion consistency of one mesh-motion step. The physical wall
// velocity always comes from the velocity boundary condition -- never from
// the mesh motion -- but the fluid cannot cross a wall or a symmetry plane,
// so the boundary's NORMAL mesh velocity (MeshMotionStep::meshVolumeFlux /
// |S_f|, the normal velocity of the moving face) must match:
//   Wall                 -- 0 (a stationary wall may only slide along itself);
//   MovingWall(V_w)      -- V_w . n (any tangential difference is physical
//                           sliding and allowed);
//   Symmetry             -- 0;
//   Inlet, Outlet, other -- no constraint (fluid may cross them).
// Tolerance, per patch: 1e-8 (|V_w| + the step's maximum vertex speed).
// Returns one message per violating patch (its name, condition, worst face,
// the two normal velocities and the mismatch); empty if consistent.
// Patches without a velocity condition are skipped (the solver reports
// them). Throws InvalidArgumentError if step does not match mesh.
[[nodiscard]] std::vector<std::string> checkBoundaryMotion(
    const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::mesh::MeshMotionStep& step);

// Local and global ALE mass conservation of a step: for every cell,
//   R_P = sum_f s_Pf (F_f - rho phi_m,f) + rho (V_P^{n+1} - V_P^n) / dt
// (the flux sum in the order of evaluateContinuity; the volume term only
// when the mesh moved, so a static step gives exactly the continuity
// imbalance), and globally the boundary relative flux sum plus
// rho (sum V^{n+1} - sum V^n) / dt. `massFlux` is the step's final
// (absolute) flux on the mesh's current geometry.
struct AleConservation {
  std::vector<Real> cellMassResidual;
  Real maxCellMassResidual{0.0};
  Real boundaryRelativeFlux{0.0};
  Real globalMassResidual{0.0};
};
[[nodiscard]] AleConservation evaluateAleConservation(const cfd::mesh::Mesh& mesh,
                                                      const cfd::mesh::MeshMotionStep& step,
                                                      const cfd::fields::SurfaceField& massFlux,
                                                      Real density);

// One ALE PISO time step on the mesh moved by `motion`: the mesh is moved
// from motion.time() to motion.time() + dt (MeshMotion::advance), the
// boundary motion is checked (checkBoundaryMotion), then the PISO algorithm
// runs on the new geometry with the ALE terms -- V^n in the time derivative
// and the relative convecting flux F^n - rho dV/dt (cfd::physics::
// relativeMassFlux) in the momentum predictor and the CFL number. The
// pressure corrections act on the absolute flux, unchanged: with the discrete
// GCL satisfied, mass conservation of the moving cells is exactly continuity
// of the absolute flux (architecture.md section 3.6).
//
// Laminar only (a turbulence model's cached wall distance would go stale on
// a moving mesh). Two-dimensional only, like PISO.
//
// Outcome:
//   - Converged: the mesh stays at the new time; result.state is {u, p, F}
//     on the new geometry;
//   - any other status: the mesh is reverted to motion.time() before the
//     call (bit for bit) and the status returned (same statuses as PISO);
//   - an invalid cell produced by the motion, or a boundary motion
//     inconsistent with the velocity conditions: InvalidArgumentError (the
//     message names the cell / patch), the mesh unchanged -- a configuration
//     error, not a numerical failure.
// onStepRejected() (called by TransientSolver when it rejects a Converged
// step, e.g. for CFL) reverts the mesh as well.
class AlePISO final : public cfd::solver::TransientStepSolver {
 public:
  // motion, fluid and the two condition sets are not owned and must outlive
  // this object; the mesh is the one `motion` moves.
  AlePISO(cfd::mesh::MeshMotion& motion, const cfd::physics::FluidProperties& fluid,
          const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
          const cfd::boundary::BoundaryConditionSet& pressureBoundaries, PISOSettings settings,
          Index referenceCell = 0);

  [[nodiscard]] cfd::solver::TransientStepResult solveTimeStep(
      const cfd::solver::TransientState& previousState, Real dt) const override;
  void onStepRejected() const override;

  [[nodiscard]] const PISOSettings& settings() const noexcept;
  [[nodiscard]] Index referenceCell() const noexcept;
  [[nodiscard]] const cfd::mesh::MeshMotion& motion() const noexcept;

 private:
  cfd::mesh::MeshMotion& motion_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  const cfd::boundary::BoundaryConditionSet& pressureBoundaries_;
  PISOSettings settings_;
  Index referenceCell_;
};

}  // namespace cfd::pressure_velocity
