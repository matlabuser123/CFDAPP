// GPU-DISC-001J -- host-side construction of the velocity-correction plan.
//
// The only thing this adds over the two gradient plans is
// makeGradientBoundaries (PressureCorrectionEquation.cpp:43), restated here
// because it is file-static there. It is four lines and exactly reproduced:
//
//   FixedValue(0.0)    where the PRESSURE patch's condition is FixedValue
//   FixedGradient(0.0) on every other patch
//
// Building it inside the plan means the caller passes the same
// `pressureBoundaries` the CPU is handed and cannot construct the p' set
// differently -- which would be a silent discretization change, not an error.

#include <memory>
#include <string>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::gpu {

using cfd::Index;

bool DeviceVelocityCorrectionPlan::build(
    const cfd::mesh::Mesh& mesh, const cfd::boundary::BoundaryConditionSet& pressureBoundaries) {
  usable_ = false;
  unsupportedReason_.clear();
  fixedValuePatches_ = 0;

  cfd::boundary::BoundaryConditionSet gradientBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() ==
        cfd::boundary::BoundaryConditionType::FixedValue) {
      gradientBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
      ++fixedValuePatches_;
    } else {
      gradientBoundaries.set(mesh, patch.name(),
                             std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }

  // One plan covers both schemes: the least-squares plan embeds the
  // Green-Gauss one it uses as its own fallback, so building it builds both.
  if (!leastSquares_.build(mesh, gradientBoundaries)) {
    unsupportedReason_ = leastSquares_.unsupportedReason();
    return false;
  }

  cellCount_ = mesh.numberOfCells();
  threeDimensional_ = mesh.dimension() == 3;
  usable_ = true;
  return true;
}

std::size_t DeviceVelocityCorrectionPlan::residentBytes() const noexcept {
  return leastSquares_.residentBytes();
}

}  // namespace cfd::gpu
