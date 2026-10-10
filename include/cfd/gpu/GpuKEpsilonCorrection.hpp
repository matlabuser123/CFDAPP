#pragma once

// P16-GPU-TURB Stage 7D -- the production seam for GPU k-epsilon.
//
// CUDA-FREE BY CONSTRUCTION, and that is the whole reason this file exists.
// `DeviceKEpsilon.hpp` pulls in <cuda_runtime.h> and can only be included from
// the cfdcuda target; `KEpsilonModel` lives in cfdcore and must compile in a
// CPU-only build. So this is the same pimpl facade `GpuSimpleDiscretization`
// already uses for exactly the same reason: one CUDA-free header, a real
// implementation in cuda/kernels/, and a stub under
// `if(NOT CFDAPP_ENABLE_CUDA)` whose active() returns false.
//
// WHY A FACADE AND NOT A SECOND IMPLEMENTATION
// --------------------------------------------
// Everything below forwards to the already-qualified `correctKEpsilonDevice`.
// The Stage 6/7 tests and production call the SAME implementation -- if they
// did not, the equivalence those tests establish would say nothing about what
// ships.
//
// WHAT THIS DOES NOT HIDE
// -----------------------
// The high-Re wall treatment stays on the HOST in P16 by an explicit scope
// decision. `correct()` therefore takes the wall treatment's compact outputs as
// inputs; it does not compute them. A caller with wall functions enabled must
// evaluate `computeWallTreatment` and pass the results, exactly as
// KEpsilonModel already does on the CPU path.
//
// `allOnGpu()` is false for any turbulent case in P16, and stays false.

#include <memory>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

namespace cfd::mesh {
class Mesh;
}
namespace cfd::boundary {
class BoundaryConditionSet;
}
namespace cfd::physics {
class FluidProperties;
}
namespace cfd::turbulence {
struct KEpsilonConfig;
}

namespace cfd::gpu {

// Defined in the CUDA-only GpuSimpleResidentTurbulence.hpp. Incomplete here on
// purpose: its members are CUDA types and this header must stay includable
// from a CPU-only build. A reference to an incomplete type is legal in a
// declaration, which is exactly why correctResident() can take one.
struct ResidentTurbulenceInputs;
class GpuSimpleDiscretization;

// The host wall treatment's outputs for one correction. Empty vectors mean
// "wall functions are off", which must leave the correction identical to a
// no-wall run.
//
// COMPACT BY DESIGN: these are O(wall cells), not fields. Nothing here
// requires downloading k, epsilon or mu_t -- the caller already holds the k it
// passed to computeWallTreatment.
struct GpuKEpsilonWallInputs {
  const std::vector<cfd::Index>* wallCells{nullptr};
  const std::vector<cfd::Real>* wallCellProduction{nullptr};
  const std::vector<cfd::Real>* wallCellDissipation{nullptr};
};

// What one production correction produced.
struct GpuKEpsilonResult {
  cfd::fields::ScalarField k;
  cfd::fields::ScalarField epsilon;
  cfd::fields::ScalarField turbulentViscosity;
  cfd::Real convergenceResidual{};
  cfd::Index kIterations{};
  cfd::Index epsilonIterations{};
  cfd::Real kResidual{};
  cfd::Real epsilonResidual{};
};

class GpuKEpsilonCorrection {
 public:
  GpuKEpsilonCorrection();
  ~GpuKEpsilonCorrection();
  GpuKEpsilonCorrection(const GpuKEpsilonCorrection&) = delete;
  GpuKEpsilonCorrection& operator=(const GpuKEpsilonCorrection&) = delete;
  GpuKEpsilonCorrection(GpuKEpsilonCorrection&&) noexcept;
  GpuKEpsilonCorrection& operator=(GpuKEpsilonCorrection&&) noexcept;

  // True iff this binary was built with CUDA AND a usable device was present.
  // False in a CPU-only build, always -- the stub implementation makes that a
  // compile-time fact rather than a runtime hope.
  [[nodiscard]] static bool available() noexcept;

