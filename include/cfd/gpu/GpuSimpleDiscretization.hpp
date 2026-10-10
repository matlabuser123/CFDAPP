#pragma once

// GPU-DISC-001M -- the production entry point for running SIMPLE's
// DISCRETIZATION on the device.
//
// Before this, a "GPU solve" meant two linear solves on the device with every
// operator that builds those systems running on the host. This class is what
// lets SIMPLE::solve run the operators themselves on the device, reusing --
// never duplicating -- the implementations already qualified bitwise by
// GPU-DISC-001B..001K and composed by 001L.
//
// Structured exactly like GpuResidencyManager and GPUBackend, which solve the
// same problem: cfdcore must be able to CALL this, but only cfdcuda can
// IMPLEMENT it.
//
//   this header          CPU-includable facade, PIMPL, every method a safe
//                        no-op when inactive
//   src/gpu/...          the stub, compiled ONLY when NOT CFDAPP_ENABLE_CUDA
//   cuda/kernels/...Cuda.cpp   the real implementation, part of cfdcuda
//
// ALL OR NOTHING. `prepare()` builds every device plan up front and returns
// false with a reason if ANY operator cannot reproduce the configuration. The
// caller then runs the whole iteration on the CPU. A partially-GPU iteration is
// never run: every one of these operators was qualified as BITWISE equal, and
// silently substituting one CPU stage into a GPU chain would produce a path no
// gate has verified.
//
// The method order below is SIMPLE's own stage order (see
// results/gpu-disc-001/single-iteration/audit.md section 2). The two linear
// solves stay on the host because LinearSolver is a host interface taking a
// host LinearSystem -- so the assembled systems come back and the solutions go
// out. Those transfers are forced by the architecture, are measured, and are
// reported rather than hidden; removing them is GPU-PIPE-001's gate, not this
// one.

#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}

namespace cfd::gpu {

// Defined only in the CUDA-only GpuSimpleResidentTurbulence.hpp; see the friend
// declaration at the bottom of GpuSimpleDiscretization for why it is opaque
// here.
struct ResidentTurbulenceInputs;
struct ResidentTurbulentViscosity;

class GpuSimpleDiscretization {
 public:
  GpuSimpleDiscretization();
  ~GpuSimpleDiscretization();

  GpuSimpleDiscretization(const GpuSimpleDiscretization&) = delete;
  GpuSimpleDiscretization& operator=(const GpuSimpleDiscretization&) = delete;
  GpuSimpleDiscretization(GpuSimpleDiscretization&&) noexcept;
  GpuSimpleDiscretization& operator=(GpuSimpleDiscretization&&) noexcept;

  // True iff this binary was built with CUDA AND a usable device was available
  // at construction -- mirrors cfd::gpu::cudaAvailable(). Every method below is
  // a safe no-op when this is false.
  [[nodiscard]] bool active() const noexcept;

  // Builds every device plan for this mesh and these condition sets. False (and
  // `reason` populated) if any operator cannot reproduce the configuration --
  // never approximates. Safe to call again for a different mesh.
  [[nodiscard]] bool prepare(const cfd::mesh::Mesh& mesh,
                             const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                             const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
                             std::string& reason);

  // --- persistent field residency (GPU-PIPE-001) -------------------------
  //
  // Before this, every outer iteration re-uploaded velocity, pressure, massFlux
  // and viscosity -- six full-field H2D copies of bytes the DEVICE had produced
  // itself one iteration earlier and the host had stored unchanged. The
  // residency audit measured that as 6 of the 21 H2D calls per iteration and
  // 61.6% of the field-path H2D bytes.
  //
  // Authority is now explicit. There is no implicit dual-authoritative state
  // and no accessor that silently returns a stale host copy.
  enum class FieldAuthority {
    HostOnly,      // no device copy, or a known-stale one
    DeviceOwned,   // the device copy is authoritative; any host copy is stale
    Synchronized,  // both sides hold the same bytes
  };

  // The persistent fields whose authority is tracked.
  enum class PersistentField { Velocity, Pressure, MassFlux, Viscosity };

