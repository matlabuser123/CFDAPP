#pragma once

// P13-001 -- Restart / checkpointing.
//
// Everything `SIMPLE::solve()` needs to run ONE SEGMENT of a longer steady
// solve: what already happened before this call, and how far this call may go.
// Deliberately contains no file I/O and no format concern -- the same layering
// the P2 restart lineage already established (solver-state model ->
// cfd::io::CheckpointWriter/CheckpointReader -> disk format), so JSON never
// leaks into the solver and device memory never leaks into the format.
//
// WHY THIS TYPE HAS TO EXIST
//
// `SIMPLE::solve()` takes an initial velocity and pressure and then does
//
//     SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, ...);
//
// i.e. it regenerates the face mass flux by interpolating the cell velocity.
// F is loop-carried state: the next iteration's momentum assembly reads it.
// A "restart" that handed back only U and p would therefore reinterpolate F
// and continue down a different iteration path -- exactly the inconsistency
// this project already refused for PISO:
//
//   "a restart that only saved U/p and rebuilt F afterward would silently
//    reintroduce exactly the inconsistency PISO was built to avoid"
//                                      -- cfd::solver::RestartSnapshot.hpp
//
// So the authoritative corrected F travels in the continuation, and
// solve() accepts it instead of regenerating it.
//
// WHY THE RESIDUAL HISTORIES TRAVEL TOO, AND WHY NO MONITOR STATE DOES
//
// cfd::solver::OuterIterationMonitor holds the normalization references, the
// stagnation/divergence windows and the adaptive-relaxation controller. A
// resumed solve that started those from scratch would judge convergence
// against a different reference and relax differently. None of it is
// serialized, because none of it needs to be: `OuterIterationMonitor::record`
// is a pure function of the sequence of OuterResidualSamples, and
// AdaptiveRelaxationController says so itself ("the factors depend only on the
// sequence of measures"). solve() replays the recorded samples into a fresh
// monitor and it arrives in exactly the state it had. What is stored is solver
// state; what is reconstructed is implementation detail.
//
// The sample is wider than SIMPLEResult's own public histories:
// globalImbalance and the optional turbulence residual both feed the
// convergence distance and the convergence test, so both are carried.

#include <optional>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::pressure_velocity {

// One completed outer iteration as the monitor saw it -- exactly
// cfd::solver::OuterResidualSample's own fields, held here without depending
// on that header's layout.
struct SIMPLEIterationRecord {
  Real u{};
  Real v{};
  Real pressure{};
  Real continuity{};
  Real globalImbalance{};
  std::optional<Real> w;           // 3D solves only.
  std::optional<Real> turbulence;  // only when the active model reports one.
};

// A default-constructed value means "a fresh solve, run the whole configured
// budget" -- which is precisely what every pre-P13 caller gets, since
// SIMPLE holds a null continuation pointer by default.
struct SIMPLEContinuation {
  // Outer iterations already completed by earlier calls. 0 = a fresh start,
  // in which case `massFlux` and `history` must both be empty.
  Index completedIterations{0};

  // F -- the authoritative corrected face mass flux as the previous segment
  // left it. Size must equal mesh.numberOfFaces(). Empty iff
  // completedIterations == 0.
  cfd::fields::SurfaceField massFlux;

  // One entry per already-completed outer iteration, oldest first. Size must
  // equal completedIterations.
  std::vector<SIMPLEIterationRecord> history;

  // The most this call may run, on top of `completedIterations`. 0 means "no
  // extra limit": run until the configured maxIterations, convergence or a
  // failure, exactly as an unsegmented solve would.
  //
  // A segment that stops because it exhausted THIS budget rather than the
  // configured one is reported as SIMPLEStatus::MaxIterations with
  // `SIMPLEResult::iterations < SIMPLESettings::maxIterations`. That pair is
  // unambiguous -- a genuine MaxIterations always has iterations ==
  // maxIterations -- so no new status value was needed for it.
  Index iterationBudget{0};
};

// Throws InvalidArgumentError, naming the offending field, if:
//   - completedIterations == 0 but massFlux or history is non-empty;
//   - completedIterations > 0 but massFlux.size() != mesh.numberOfFaces();
//   - history.size() != completedIterations;
//   - any stored value is non-finite;
//   - a record carries w on a 2D mesh, or omits it on a 3D mesh
//     (`threeDimensional` is the caller's resolved mesh dimension, because a
//     continuation is validated against the mesh it will be resumed on).
//
// SIMPLE::solve() calls this itself and converts a throw into
// SIMPLEStatus::InvalidConfiguration, the same way it already treats every
// other configuration problem -- a caller never sees an exception escape
// solve().
void validateSIMPLEContinuation(const SIMPLEContinuation& continuation, const cfd::mesh::Mesh& mesh,
                                bool threeDimensional);

// The one place a finished segment becomes the next segment's starting point,
// so no caller has to know which of SIMPLEResult's histories make up an
// OuterResidualSample. `iterationBudget` is passed straight through.
//
// Throws InvalidArgumentError if `result` is not internally consistent -- a
// history whose length is not `result.iterations`, or a turbulence history
// that is neither empty nor exactly `result.iterations` long. That is a
// programming error in the caller or a corrupted result, never a normal
// outcome, and it must not become a silently truncated continuation.
[[nodiscard]] SIMPLEContinuation makeSIMPLEContinuation(const SIMPLEResult& result,
                                                        Index iterationBudget = 0);

}  // namespace cfd::pressure_velocity
