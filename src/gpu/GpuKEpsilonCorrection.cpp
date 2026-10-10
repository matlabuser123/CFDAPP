// P16-GPU-TURB Stage 7D -- the CPU-ONLY stub.
//
// Compiled ONLY when CFDAPP_ENABLE_CUDA is OFF (see src/CMakeLists.txt's own
// `if(NOT CFDAPP_ENABLE_CUDA)` block). The real implementation lives in
// cuda/kernels/GpuKEpsilonCorrectionCuda.cpp and is part of cfdcuda; exactly
// one of the two is ever compiled, the same arrangement
// GpuSimpleDiscretization and GPUBackend already use.
//
// `available()` returns false unconditionally here. That is a COMPILE-TIME
// fact, not a runtime check: a binary built without CUDA cannot execute GPU
// turbulence, and a caller asking is told so without any device query.
//
// `prepare()` refuses with a reason naming the build, so a user who configured
// a GPU turbulence run and got CPU numbers can find out why from the
// diagnostic rather than from the timings.

#include "cfd/gpu/GpuKEpsilonCorrection.hpp"

#include "cfd/core/Exception.hpp"

namespace cfd::gpu {

struct GpuKEpsilonCorrection::Impl {
  std::string unsupportedReason{"this binary was built without CUDA (CFDAPP_ENABLE_CUDA=OFF)"};
};

GpuKEpsilonCorrection::GpuKEpsilonCorrection() : impl_(std::make_unique<Impl>()) {}
GpuKEpsilonCorrection::~GpuKEpsilonCorrection() = default;
GpuKEpsilonCorrection::GpuKEpsilonCorrection(GpuKEpsilonCorrection&&) noexcept = default;
GpuKEpsilonCorrection& GpuKEpsilonCorrection::operator=(GpuKEpsilonCorrection&&) noexcept = default;

bool GpuKEpsilonCorrection::available() noexcept { return false; }

bool GpuKEpsilonCorrection::correctFromResidentDiscretization(
    GpuSimpleDiscretization&, const cfd::mesh::Mesh&, const cfd::physics::FluidProperties&,
    const cfd::turbulence::KEpsilonConfig&, const std::vector<cfd::turbulence::WallFace>&,
    cfd::turbulence::WallTreatment&, GpuKEpsilonResult&) {
  // CPU-only build: there is no device, so there is no resident state to
  // correct from. Refusing here is what keeps the caller's guard honest rather
  // than letting a CUDA-OFF build silently take a "resident" path.
  return false;
}

bool GpuKEpsilonCorrection::prepare(const cfd::mesh::Mesh&,
                                    const cfd::boundary::BoundaryConditionSet&,
                                    const cfd::boundary::BoundaryConditionSet&,
                                    const cfd::fields::ScalarField&,
                                    const cfd::fields::ScalarField&,
                                    const cfd::fields::ScalarField&) {
  return false;
}

bool GpuKEpsilonCorrection::prepared() const noexcept { return false; }

const std::string& GpuKEpsilonCorrection::unsupportedReason() const noexcept {
  return impl_->unsupportedReason;
}

void GpuKEpsilonCorrection::correct(const cfd::mesh::Mesh&, const cfd::fields::VectorField&,
                                    const cfd::boundary::BoundaryConditionSet&,
                                    const cfd::physics::FluidProperties&,
                                    const cfd::turbulence::KEpsilonConfig&,
                                    const GpuKEpsilonWallInputs&, GpuKEpsilonResult&) {
  // Throwing rather than quietly doing nothing: reaching here means a caller
  // decided to use the GPU path in a binary that has none, and an empty result
  // would be indistinguishable from a converged one.
  throw cfd::InvalidArgumentError(
      "GpuKEpsilonCorrection::correct: this binary was built without CUDA");
}

}  // namespace cfd::gpu