  [[nodiscard]] FieldAuthority authority(PersistentField field) const noexcept;

  // One-time upload of the initial host state. HostOnly -> Synchronized for all
  // four fields. Called once per solve; calling it again is legal and simply
  // re-establishes the host state as authoritative (used by the lifecycle
  // tests, and by a caller that legitimately restarts a solve).
  void uploadInitialState(const cfd::fields::VectorField& velocity,
                          const cfd::fields::ScalarField& pressure,
                          const cfd::fields::SurfaceField& massFlux,
                          const cfd::fields::ScalarField& effectiveViscosity);

  // Start an outer iteration with NO transfer: the device carries its own
  // corrected velocity and flux forward, and the pressure it updated itself.
  // Requires uploadInitialState() to have run and the previous iteration to
  // have completed its corrections.
  void beginIterationResident();

  // The effective viscosity, re-uploaded ONLY when the turbulence model
  // actually changed it. LaminarModel returns a constant field for the whole
  // solve, so on a laminar case this transfers once and never again.
  void setViscosity(const cfd::fields::ScalarField& effectiveViscosity);

  // P16 Phase 2 -- the wall function's FACE-LOCAL viscosity override.
  //
  // `faceIds` and `values` are COMPACT: one entry per wall face, so the
  // transfer is O(N_wall) and never a face-length field. They are scattered
  // into a device buffer that is zero everywhere else, which reproduces the
  // host SurfaceField's "zero means no override" convention exactly.
  //
  // It is deliberately NOT folded into setViscosity's cell field. A cell with
  // one wall face and three interior faces must use the wall value on the wall
  // face ALONE; folding it in would apply it to all four and silently change
  // the finite-volume operator.
  //
  // clearWallFaceViscosity() returns the assembly to its no-override form, so a
  // laminar solve is bitwise what it was.
  void setWallFaceViscosity(const std::vector<cfd::Index>& faceIds,
                            const std::vector<cfd::Real>& values);
  void clearWallFaceViscosity();

  // The same override, taken from the host SurfaceField a turbulence model
  // publishes. Compacts to the non-zero entries internally, so the TRANSFER is
  // still O(N_wall) even though the argument is face-length; a null or
  // all-zero field clears the override.
  //
  // This exists because the wall override must reach the device on EVERY path
  // that assembles momentum there, not only when the resident turbulence
  // correction ran. A GPU solve whose turbulence took the host path would
  // otherwise assemble momentum with no wall-function shear at all.
  void setWallFaceViscosity(const cfd::fields::SurfaceField* wallViscosity);

  // Stage 7, on the device: pressure = pressure + (alpha * p'), bitwise
  // identical to SIMPLE.cpp's host loop. Keeps `pressure` resident instead of
  // round-tripping it through the host every iteration.
  void updatePressure(cfd::Real relaxationAlpha);

  // Explicit host access. A download of a Synchronized field performs NO
  // transfer; a download of a DeviceOwned field copies and marks it
  // Synchronized. Nothing else exposes these fields to the host.
  void downloadVelocity(cfd::fields::VectorField& out);
  void downloadPressure(cfd::fields::ScalarField& out);
  void downloadMassFlux(cfd::fields::SurfaceField& out);
  // P16 Phase 1. Viscosity is the fourth tracked persistent field and was the
  // only one with no explicit host access -- harmless while it was always
  // Synchronized (the host had just uploaded it), and a real gap once
  // adoptDeviceEffectiveViscosity makes it DeviceOwned, because then nothing
  // could read it back at all.
  void downloadViscosity(cfd::fields::ScalarField& out);

  // Stages 8 and 9 in their RESIDENT form: compute the correction and carry the
  // result into the persistent field, with no download. The non-resident
  // overloads above still exist and still download, because the GPU-DISC-001
  // harnesses drive them that way.
  void correctVelocityResident(cfd::Index gradientScheme, bool threeDimensional);
  void correctFaceMassFluxResident(bool withExplicitTerm);

  // SIMPLE's per-iteration NonFiniteState guard for the one field that has no
  // host copy during a resident solve. Four bytes cross the boundary instead of
  // a full field. Velocity and mass flux are downloaded each iteration for
  // other reasons, so SIMPLE checks those on the host.
  [[nodiscard]] bool residentPressureAllFinite();

