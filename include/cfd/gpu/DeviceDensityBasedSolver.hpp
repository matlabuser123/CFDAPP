#pragma once

// P17.10 -- the density-based compressible solver on device. CUDA-only; see DeviceBuffer.hpp's
// "never include from a CPU-only-compiled file" rule, which this header inherits.
//
// WHAT THIS IS FOR. P17.8 built the CPU solver and recorded, in its own header, that "there is no
// GPU density-based solver in P17.8 ... the residual assembly, the CFL reduction and the update are
// not [qualified]". This closes that, and the target is not "close" -- it is BITWISE, for the
// reasons tolerance_policy.md gives and kernel_inventory.md details.
//
// THE DESIGN DECISION THAT MAKES BITWISE POSSIBLE, in one sentence: face ids ascend by
// construction, so each cell's face list ascends, so a per-cell GATHER adds exactly the terms the
// CPU's ascending face loop adds, in exactly that order -- with NO ATOMICS, which is separately
// what makes the GPU residual reproducible run to run. Measured on five mesh construction paths
// before any kernel was written (acceptance_gate.md §0, D1: 702 cell-face references, all
// ascending).
//
// THE ENTRY POINTS ARE FINE-GRAINED ON PURPOSE. recoverPrimitives / assembleResidual /
// localTimeSteps / globalTimeStep are each separately comparable against their CPU counterparts,
// so when something differs the evidence says WHICH STAGE differs rather than "the answers moved".
// advance() is the opposite: it runs N steps with no transfer in between, which is what §24's
// residency requirement is about.

#include <memory>
#include <string>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceThermodynamics.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::gpu {

// Counted, not estimated. §23 asks what crosses the bus and how often, and a transfer counter that
// reads 0 on a path that transfers is a fail-open probe -- so the gate asserts a POSITIVE expected
// count where transfers are expected and 0 only where 0 is the actual claim.
struct DeviceSolveStats {
  cfd::Index hostToDeviceTransfers{};
  cfd::Index deviceToHostTransfers{};
  cfd::Index hostToDeviceBytes{};
  cfd::Index deviceToHostBytes{};
  cfd::Index kernelLaunches{};
};

// true when a usable CUDA device is present. Every entry point returns false with a message when
// not, so a CUDA-enabled build on a machine without a GPU produces a SKIPPED test rather than a
// crash or, worse, a silently empty comparison.
[[nodiscard]] bool densityBasedDeviceAvailable();

// --- the sanitizer controls, and why they exist -------------------------------------------------
//
// A clean compute-sanitizer log names no kernels, because kernels are only named when something is
// wrong. So "0 errors" and "the tool never instrumented our code" produce IDENTICAL output, and a
// gate that reads only the error count is fail-open -- the exact failure mode this project has a
// standing finding about.
//
// These two entry points are the live control. Each launches a kernel IN THIS LIBRARY'S TRANSLATION
// UNIT that commits one specific, deliberate fault, so the corresponding tool must report it by
// name. If the tool stays silent on these, its silence on the real kernels means nothing.
//
// They are NOT part of any production path: no solver entry point calls them, and they touch only
// their own scratch allocation. They are deliberately separate kernels rather than a flag inside a
// real one, so no production kernel carries a fault path at all.
[[nodiscard]] bool sanitizerControlOutOfBoundsRead(std::string* message);   // memcheck must catch
[[nodiscard]] bool sanitizerControlSharedMemoryRace(std::string* message);  // racecheck must catch

class DeviceDensityBasedSolver {
 public:
  DeviceDensityBasedSolver();
  ~DeviceDensityBasedSolver();
  DeviceDensityBasedSolver(const DeviceDensityBasedSolver&) = delete;
  DeviceDensityBasedSolver& operator=(const DeviceDensityBasedSolver&) = delete;

  // Mirrors the mesh, the thermodynamic scalars and the per-face boundary treatment onto the
  // device. Returns false with a message on any CUDA failure.
  //
  // REFUSES, exactly as the CPU does, when a boundary patch has no treatment or a boundary face
  // belongs to no patch. An unmapped patch silently treated as a wall is precisely the kind of
  // default that makes a wrong answer look plausible, and the equivalence suite drives BOTH the CPU
  // solver and this upload with an unmapped patch to prove the two refuse together.
  bool upload(const cfd::mesh::Mesh& mesh, const cfd::compressible::TotalEnergyState& thermo,
              const cfd::compressible::DensityBasedBoundaries& boundaries, cfd::Real cfl,
              std::string* message);

  // All false is production. Each flag re-introduces one named trap so a negative control is a
  // FLAG, not a source edit -- the control and the production path are the same binary.
  void setControls(const DeviceControls& controls);
  [[nodiscard]] const DeviceControls& controls() const;

  // Block size for every kernel launch. Exposed ONLY so R5 can show the result does not depend on
  // it: an answer that changes with the launch configuration is not deterministic.
  void setBlockSize(int threads);
  [[nodiscard]] int blockSize() const;

  // --- the transfer pair, which §23's T2 requires to be exactly inverse -------------------------
  bool setState(const cfd::compressible::ConservedField& q, std::string* message);
  bool getState(cfd::compressible::ConservedField* q, std::string* message) const;

  // --- Level A/B/C, each comparable against its CPU counterpart in isolation --------------------

  // K1. `statuses` receives one DeviceRecoveryStatus per cell as an int, so the equivalence suite
  // can compare the REASON a state was refused and not merely the fact of refusal.
  bool recoverPrimitives(std::vector<cfd::compressible::PrimitiveState>* primitives,
                         std::vector<int>* statuses, std::string* message);

  // K2a + K2b. The residual from the CURRENT device state.
  bool assembleResidual(cfd::compressible::ResidualField* residual, std::string* message);

  // K2a alone -- the per-face flux, before any accumulation. Present because B1 and B2 fail for
  // different reasons and an evidence trail that cannot separate them is worth less.
  bool faceFluxes(std::vector<cfd::compressible::FluxVector>* fluxes, std::string* message);

  // K3, K4.
  bool localTimeSteps(std::vector<cfd::Real>* dt, std::string* message);
  bool globalTimeStep(cfd::Real* dt, std::string* message);

  // K5 + K6: one complete step from the current device state. `dtUsed` receives the global dt.
  // Returns true and sets `admissible` false if the candidate state is inadmissible -- a refusal
  // is a result, not an error, and `offendingCell` then names the FIRST offender in ascending id
  // order, which is the same convention the CPU reports.
  bool step(cfd::Real* dtUsed, bool* admissible, cfd::Index* offendingCell, int* status,
            std::string* message);

  // N steps with NO host transfer in between. The evidence for §24-§25: only the per-step dt comes
  // back, and that only because the history is worth having.
  bool advance(cfd::Index steps, std::vector<cfd::Real>* dtPerStep, cfd::Index* stepsTaken,
               std::string* message);

  [[nodiscard]] const DeviceSolveStats& stats() const;
  void resetStats();

  // Bytes resident for the solution state, excluding the mesh (DeviceMesh reports its own).
  [[nodiscard]] std::size_t residentBytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cfd::gpu