  // Builds the two plan pairs and uploads the initial turbulence state.
  //
  // TWO PLAN PAIRS, NOT ONE. k and epsilon have different boundary sets and a
  // DeviceDiffusionPlan bakes the encoded values in at build() time; sharing
  // them applies k's inlet value to epsilon. Stage 6 shipped that defect and
  // measured a 23.5% epsilon error with k untouched, so this signature takes
  // both sets and the implementation refuses to share.
  //
  // Returns false (with unsupportedReason() set) when the mesh or boundary
  // conditions are outside the device discretization's support. A false return
  // is a REFUSAL, not a silent fallback: the caller decides what to do and is
  // responsible for recording the backend it actually used.
  [[nodiscard]] bool prepare(const cfd::mesh::Mesh& mesh,
                             const cfd::boundary::BoundaryConditionSet& kBoundaries,
                             const cfd::boundary::BoundaryConditionSet& epsilonBoundaries,
                             const cfd::fields::ScalarField& k,
                             const cfd::fields::ScalarField& epsilon,
                             const cfd::fields::ScalarField& turbulentViscosity);

  [[nodiscard]] bool prepared() const noexcept;
  [[nodiscard]] const std::string& unsupportedReason() const noexcept;

  // One correction, in the audited production order. The device state is
  // carried inside, so correction N+1 reads what N published -- and nothing is
  // published until both solves have succeeded.
  //
  // Throws NumericalError if either solve fails, leaving the device state
  // untouched, which is the guarantee KEpsilonModel documents for itself.
  void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
               const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
               const cfd::physics::FluidProperties& fluid,
               const cfd::turbulence::KEpsilonConfig& config, const GpuKEpsilonWallInputs& wall,
               GpuKEpsilonResult& result);

  // P16-GPU-TURB Stage 7I step 2 -- the RESIDENT entry point.
  //
  // WHY A SECOND ENTRY POINT AND NOT A FLAG. `correct()` above computes the
  // mass flux and the velocity gradient ON THE HOST from the `velocity` it is
  // given. That is right for the non-resident path, where the host field is
  // current. It is wrong for a resident SIMPLE loop, where the host mirror is
  // frozen at iteration 0 for the whole solve -- which is the invariant the
  // laminar guard has been protecting. Rather than teach one function to
  // distrust its own argument, the resident caller passes the resident state
  // and never supplies a host velocity at all.
  //
  // Note the signature takes NO velocity and NO velocity boundaries: the
  // gradient comes from `inputs.convectionPlan` (which already encodes those
  // boundary conditions) applied to `inputs.velocity`, and the mass flux is
  // adopted from `inputs.massFlux`. There is no host field to get wrong.
  //
  // BOTH ENTRY POINTS CONVERGE on the same `correctKEpsilonDevice`. They differ
  // only in how the gradient and flux reach the device; every downstream
  // semantic -- the mu_t_OLD lag, the k_OLD/epsilon_OLD sources, epsilon never
  // seeing fresh k, the atomic publication -- is the one implementation both
  // share.
  //
  // BORROWED-VIEW LIFETIME CONTRACT. `inputs` aliases buffers owned by
  // GpuSimpleDiscretization::Impl. This call BORROWS them for its duration
  // only: nothing here frees, resizes or retains them, and the view must
  // outlive the call but need not outlive anything else. Passing a view whose
  // discretization has been destroyed is a use-after-free the same way any
  // dangling reference is -- the type is a view and is named like one.
  //
  // `wall` is the same compact structure `correct()` takes. Step 2
  // deliberately does NOT change how the caller obtains it; the compact
  // wall-owner gather is step 3, and mixing the two would make neither
  // testable on its own.
  void correctResident(const ResidentTurbulenceInputs& inputs, const cfd::mesh::Mesh& mesh,
                       const cfd::physics::FluidProperties& fluid,
                       const cfd::turbulence::KEpsilonConfig& config,
                       const GpuKEpsilonWallInputs& wall, GpuKEpsilonResult& result);