  // --- one outer iteration, in SIMPLE's own order ------------------------

  // Uploads this iteration's inputs once. `massFlux` is the flux SIMPLE carries
  // between iterations, NOT the predictor.
  //
  // RETAINED for callers that do not use the resident path -- the GPU-DISC-001
  // harnesses drive single iterations this way, and their qualification
  // evidence must keep meaning what it meant. Equivalent to
  // uploadInitialState().
  void beginIteration(const cfd::fields::VectorField& velocity,
                      const cfd::fields::ScalarField& pressure,
                      const cfd::fields::SurfaceField& massFlux,
                      const cfd::fields::ScalarField& effectiveViscosity);

  // Stage 1. `component` is 0/1/2 for U/V/W. The assembled system comes back on
  // the host for the (separately qualified) linear solver; `diagonal` is
  // MomentumAssembly::diagonal, which the response coefficients need.
  [[nodiscard]] cfd::algebra::LinearSystem assembleMomentum(
      cfd::Index component, const cfd::fields::ScalarField& previousComponent,
      cfd::Real relaxationAlpha, cfd::Index convectionScheme, bool applyNonOrthogonalCorrection);

  // Stage 2's result, coming back from the host solver.
  void setMomentumSolution(cfd::Index component, const cfd::algebra::Vector& solution);

  // --- GPU-resident momentum (GPU-PIPE-001 final residency) --------------
  //
  // Stages 1 and 2 without the host in between, mirroring what the pressure
  // stage already does and reusing the SAME BiCGSTAB implementation through
  // the SAME resident entry point (GpuResidentSolve.hpp). No second solver,
  // no second assembly: `assembleMomentumResident` calls exactly the kernel
  // `assembleMomentum` calls, and simply does not download the result.
  //
  // `previousComponent` is absent by design rather than by omission: the
  // relaxation's phiOld is the start-of-iteration velocity component, which
  // is already resident, so it is taken from there instead of being handed
  // back across PCIe.
  void assembleMomentumResident(cfd::Index component, cfd::Real relaxationAlpha,
                                cfd::Index convectionScheme, bool applyNonOrthogonalCorrection);

  // Runs the resident BiCGSTAB against the resident momentum system, warm
  // started from that same resident component -- the bytes the host path
  // passes as `toVector(previousU)` -- and leaves u* in the device
  // predictor. `solution` in the returned result is intentionally EMPTY.
  [[nodiscard]] cfd::algebra::SolverResult solveMomentumResident(
      cfd::Index component, const cfd::algebra::LinearSolverSettings& settings);

  // SIMPLE's finiteness guards for the two fields that have no host copy
  // during a resident loop. Predicates, so reduction order carries no
  // bitwise consequence; four bytes cross instead of a full field.
  //
  // The predictor guard takes `threeDimensional` because on a 2D mesh the W
  // predictor is not written until computeResponseCoefficients zeroes it,
  // which happens AFTER this guard in SIMPLE's order -- reading it here
  // would examine a value no stage has produced. The host path has the same
  // shape: combineComponents leaves W exactly 0.0 on a 2D mesh.
  [[nodiscard]] bool residentMomentumPredictorAllFinite(bool threeDimensional);
  [[nodiscard]] bool residentVelocityAllFinite();

  // Stage 3. Uses the diagonals retained from assembleMomentum.
  void computeResponseCoefficients(bool threeDimensional);

  // Stage 4. `rhieChow` false selects the plain linear predictor. The pressure
  // gradient is computed ON DEVICE from the start-of-iteration pressure, with
  // the PRESSURE boundary conditions, exactly as SIMPLE.cpp:487 does.
  void computePredictedFaceFlux(bool rhieChow, cfd::Real density, cfd::Real relaxationAlpha,
                                cfd::Index gradientScheme, bool threeDimensional);

