#pragma once

#include <cstdint>
#include <string>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/solver/TransientSolver.hpp"

namespace cfd::solver {

// Restart-A (TODO.md P2 -- Restart capability): the data model + full
// validation for what a restart must represent -- "a complete accepted
// transient state from which the next time step can continue without
// reconstructing numerical state approximately." Deliberately contains
// no file I/O: TransientState -> RestartSnapshot (this file) ->
// RestartIO -> disk format, kept as separate layers on purpose so JSON/
// binary/file concerns never leak into the solver-state model
// (Restart-C/D own the disk format).
//
// The saved face flux is F -- the *authoritative*, already-corrected
// SurfaceField a real PISO time step produces (TransientState.massFlux),
// never something a restart reconstructs by reinterpolating U after
// loading. This project's entire pressure-velocity architecture treats
// the corrected SurfaceField, not U, as the conservative state (PISO-E
// through PISO-G's own "never regenerate F by interpolating corrected
// cell velocities" invariant) -- a restart that only saved U/p and
// rebuilt F afterward would silently reintroduce exactly the
// inconsistency PISO was built to avoid.
constexpr std::uint32_t kRestartFormatVersion = 1;

struct RestartSnapshot {
  std::uint32_t formatVersion = kRestartFormatVersion;

  // Physical time and step index of the accepted state this snapshot
  // represents -- TimeController's own `time()`/`step()` immediately
  // after the accepting `advance()` call, not the ones on offer before
  // accepting the next step.
  Real time = 0.0;
  Index step = 0;

  // The dt that was used to advance *into* this state (previous accepted
  // state -> this one) -- matches TimeStepRecord.deltaT's own,
  // already-established convention (TransientSolver.cpp: `dt` is read
  // via `timeController.deltaT()` *before* `advance()`, i.e. "the delta
  // that produced this step", not "the delta the next call would
  // apply"). Deliberately NOT the nominal/next-step dt a resumed run
  // would separately configure -- those two coincide for every step
  // except a shortened final one, which is exactly the case worth being
  // unambiguous about now, before adaptive time stepping makes the
  // distinction unavoidable.
  Real deltaT = 0.0;

  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::SurfaceField massFlux;  // F -- the authoritative corrected flux.

  // Enough information to reject a restart belonging to a different
  // mesh. cellCount/faceCount alone are a necessary but not sufficient
  // check (two different meshes can share both counts); meshFingerprint
  // (Restart-B: cfd::mesh::computeMeshFingerprint, see
  // MeshFingerprint.hpp) is a deterministic hash of the numerical
  // topology/geometry that distinguishes those cases too -- populated by
  // makeRestartSnapshot and checked (exact match) by
  // validateRestartSnapshot below.
  Index cellCount = 0;
  Index faceCount = 0;
  std::string meshFingerprint;
};

// Builds a RestartSnapshot from an accepted TransientState, stamping the
// current format version and this file's documented deltaT semantics,
// then runs it through validateRestartSnapshot before returning it --
// never returns a snapshot that would itself fail validation. `time` and
// `step` are supplied by the caller (a TransientState alone carries no
// physical-time or step-index information of its own -- that is
// TimeController's responsibility, per this project's own established
// "do not let PISO independently manage physical time" separation).
// Throws InvalidArgumentError under the same conditions
// validateRestartSnapshot documents (time or deltaTUsedToReachThisState
// not finite, deltaT <= 0; state's field sizes not matching mesh; any
// non-finite value in state).
[[nodiscard]] RestartSnapshot makeRestartSnapshot(const cfd::mesh::Mesh& mesh,
                                                  const TransientState& state, Real time,
                                                  Real deltaTUsedToReachThisState, Index step);

// The single validation entry point both makeRestartSnapshot (on
// construction) and a future reader (Restart-D, loading a snapshot
// whose fields were never guaranteed valid to begin with) use -- so
// "what makes a restart valid" is defined exactly once. Throws
// InvalidArgumentError if:
//   - formatVersion != kRestartFormatVersion (unsupported version)
//   - time is not finite (negative time is NOT rejected -- TimeController
//     itself does not forbid a negative startTime, so this file does not
//     invent a stricter contract than the class that actually owns
//     physical time)
//   - deltaT is not finite, or deltaT <= 0
//   - velocity.size() or pressure.size() != cellCount
//   - massFlux.size() != faceCount
//   - any value in velocity, pressure, or massFlux is non-finite
//   - cellCount != mesh.numberOfCells() or faceCount != mesh.numberOfFaces()
//   - meshFingerprint != cfd::mesh::computeMeshFingerprint(mesh) (catches
//     a different mesh that happens to share both counts -- Restart-B)
// step has no invalid values of its own: Index is unsigned, so there is
// no negative-step or non-finite-step case to reject beyond what the
// type system already rules out.
void validateRestartSnapshot(const RestartSnapshot& snapshot, const cfd::mesh::Mesh& mesh);

}  // namespace cfd::solver
