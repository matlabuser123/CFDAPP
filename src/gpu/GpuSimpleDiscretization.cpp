// GPU-DISC-001M -- the CPU-only stub.
//
// Compiled ONLY when CFDAPP_ENABLE_CUDA is off (see src/CMakeLists.txt);
// cuda/kernels/GpuSimpleDiscretizationCuda.cpp is the real implementation and
// exactly one of the two is ever built. Same split as GPUBackend and
// GpuResidencyManager.
//
// `active()` is false and `prepare()` refuses with a reason, so a CPU-only
// build that is handed enableGpuDiscretization records the fallback and runs
// the CPU path -- it never silently pretends.

#include "cfd/gpu/GpuSimpleDiscretization.hpp"

#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::gpu {

namespace {

// GPU-DISC-001R: the two assembling methods are UNREACHABLE in a CPU-only
// build -- prepare() returns false, so SIMPLE never dispatches to this object.
// They still have to compile, and what they do if the impossible happens is a
// real decision.
//
// They previously returned `LinearSystem(SparseMatrix{}, Vector{})`, which does
// not compile at all: SparseMatrix has no default constructor. That went
// unnoticed because this translation unit is built ONLY when
// CFDAPP_ENABLE_CUDA is off, every gate from GPU-DISC-001M onward built with it
// ON, and the work is uncommitted so CI never compiled it either. The
// GPU-DISC-001R CPU-only phase is the first thing that ever did.
//
// Throwing is the fix rather than fabricating an empty 0x0 system: a silently
// empty momentum or pressure system would be handed to a linear solver and
// produce a wrong answer instead of a loud failure, which is exactly what this
// file's own header comment ("it never silently pretends") rules out.
[[noreturn]] void unreachableWithoutCuda(const char* what) {
  throw cfd::InvalidArgumentError(
      std::string("GpuSimpleDiscretization::") + what +
      " was called in a build without CUDA support. prepare() returns false in this build, so "
      "SIMPLE should never dispatch here -- reaching this point is a dispatch defect.");
}

}  // namespace

struct GpuSimpleDiscretization::Impl {};

GpuSimpleDiscretization::GpuSimpleDiscretization() = default;
GpuSimpleDiscretization::~GpuSimpleDiscretization() = default;
GpuSimpleDiscretization::GpuSimpleDiscretization(GpuSimpleDiscretization&&) noexcept = default;
GpuSimpleDiscretization& GpuSimpleDiscretization::operator=(GpuSimpleDiscretization&&) noexcept =
    default;

bool GpuSimpleDiscretization::active() const noexcept { return false; }

bool GpuSimpleDiscretization::prepare(const cfd::mesh::Mesh&,
                                      const cfd::boundary::BoundaryConditionSet&,
                                      const cfd::boundary::BoundaryConditionSet&,
                                      std::string& reason) {
  reason = "this binary was built without CUDA support";
  return false;
}

void GpuSimpleDiscretization::beginIteration(const cfd::fields::VectorField&,
                                             const cfd::fields::ScalarField&,
                                             const cfd::fields::SurfaceField&,
                                             const cfd::fields::ScalarField&) {}

// --- GPU-PIPE-001 persistent fields, CPU-only parity ---------------------
//
// `prepare()` returns false in this build, so SIMPLE never dispatches here and
// none of these run. They exist so the CPU-only translation unit keeps exact
// parity with the header -- and, after GPU-DISC-001R, so that this file is
// COMPILED and kept honest rather than drifting until someone configures with
// CUDA off. The two that must return a value throw for the same reason
// assembleMomentum does: a fabricated answer would be worse than a loud one.

void GpuSimpleDiscretization::uploadInitialState(const cfd::fields::VectorField&,
                                                 const cfd::fields::ScalarField&,
                                                 const cfd::fields::SurfaceField&,
                                                 const cfd::fields::ScalarField&) {}

GpuSimpleDiscretization::FieldAuthority GpuSimpleDiscretization::authority(
    PersistentField) const noexcept {
  // Nothing is ever resident in a CPU-only build.
  return FieldAuthority::HostOnly;
}

void GpuSimpleDiscretization::beginIterationResident() {
  unreachableWithoutCuda("beginIterationResident");
}

void GpuSimpleDiscretization::setViscosity(const cfd::fields::ScalarField&) {}