  // Stage 5. `previousPressureCorrection` is null on pass 1.
  [[nodiscard]] cfd::algebra::LinearSystem assemblePressureCorrection(
      bool nonOrthogonal, cfd::Index referenceCell, cfd::Real density,
      const cfd::fields::ScalarField* previousPressureCorrection, bool threeDimensional);

  // --- GPU-resident pressure solve (GPU-PIPE-001) ------------------------
  //
  // Stage 5 and stage 6 without the host in between. assembleResident leaves
  // the CSR system on the device instead of downloading it into a
  // LinearSystem; solvePressureCorrectionResident runs the SAME BiCGSTAB
  // implementation the host path uses (GpuResidentSolve.hpp explains how one
  // algorithm serves both entry points) directly against it, and leaves p'
  // resident for correctVelocityResident/correctFaceMassFluxResident.
  //
  // Only pass 1 is ever served this way: SIMPLE declines the GPU path entirely
  // when nonOrthogonalCorrections > 1, so the corrector passes never reach
  // here. `previousPressureCorrection` is therefore absent by construction
  // rather than by omission.
  void assemblePressureCorrectionResident(bool nonOrthogonal, cfd::Index referenceCell,
                                          cfd::Real density, bool threeDimensional);

  // Returns the solve's status, iteration count and residual history exactly as
  // the host path does. `solution` is intentionally EMPTY -- p' stayed on the
  // device, which is the point.
  [[nodiscard]] cfd::algebra::SolverResult solvePressureCorrectionResident(
      const cfd::algebra::LinearSolverSettings& settings);

  // The p' finiteness guard SIMPLE applies after the solve, evaluated on the
  // device because there is no host copy to ask. Four bytes cross.
  [[nodiscard]] bool residentPressureCorrectionAllFinite();

  // Device memory held by the resident solve's Krylov workspace, so a memory
  // probe can attribute it rather than seeing it as unexplained growth.
  [[nodiscard]] std::size_t residentSolveBytes() const noexcept;

  // Stage 6's result, coming back from the host solver.
  void setPressureCorrection(const cfd::algebra::Vector& pressureCorrection);

  // Stage 8. Applied to the PREDICTOR velocity held on the device, never to the
  // start-of-iteration velocity.
  void correctVelocity(cfd::Index gradientScheme, bool threeDimensional,
                       cfd::fields::VectorField& corrected);

  // Stage 9. Applied to the PREDICTOR flux held on the device, never to the
  // carried mass flux.
  void correctFaceMassFlux(bool withExplicitTerm, cfd::fields::SurfaceField& corrected);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  // P16-GPU-TURB Stage 7I -- the resident turbulence seam.
  //
  // The resident k-epsilon correction must read the SAME device state the CPU
  // algorithm would have handed it: the corrected velocity and corrected face
  // flux carried out of the previous outer iteration (see
  // beginIterationResident's own comment -- "the corrections already carried
  // their results into `velocity` and `massFlux`"). Both live in Impl, along
  // with the DeviceMomentumConvectionPlan the qualified device velocity
  // gradient needs.
  //
  // `ResidentTurbulenceInputs` is declared but NOT defined here: its members
  // are CUDA types, and this header must stay includable from a CPU-only
  // build. The definition lives in the CUDA-only
  // GpuSimpleResidentTurbulence.hpp, so the device types never cross this
  // boundary. A friend declaration of a function returning an incomplete type
  // is legal precisely because nothing here can call it.
  //
  // This exists so the turbulence path does NOT download the velocity field.
  // Exposing a borrowed view is the smaller change; the alternative was a full
  // D2H every outer iteration, which is what the whole resident design exists
  // to avoid.
  friend struct ResidentTurbulenceInputs;
  friend ResidentTurbulenceInputs residentTurbulenceInputs(GpuSimpleDiscretization& discretization);
  // P16 Phase 1: the return direction. Its argument is an incomplete type
  // here for the same reason ResidentTurbulenceInputs is -- the device buffer
  // it carries must not be named by a header that CPU-only targets compile.
  friend bool adoptDeviceEffectiveViscosity(GpuSimpleDiscretization& discretization,
                                            const ResidentTurbulentViscosity& turbulent);
};

}  // namespace cfd::gpu