  // P16-GPU-TURB Stage 7I step 3 -- the resident path WITH the compact wall
  // exchange.
  //
  // THE TRANSFER THIS EXISTS TO AVOID. The audited wall law reads exactly
  // `velocity[owner]` (WallTreatment.cpp:84) and `turbulentEnergy[owner]`
  // (lines 98-159) for wall-owner cells, and nothing else -- not epsilon, not
  // mu_t, not any interior cell. So the data that must cross PCIe is
  // O(wall faces), not O(cells).
  //
  // HOW THE HOST LAW IS FED WITHOUT A FIELD DOWNLOAD. `evaluateWallFace`
  // size-checks its inputs against the cell count (line 73), so it needs
  // full-LENGTH fields even though it reads only a handful of entries. This
  // therefore builds full-length host fields whose wall-owner entries hold the
  // gathered device values and whose every OTHER entry is a quiet NaN.
  //
  // The NaN is not laziness, it is the control. If the wall law ever reads a
  // cell the audit says it does not, the result is NaN and it propagates into
  // y+, the wall epsilon and the production replacement immediately --
  // loudly, on the first run, rather than as a plausible wrong number. Zero
  // filling would hide exactly that mistake.
  //
  // `computeWallTreatment` itself is called UNCHANGED, with the same
  // `wallFaces` vector in the same order, so the sequential corner rule
  // (nearest wall face wins, first claim sets the slot) observes precisely the
  // sequence it always has.
  void correctResidentWithWallTreatment(const ResidentTurbulenceInputs& inputs,
                                        const cfd::mesh::Mesh& mesh,
                                        const cfd::physics::FluidProperties& fluid,
                                        const cfd::turbulence::KEpsilonConfig& config,
                                        const std::vector<cfd::turbulence::WallFace>& wallFaces,
                                        cfd::turbulence::WallTreatment& treatmentOut,
                                        GpuKEpsilonResult& result);

  // P16 Step 4 -- THE PRODUCTION SEAM.
  //
  // One call that a CPU-only translation unit may name: it borrows the
  // discretization's resident state, runs the qualified resident correction,
  // and hands the results back to the SAME discretization, without any CUDA
  // type crossing this header.
  //
  // Everything it does, in the audited CPU order:
  //
  //   borrow resident U(N-1) and the corrected carried face flux
  //   GPU strain rate -> P_k from mu_t OLD
  //   compact O(N_wall) gather -> CPU wall law -> compact scatter
  //   resident k and epsilon assembly/solve from k OLD, epsilon OLD, mu_t OLD
  //   floors, imposed-epsilon reassertion, fresh mu_t, atomic publication
  //   mu_eff = mu + mu_t on device, adopted by the discretization
  //   compact wall-face viscosity overrides, scattered on device
  //
  // The HOST velocity is never read. That is the whole point: a resident loop
  // leaves it frozen at iteration 0, which is exactly what the laminar-only
  // guard existed to avoid.
  //
  // Returns false when the discretization has no resident state yet, so a
  // caller cannot mistake "not started" for "done".
  [[nodiscard]] bool correctFromResidentDiscretization(
      GpuSimpleDiscretization& discretization, const cfd::mesh::Mesh& mesh,
      const cfd::physics::FluidProperties& fluid, const cfd::turbulence::KEpsilonConfig& config,
      const std::vector<cfd::turbulence::WallFace>& wallFaces,
      cfd::turbulence::WallTreatment& treatmentOut, GpuKEpsilonResult& result);

 private:
  // The shared tail of both entry points. Everything from the wall inputs
  // onward lives here, so a semantic added to one entry point cannot be
  // missing from the other.
  void runCorrection(const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
                     const cfd::turbulence::KEpsilonConfig& config,
                     const GpuKEpsilonWallInputs& wall, GpuKEpsilonResult& result);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cfd::gpu