void GpuSimpleDiscretization::updatePressure(cfd::Real) {
  unreachableWithoutCuda("updatePressure");
}

void GpuSimpleDiscretization::downloadVelocity(cfd::fields::VectorField&) {
  unreachableWithoutCuda("downloadVelocity");
}

void GpuSimpleDiscretization::downloadPressure(cfd::fields::ScalarField&) {
  unreachableWithoutCuda("downloadPressure");
}

void GpuSimpleDiscretization::downloadMassFlux(cfd::fields::SurfaceField&) {
  unreachableWithoutCuda("downloadMassFlux");
}

void GpuSimpleDiscretization::correctVelocityResident(cfd::Index, bool) {
  unreachableWithoutCuda("correctVelocityResident");
}

void GpuSimpleDiscretization::correctFaceMassFluxResident(bool) {
  unreachableWithoutCuda("correctFaceMassFluxResident");
}

bool GpuSimpleDiscretization::residentPressureAllFinite() {
  unreachableWithoutCuda("residentPressureAllFinite");
}

cfd::algebra::LinearSystem GpuSimpleDiscretization::assembleMomentum(
    cfd::Index, const cfd::fields::ScalarField&, cfd::Real, cfd::Index, bool) {
  unreachableWithoutCuda("assembleMomentum");
}

void GpuSimpleDiscretization::setMomentumSolution(cfd::Index, const cfd::algebra::Vector&) {}

// GPU-PIPE-001 final residency. Unreachable in this build for the same
// reason as everything else here, and loud rather than fabricating: a
// silently "successful" momentum solve would be handed to SIMPLE, which
// checks converged() and would proceed on a fake result.
void GpuSimpleDiscretization::assembleMomentumResident(cfd::Index, cfd::Real, cfd::Index, bool) {
  unreachableWithoutCuda("assembleMomentumResident");
}

cfd::algebra::SolverResult GpuSimpleDiscretization::solveMomentumResident(
    cfd::Index, const cfd::algebra::LinearSolverSettings&) {
  unreachableWithoutCuda("solveMomentumResident");
}

bool GpuSimpleDiscretization::residentMomentumPredictorAllFinite(bool) {
  unreachableWithoutCuda("residentMomentumPredictorAllFinite");
}

bool GpuSimpleDiscretization::residentVelocityAllFinite() {
  unreachableWithoutCuda("residentVelocityAllFinite");
}

void GpuSimpleDiscretization::computeResponseCoefficients(bool) {}

void GpuSimpleDiscretization::computePredictedFaceFlux(bool, cfd::Real, cfd::Real, cfd::Index,
                                                       bool) {}

cfd::algebra::LinearSystem GpuSimpleDiscretization::assemblePressureCorrection(
    bool, cfd::Index, cfd::Real, const cfd::fields::ScalarField*, bool) {
  unreachableWithoutCuda("assemblePressureCorrection");
}

void GpuSimpleDiscretization::setPressureCorrection(const cfd::algebra::Vector&) {}

// GPU-PIPE-001 (GPU-resident pressure solve). Same reasoning as the two
// assembling methods above: unreachable in this build, and a fabricated
// "successful" solve would be worse than a loud failure -- SIMPLE checks
// pResult.converged() and would proceed on a fake result. residentSolveBytes()
// is the exception: it is an observation, not an action, and zero is the true
// answer when nothing is resident.
void GpuSimpleDiscretization::assemblePressureCorrectionResident(bool, cfd::Index, cfd::Real,
                                                                 bool) {
  unreachableWithoutCuda("assemblePressureCorrectionResident");
}

cfd::algebra::SolverResult GpuSimpleDiscretization::solvePressureCorrectionResident(
    const cfd::algebra::LinearSolverSettings&) {
  unreachableWithoutCuda("solvePressureCorrectionResident");
}

bool GpuSimpleDiscretization::residentPressureCorrectionAllFinite() {
  unreachableWithoutCuda("residentPressureCorrectionAllFinite");
}

std::size_t GpuSimpleDiscretization::residentSolveBytes() const noexcept { return 0; }

void GpuSimpleDiscretization::correctVelocity(cfd::Index, bool, cfd::fields::VectorField&) {}

void GpuSimpleDiscretization::correctFaceMassFlux(bool, cfd::fields::SurfaceField&) {}

}  // namespace cfd::gpu
