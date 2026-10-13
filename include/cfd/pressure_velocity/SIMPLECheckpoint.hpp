#pragma once

// P13-001 -- Restart / checkpointing: the production checkpoint of a steady
// SIMPLE solve, plus the full definition of what makes one valid.
//
// Deliberately contains no file I/O. The layering is the one the P2 restart
// lineage already established and this file keeps:
//
//     SIMPLEResult  ->  SIMPLECheckpoint (this file)  ->  cfd::io::Checkpoint{Writer,Reader}
//        solver               solver-state model                   disk format
//
// so JSON never reaches the solver and device memory never reaches the format.
//
// RELATIONSHIP TO cfd::solver::RestartSnapshot (kRestartFormatVersion = 1)
//
// That type is NOT superseded and NOT changed. It is the restart of a
// TRANSIENT PISO time step: it is parameterised by physical time, step index
// and delta_t, it is 2D-only by explicit refusal, and it carries no
// configuration identity. A steady SIMPLE solve has no physical time, runs in
// 2D and 3D, and needs to know whether the case it is being resumed into is
// still the same case. This is a separate artifact with its own version
// number, so neither format constrains the other.
//
// WHAT IS STORED, AND WHAT IS NOT
//
// Stored: the fields that are loop-carried state (velocity, pressure and the
// authoritative corrected face mass flux), the outer residual sample history
// the convergence monitor consumes, and enough identity to refuse an
// incompatible resume.
//
// NOT stored, because none of it is solver state:
//   - device pointers, CUDA handles, streams, allocator state -- all of it is
//     rebuilt from host state at the top of a solve;
//   - the Krylov workspace and any linear-solver iterate -- p' is reset to
//     zero at the top of every outer iteration, and every momentum solve is
//     warm-started from the velocity field, which IS stored;
//   - the convergence monitor's internal trackers -- reconstructed exactly by
//     replaying `history` (see SIMPLEContinuation.hpp);
//   - stage timings.
//
// WHAT THIS FORMAT CANNOT REPRESENT, stated rather than worked around
//
// A transport-equation turbulence model (k-epsilon, k-omega, SST) carries k,
// epsilon/omega and mu_t across outer iterations, and all three models can
// only be constructed from uniform scalar initial values -- there is no API
// that could restore a checkpointed field. A checkpoint of such a run would
// silently resume from the wrong turbulence state. So the production writer
// REFUSES to write one and the reader refuses to resume one, naming the
// model. Recorded as technical debt, not hidden.

#include <cstdint>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEContinuation.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::pressure_velocity {

// Independent of cfd::solver::kRestartFormatVersion by design (see above).
constexpr std::uint32_t kSIMPLECheckpointVersion = 1;

struct SIMPLECheckpoint {
  std::uint32_t formatVersion{kSIMPLECheckpointVersion};

  // Informational, never a compatibility condition: a checkpoint written by
  // one build and resumed by another is a normal thing to want, and the
  // fingerprints below are what actually decide compatibility.
  std::string applicationVersion;
  // "CPU" or "GPU" -- which backend produced this. Also informational: the
  // checkpoint holds no device-specific data and CPU/GPU equivalence is
  // already qualified, so "the GPU node died, finish it on CPU" is allowed.
  // A mismatch is reported by the caller, never rejected here.
  std::string backend;

  // Identity. All four must match on resume.
  std::string caseFingerprint;  // cfd::io::computeCaseFingerprint
  std::string meshFingerprint;  // cfd::mesh::computeMeshFingerprint
  Index cellCount{0};
  Index faceCount{0};
  int dimension{2};

  // Solver state.
  Index completedIterations{0};
  // The SIMPLEStatus name the segment ended with -- informational, so a
  // human reading the file can see why it stopped.
  std::string status;
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::SurfaceField massFlux;
  std::vector<SIMPLEIterationRecord> history;
};

// The single definition of "valid checkpoint", used by the writer on the way
// out and by the reader on the way in, so the two can never drift.
//
// Throws InvalidArgumentError, naming the offending thing, if:
//   - formatVersion != kSIMPLECheckpointVersion;
//   - completedIterations == 0 (a checkpoint of a solve that never ran an
//     iteration carries no state worth resuming, and would be indistinguishable
//     from a fresh start);
//   - velocity.size() or pressure.size() != cellCount, or massFlux.size() !=
//     faceCount;
//   - history.size() != completedIterations;
//   - any field or history value is non-finite;
//   - dimension is neither 2 nor 3, or disagrees with whether the history
//     carries w residuals.
//
// Throws InvalidArgumentError naming the mismatch if `mesh` is given and its
// cell count, face count, dimension or fingerprint differs.
void validateSIMPLECheckpoint(const SIMPLECheckpoint& checkpoint);
void validateSIMPLECheckpoint(const SIMPLECheckpoint& checkpoint, const cfd::mesh::Mesh& mesh);

// Builds a checkpoint from a finished segment. `mesh` supplies the identity;
// `caseFingerprint` and `backend` come from the caller, which is the only
// layer that knows about case directories and backends.
//
// Throws InvalidArgumentError under exactly the conditions
// validateSIMPLECheckpoint documents -- never returns a checkpoint that would
// itself fail validation.
[[nodiscard]] SIMPLECheckpoint makeSIMPLECheckpoint(const SIMPLEResult& result,
                                                    const cfd::mesh::Mesh& mesh,
                                                    std::string caseFingerprint,
                                                    std::string backend,
                                                    std::string applicationVersion);

// The inverse: the continuation that resumes from this checkpoint.
// `iterationBudget` is passed through to SIMPLEContinuation.
[[nodiscard]] SIMPLEContinuation makeContinuationFromCheckpoint(const SIMPLECheckpoint& checkpoint,
                                                                Index iterationBudget = 0);

}  // namespace cfd::pressure_velocity
