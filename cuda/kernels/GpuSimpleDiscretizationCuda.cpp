// GPU-DISC-001M -- the real GpuSimpleDiscretization, part of cfdcuda.
//
// Pure INTEGRATION: every numerical line here is a call into an operator
// already qualified bitwise by GPU-DISC-001B..001K. Nothing is reimplemented,
// and no scheme is invented. What this file owns is the plumbing -- the plans,
// the device-resident state carried between stages, and the two unavoidable
// host round-trips the LinearSolver interface forces.
//
// The stage methods appear in SIMPLE's own order.

#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceFaceFluxCorrection.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DevicePersistentFields.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"
#include "cfd/gpu/GpuResidentSolve.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

// The device candidate CSR can carry explicit zeros the CPU builder drops, so
// the host system is rebuilt through SparseMatrixBuilder with the same
// drop-exact-zero rule. This is the ONE place the assembled system crosses back
// to the host, and it exists because LinearSolver takes a host LinearSystem.
cfd::algebra::LinearSystem toHostSystem(Index rows, const std::vector<Index>& rowOffsets,
                                        const std::vector<Index>& columnIndices,
                                        const std::vector<Real>& values,
                                        const std::vector<Real>& rhs) {
  cfd::algebra::SparseMatrixBuilder builder(rows, rows);
  for (Index r = 0; r < rows; ++r) {
    for (Index k = rowOffsets[r]; k < rowOffsets[r + 1]; ++k) {
      if (values[k] != 0.0) builder.add(r, columnIndices[k], values[k]);
    }
  }
  cfd::algebra::Vector b(static_cast<std::size_t>(rows));
  for (Index i = 0; i < rows; ++i) b[i] = rhs[i];
  return cfd::algebra::LinearSystem(builder.build(), b);
}

std::vector<Real> download(const DeviceBuffer<Real>& buffer) {
  std::vector<Real> host(static_cast<std::size_t>(buffer.size()));
  if (!host.empty()) buffer.downloadTo(host.data(), buffer.size());
  return host;
}

std::vector<Index> downloadIndices(const DeviceBuffer<Index>& buffer) {
  std::vector<Index> host(static_cast<std::size_t>(buffer.size()));
  if (!host.empty()) buffer.downloadTo(host.data(), buffer.size());
  return host;
}

// Every stage below consumes state a PREVIOUS stage was supposed to make
// resident. A caller that skips one of those uploads used to reach a kernel
// with an unsized buffer and fail as an illegal memory access from inside CUDA
// -- found by GPU-DISC-001N's negative controls N3 and N4. Failure handling
// here is explicit, matching every other device entry point in this layer.
void requireResident(const DeviceBuffer<Real>& buffer, Index expected, const char* what) {
  if (buffer.size() < expected) {
    throw cfd::InvalidArgumentError(std::string("GpuSimpleDiscretization: ") + what +
                                    " is not resident -- the stage that supplies it was skipped");
  }
}

}  // namespace

struct GpuSimpleDiscretization::Impl {
  bool available = false;
  bool ready = false;
  Index cellCount = 0;
  Index faceCount = 0;

  // Plans -- built once per mesh by prepare().
  DeviceMomentumAssemblyPlan momentum;
  DeviceFaceFluxPlan faceFlux;
  DevicePressureCorrectionPlan pressureCorrection;
  DeviceVelocityCorrectionPlan velocityCorrection;
  // The PRESSURE gradient Rhie-Chow needs uses the PRESSURE boundary
  // conditions, which differ from the p' set the velocity-correction plan
  // builds internally -- so it needs its own plan.
  DeviceGradientPlan pressureGradient;

  // Iteration state, device-resident between stages.
  DeviceVelocity velocity;       // start-of-iteration
  DeviceVelocity velocityStar;   // the momentum predictor
  DeviceBuffer<Real> pressure;
  DeviceBuffer<Real> viscosity;
  DeviceBuffer<Real> massFlux;   // carried between iterations, not the predictor
  DeviceBuffer<Real> previousU, previousV, previousW;
  // Retained whole rather than copying out the diagonal: the response
  // coefficients need THIS assembly's diagonal, and keeping the system alive
  // costs nothing while a device-to-device copy would need either a new kernel
  // or a host round-trip -- the very thing this gate exists to remove.
  DeviceMomentumSystem systemU, systemV, systemW;
  DeviceBuffer<Real> responseU, responseV, responseW;
  DeviceBuffer<Real> predictorFlux;
  DeviceBuffer<Real> pPrime;
  DeviceBuffer<Real> previousPPrime;
  DeviceBuffer<Real> gradPx, gradPy, gradPz;
  DevicePressureCorrectionSystem pressureSystem;
  // Scratch for the p' gradient the velocity correction produces.
  DeviceBuffer<Real> correctionGradX, correctionGradY, correctionGradZ;
  // The two correction OUTPUTS. Owned here rather than declared inside the
  // stage methods: DeviceBuffer never shrinks, so a persistent buffer allocates
  // once for the whole solve, while a function-local one allocates on every
  // outer iteration -- which the transfer audit measured as 4 allocations per
  // iteration before this was fixed.
  DeviceVelocity correctedVelocity;
  DeviceBuffer<Real> correctedFlux;

  // GPU-PIPE-001: scratch for the device-side pressure update. The update
  // cannot write in place (out may alias neither input), so the result lands
  // here and is carried back -- both device-to-device, nothing crosses PCIe.
  DeviceBuffer<Real> pressureNext;
  // GPU-PIPE-001: the non-finite counter, owned here so the per-iteration
  // guard allocates nothing. A locally-scoped counter cost 4 allocations,
  // 4 H2D and 4 D2H per iteration -- measured, and fixed.
  DeviceBuffer<unsigned int> nonFiniteCounter;

  // GPU-PIPE-001 (GPU-resident pressure solve): the solver's view of the
  // assembly's own CSR -- adopted, never copied -- and the Krylov workspace,
  // owned here so it persists across outer iterations and allocates nothing in
  // steady state. `pressureMatrix` holds no memory of its own.
  DeviceCsrMatrix pressureMatrix;
  // GPU-PIPE-001 final residency: the same borrowing view for whichever
  // momentum component is being solved. Holds no memory of its own, and is
  // re-adopted per component, so one instance serves all three.
  DeviceCsrMatrix momentumMatrix;
  // ONE Krylov workspace serves the momentum and pressure solves alike.
  // Each resident solve establishes everything it reads -- b and x are
  // copied in, the Jacobi diagonal is rebuilt, and bicgstabCore writes r,
  // rHat, p and v before reading them -- so sharing it is safe AND is what
  // keeps steady-state allocations at zero. Negative control `rl5` exists
  // to prove a missing one of those resets is detected rather than assumed.
  GpuKrylovWorkspace krylov;

  // GPU-PIPE-001: explicit authority per persistent field. There is exactly one
  // authoritative copy at any time and this says which.
  using Authority = GpuSimpleDiscretization::FieldAuthority;
  Authority velocityAuthority = Authority::HostOnly;
  Authority pressureAuthority = Authority::HostOnly;
  Authority massFluxAuthority = Authority::HostOnly;
  Authority viscosityAuthority = Authority::HostOnly;
  // Set once uploadInitialState has run; beginIterationResident refuses before
  // that rather than silently computing from empty buffers.
  bool stateUploaded = false;
};

GpuSimpleDiscretization::GpuSimpleDiscretization() : impl_(std::make_unique<Impl>()) {
  impl_->available = cfd::gpu::cudaAvailable();
}
GpuSimpleDiscretization::~GpuSimpleDiscretization() = default;
GpuSimpleDiscretization::GpuSimpleDiscretization(GpuSimpleDiscretization&&) noexcept = default;
GpuSimpleDiscretization& GpuSimpleDiscretization::operator=(GpuSimpleDiscretization&&) noexcept =
    default;

bool GpuSimpleDiscretization::active() const noexcept { return impl_ && impl_->available; }

bool GpuSimpleDiscretization::prepare(const cfd::mesh::Mesh& mesh,
                                      const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                      const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
                                      std::string& reason) {
  impl_->ready = false;
  if (!impl_->available) {
    reason = "no usable CUDA device";
    return false;
  }
  // ALL OR NOTHING: if any operator cannot reproduce this configuration, the
  // caller runs the whole iteration on the CPU rather than a mixed path no gate
  // has verified.
  if (!impl_->momentum.build(mesh, velocityBoundaries, pressureBoundaries)) {
    reason = "momentum assembly plan: " + impl_->momentum.unsupportedReason();
    return false;
  }
  if (!impl_->faceFlux.build(mesh, velocityBoundaries)) {
    reason = "face-flux plan: " + impl_->faceFlux.unsupportedReason();
    return false;
  }
  if (!impl_->pressureCorrection.build(mesh, pressureBoundaries)) {
    reason = "pressure-correction plan: " + impl_->pressureCorrection.unsupportedReason();
    return false;
  }
  if (!impl_->velocityCorrection.build(mesh, pressureBoundaries)) {
    reason = "velocity-correction plan: " + impl_->velocityCorrection.unsupportedReason();
    return false;
  }
  if (!impl_->pressureGradient.build(mesh, pressureBoundaries)) {
    reason = "pressure-gradient plan: " + impl_->pressureGradient.unsupportedReason();
    return false;
  }
  impl_->cellCount = mesh.numberOfCells();
  impl_->faceCount = mesh.numberOfFaces();
  impl_->ready = true;
  reason.clear();
  return true;
}

void GpuSimpleDiscretization::beginIteration(const cfd::fields::VectorField& velocity,
                                             const cfd::fields::ScalarField& pressure,
                                             const cfd::fields::SurfaceField& massFlux,
                                             const cfd::fields::ScalarField& effectiveViscosity) {
  if (!impl_->ready) return;
  const Index nc = impl_->cellCount;
  std::vector<Real> x(static_cast<std::size_t>(nc)), y(static_cast<std::size_t>(nc)),
      z(static_cast<std::size_t>(nc));
  for (Index c = 0; c < nc; ++c) {
    x[c] = velocity[c].x;
    y[c] = velocity[c].y;
    z[c] = velocity[c].z;
  }
  impl_->velocity.x.uploadFrom(x.data(), nc);
  impl_->velocity.y.uploadFrom(y.data(), nc);
  impl_->velocity.z.uploadFrom(z.data(), nc);
  impl_->pressure.uploadFrom(pressure.data(), nc);
  impl_->viscosity.uploadFrom(effectiveViscosity.data(), nc);
  impl_->massFlux.uploadFrom(massFlux.data(), impl_->faceCount);

  // GPU-PIPE-001: the host state has just been made authoritative on both
  // sides. `beginIteration` and `uploadInitialState` are the same operation;
  // the latter is the name the resident path uses.
  impl_->velocityAuthority = FieldAuthority::Synchronized;
  impl_->pressureAuthority = FieldAuthority::Synchronized;
  impl_->massFluxAuthority = FieldAuthority::Synchronized;
  impl_->viscosityAuthority = FieldAuthority::Synchronized;
  impl_->stateUploaded = true;
}

void GpuSimpleDiscretization::uploadInitialState(
    const cfd::fields::VectorField& velocity, const cfd::fields::ScalarField& pressure,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& effectiveViscosity) {
  beginIteration(velocity, pressure, massFlux, effectiveViscosity);
}

GpuSimpleDiscretization::FieldAuthority GpuSimpleDiscretization::authority(
    PersistentField field) const noexcept {
  switch (field) {
    case PersistentField::Velocity: return impl_->velocityAuthority;
    case PersistentField::Pressure: return impl_->pressureAuthority;
    case PersistentField::MassFlux: return impl_->massFluxAuthority;
    case PersistentField::Viscosity: return impl_->viscosityAuthority;
  }
  return FieldAuthority::HostOnly;
}

void GpuSimpleDiscretization::beginIterationResident() {
  if (!impl_->ready) return;
  if (!impl_->stateUploaded) {
    throw cfd::InvalidArgumentError(
        "GpuSimpleDiscretization::beginIterationResident: uploadInitialState has not run, so "
        "there is no resident state to continue from");
  }
  // Nothing crosses PCIe. The corrections already carried their results into
  // `velocity` and `massFlux`, and `updatePressure` into `pressure`; this is
  // the explicit statement that a new outer iteration begins from them.
}

void GpuSimpleDiscretization::setViscosity(const cfd::fields::ScalarField& effectiveViscosity) {
  if (!impl_->ready) return;
  impl_->viscosity.uploadFrom(effectiveViscosity.data(), impl_->cellCount);
  impl_->viscosityAuthority = FieldAuthority::Synchronized;
}

void GpuSimpleDiscretization::updatePressure(Real relaxationAlpha) {
  if (!impl_->ready) return;
  requireResident(impl_->pressure, impl_->cellCount, "the pressure field");
  requireResident(impl_->pPrime, impl_->cellCount, "the pressure correction");
  relaxedPressureUpdateDevice(impl_->cellCount, impl_->pressure, impl_->pPrime, relaxationAlpha,
                              impl_->pressureNext);
  carryFieldDevice(impl_->cellCount, impl_->pressureNext, impl_->pressure);
  impl_->pressureAuthority = FieldAuthority::DeviceOwned;
}

void GpuSimpleDiscretization::downloadVelocity(cfd::fields::VectorField& out) {
  if (!impl_->ready) return;
  requireResident(impl_->velocity.x, impl_->cellCount, "the velocity field");
  const Index nc = impl_->cellCount;
  const auto x = download(impl_->velocity.x);
  const auto y = download(impl_->velocity.y);
  const auto z = download(impl_->velocity.z);
  out = cfd::fields::VectorField(nc);
  for (Index c = 0; c < nc; ++c) out[c] = cfd::Vector3{x[c], y[c], z[c]};
  impl_->velocityAuthority = FieldAuthority::Synchronized;
}

void GpuSimpleDiscretization::downloadPressure(cfd::fields::ScalarField& out) {
  if (!impl_->ready) return;
  requireResident(impl_->pressure, impl_->cellCount, "the pressure field");
  const auto host = download(impl_->pressure);
  out = cfd::fields::ScalarField(impl_->cellCount);
  for (Index c = 0; c < impl_->cellCount; ++c) out[c] = host[c];
  impl_->pressureAuthority = FieldAuthority::Synchronized;
}

void GpuSimpleDiscretization::downloadMassFlux(cfd::fields::SurfaceField& out) {
  if (!impl_->ready) return;
  requireResident(impl_->massFlux, impl_->faceCount, "the mass flux field");
  const auto host = download(impl_->massFlux);
  out = cfd::fields::SurfaceField(impl_->faceCount);
  for (Index f = 0; f < impl_->faceCount; ++f) out[f] = host[f];
  impl_->massFluxAuthority = FieldAuthority::Synchronized;
}

cfd::algebra::LinearSystem GpuSimpleDiscretization::assembleMomentum(
    Index component, const cfd::fields::ScalarField& previousComponent, Real relaxationAlpha,
    Index convectionScheme, bool applyNonOrthogonalCorrection) {
  const Index nc = impl_->cellCount;
  DeviceBuffer<Real>& previous = component == kVelocityU   ? impl_->previousU
                                 : component == kVelocityV ? impl_->previousV
                                                           : impl_->previousW;
  previous.uploadFrom(previousComponent.data(), nc);

  MomentumAssemblyOptions options;
  options.component = component;
  options.convectionScheme = convectionScheme;
  options.relaxationAlpha = relaxationAlpha;
  options.applyNonOrthogonalCorrection = applyNonOrthogonalCorrection;

  DeviceMomentumSystem& deviceSystem = component == kVelocityU   ? impl_->systemU
                                       : component == kVelocityV ? impl_->systemV
                                                                 : impl_->systemW;
  assembleRelaxedMomentumDevice(impl_->momentum, impl_->velocity, impl_->pressure,
                                impl_->viscosity, impl_->massFlux, previous, nullptr, options,
                                deviceSystem);

  return toHostSystem(nc, downloadIndices(deviceSystem.rowOffsets),
                      downloadIndices(deviceSystem.columnIndices), download(deviceSystem.values),
                      download(deviceSystem.rhs));
}

void GpuSimpleDiscretization::setMomentumSolution(Index component,
                                                  const cfd::algebra::Vector& solution) {
  if (!impl_->ready) return;
  const Index nc = impl_->cellCount;
  std::vector<Real> host(static_cast<std::size_t>(nc));
  for (Index i = 0; i < nc; ++i) host[i] = solution[i];
  DeviceBuffer<Real>& target = component == kVelocityU   ? impl_->velocityStar.x
                               : component == kVelocityV ? impl_->velocityStar.y
                                                         : impl_->velocityStar.z;
  target.uploadFrom(host.data(), nc);
}

void GpuSimpleDiscretization::assembleMomentumResident(Index component, Real relaxationAlpha,
                                                       Index convectionScheme,
                                                       bool applyNonOrthogonalCorrection) {
  if (!impl_->ready) return;
  const Index nc = impl_->cellCount;
  // The relaxation's phiOld. The host path uploads
  // `selectComponent(velocity, ...)`; the device already holds exactly those
  // bytes in its own `velocity`, so this is a device-to-device carry of the
  // same values rather than a round trip.
  const DeviceBuffer<Real>& source = component == kVelocityU   ? impl_->velocity.x
                                     : component == kVelocityV ? impl_->velocity.y
                                                               : impl_->velocity.z;
  requireResident(source, nc, "the velocity field");
  DeviceBuffer<Real>& previous = component == kVelocityU   ? impl_->previousU
                                 : component == kVelocityV ? impl_->previousV
                                                           : impl_->previousW;
  carryFieldDevice(nc, source, previous);

  MomentumAssemblyOptions options;
  options.component = component;
  options.convectionScheme = convectionScheme;
  options.relaxationAlpha = relaxationAlpha;
  options.applyNonOrthogonalCorrection = applyNonOrthogonalCorrection;

  DeviceMomentumSystem& deviceSystem = component == kVelocityU   ? impl_->systemU
                                       : component == kVelocityV ? impl_->systemV
                                                                 : impl_->systemW;
  // The SAME call assembleMomentum makes. The only difference is what
  // follows it: nothing.
  assembleRelaxedMomentumDevice(impl_->momentum, impl_->velocity, impl_->pressure,
                                impl_->viscosity, impl_->massFlux, previous, nullptr, options,
                                deviceSystem);
}

cfd::algebra::SolverResult GpuSimpleDiscretization::solveMomentumResident(
    Index component, const cfd::algebra::LinearSolverSettings& settings) {
  const Index nc = impl_->cellCount;
  DeviceMomentumSystem& system = component == kVelocityU   ? impl_->systemU
                                 : component == kVelocityV ? impl_->systemV
                                                           : impl_->systemW;
  // Borrow the assembly's own CSR. Nothing is copied: the solver reads the
  // exact bytes the assembly kernel wrote.
  impl_->momentumMatrix.adoptDevice(system.rowOffsets.data(), system.columnIndices.data(),
                                    system.values.data(), nc, nc, system.values.size());

  impl_->krylov.resize(nc);
  copyToWorkspaceRhs(system.rhs, impl_->krylov);
  // The warm start. The host path passes `toVector(previousU)`, which is the
  // start-of-iteration component -- the same buffer the assembly above just
  // carried. SolverResult::initialResidual is measured from it and IS
  // SIMPLE's convergence measure, so starting anywhere else would change the
  // residual history even with an identical final answer.
  const DeviceBuffer<Real>& guess = component == kVelocityU   ? impl_->previousU
                                    : component == kVelocityV ? impl_->previousV
                                                              : impl_->previousW;
  copyToWorkspaceGuess(guess, impl_->krylov);

  cfd::algebra::SolverResult result =
      solveBiCGSTABResident(settings, impl_->momentumMatrix, impl_->krylov);
  // u* into the predictor the later stages already read from, device to
  // device. Copied unconditionally, exactly as the pressure stage does: on a
  // failed solve SIMPLE breaks out before any stage reads it.
  DeviceBuffer<Real>& target = component == kVelocityU   ? impl_->velocityStar.x
                               : component == kVelocityV ? impl_->velocityStar.y
                                                         : impl_->velocityStar.z;
  copyWorkspaceSolution(impl_->krylov, nc, target);
  return result;
}

bool GpuSimpleDiscretization::residentMomentumPredictorAllFinite(bool threeDimensional) {
  const Index nc = impl_->cellCount;
  if (nc == 0) return true;
  resetNonFiniteCounter(impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->velocityStar.x, impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->velocityStar.y, impl_->nonFiniteCounter);
  // W only in 3D -- see the header: on a 2D mesh nothing has written the W
  // predictor at this point in the iteration, and the host path's equivalent
  // value is an exact 0.0 that cannot be non-finite.
  if (threeDimensional)
    countNonFiniteDevice(nc, impl_->velocityStar.z, impl_->nonFiniteCounter);
  return readNonFiniteCounter(impl_->nonFiniteCounter);
}

bool GpuSimpleDiscretization::residentVelocityAllFinite() {
  const Index nc = impl_->cellCount;
  if (nc == 0) return true;
  // All three components, matching the host guard exactly: allFinite() on a
  // VectorField tests x, y and z whatever the mesh dimension, and the
  // resident velocity holds a real (zero) W on a 2D mesh.
  resetNonFiniteCounter(impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->velocity.x, impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->velocity.y, impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->velocity.z, impl_->nonFiniteCounter);
  return readNonFiniteCounter(impl_->nonFiniteCounter);
}

void GpuSimpleDiscretization::computeResponseCoefficients(bool threeDimensional) {
  if (!impl_->ready) return;
  // A 2D solve never writes velocityStar.z; SIMPLE's combineComponents leaves
  // it zero, so the device must hold zeros there rather than stale values.
  //
  // GPU-PIPE-001 final residency: written ON the device. It used to be a host
  // std::vector<Real> of zeros uploaded every 2D iteration -- a full-field
  // H2D the device can perform itself. +0.0 either way, so the stored bytes
  // are identical and every bitwise gate is unaffected.
  if (!threeDimensional) {
    fillFieldDevice(impl_->cellCount, 0.0, impl_->velocityStar.z);
  }
  requireResident(impl_->systemU.diagonal, impl_->cellCount, "the U momentum diagonal");
  requireResident(impl_->systemV.diagonal, impl_->cellCount, "the V momentum diagonal");
  if (threeDimensional)
    requireResident(impl_->systemW.diagonal, impl_->cellCount, "the W momentum diagonal");
  const DeviceMesh& mesh = impl_->momentum.convectionPlan().mesh();
  computeMomentumResponseCoefficientDevice(mesh, impl_->systemU.diagonal, impl_->responseU);
  computeMomentumResponseCoefficientDevice(mesh, impl_->systemV.diagonal, impl_->responseV);
  if (threeDimensional)
    computeMomentumResponseCoefficientDevice(mesh, impl_->systemW.diagonal, impl_->responseW);
}

void GpuSimpleDiscretization::computePredictedFaceFlux(bool rhieChow, Real density,
                                                       Real relaxationAlpha, Index gradientScheme,
                                                       bool threeDimensional) {
  if (!impl_->ready) return;
  requireResident(impl_->velocityStar.x, impl_->cellCount, "the U momentum predictor");
  requireResident(impl_->velocityStar.y, impl_->cellCount, "the V momentum predictor");
  requireResident(impl_->velocityStar.z, impl_->cellCount, "the W momentum predictor");
  requireResident(impl_->responseU, impl_->cellCount, "the U response coefficient");
  requireResident(impl_->responseV, impl_->cellCount, "the V response coefficient");
  if (threeDimensional)
    requireResident(impl_->responseW, impl_->cellCount, "the W response coefficient");
  if (!rhieChow) {
    calculateMassFluxDevice(impl_->faceFlux, impl_->velocityStar, density, impl_->predictorFlux);
    return;
  }
  // The gradient of the START-OF-ITERATION pressure with the PRESSURE boundary
  // conditions -- SIMPLE.cpp:487. Green-Gauss only: the pressure-source gradient
  // scheme is a separate setting and the least-squares path is reached through
  // the velocity correction, which owns its own plan.
  (void)gradientScheme;
  greenGaussGradientDevice(impl_->pressureGradient, impl_->pressure,
                           cfd::discretization::kGreenGaussSkewCorrectionSweeps, impl_->gradPx,
                           impl_->gradPy, impl_->gradPz);
  rhieChowMassFluxDevice(impl_->faceFlux, impl_->velocityStar, impl_->pressure, impl_->gradPx,
                         impl_->gradPy, impl_->gradPz, impl_->responseU, impl_->responseV,
                         threeDimensional ? &impl_->responseW : nullptr, density, relaxationAlpha,
                         impl_->predictorFlux);
}

cfd::algebra::LinearSystem GpuSimpleDiscretization::assemblePressureCorrection(
    bool nonOrthogonal, Index referenceCell, Real density,
    const cfd::fields::ScalarField* previousPressureCorrection, bool threeDimensional) {
  const Index nc = impl_->cellCount;
  if (previousPressureCorrection != nullptr)
    impl_->previousPPrime.uploadFrom(previousPressureCorrection->data(), nc);

  PressureCorrectionOptionsDevice options;
  options.nonOrthogonal = nonOrthogonal;
  options.referenceCell = referenceCell;
  options.density = density;
  assemblePressureCorrectionDevice(
      impl_->pressureCorrection, impl_->predictorFlux, impl_->responseU, impl_->responseV,
      threeDimensional ? &impl_->responseW : nullptr,
      previousPressureCorrection != nullptr ? &impl_->previousPPrime : nullptr, options,
      impl_->pressureSystem);

  return toHostSystem(nc, downloadIndices(impl_->pressureSystem.rowOffsets),
                      downloadIndices(impl_->pressureSystem.columnIndices),
                      download(impl_->pressureSystem.values),
                      download(impl_->pressureSystem.rhs));
}

void GpuSimpleDiscretization::assemblePressureCorrectionResident(bool nonOrthogonal,
                                                                 Index referenceCell, Real density,
                                                                 bool threeDimensional) {
  PressureCorrectionOptionsDevice options;
  options.nonOrthogonal = nonOrthogonal;
  options.referenceCell = referenceCell;
  options.density = density;
  // Identical to assemblePressureCorrection's call -- same kernels, same
  // arguments, same resulting device system. The ONLY difference is what
  // follows: no toHostSystem(), so nothing is downloaded.
  assemblePressureCorrectionDevice(impl_->pressureCorrection, impl_->predictorFlux,
                                   impl_->responseU, impl_->responseV,
                                   threeDimensional ? &impl_->responseW : nullptr, nullptr, options,
                                   impl_->pressureSystem);
}

cfd::algebra::SolverResult GpuSimpleDiscretization::solvePressureCorrectionResident(
    const cfd::algebra::LinearSolverSettings& settings) {
  const Index nc = impl_->cellCount;
  auto& system = impl_->pressureSystem;

  // Borrow the assembly's own CSR. Nothing is copied: the solver reads the
  // exact bytes the assembly kernel wrote.
  impl_->pressureMatrix.adoptDevice(system.rowOffsets.data(), system.columnIndices.data(),
                                    system.values.data(), nc, nc, system.values.size());

  impl_->krylov.resize(nc);
  copyToWorkspaceRhs(system.rhs, impl_->krylov);
  // The host path's LinearSolver::solve(system) supplies Vector(n, 0.0) as the
  // initial guess, so the resident path must start from the same all-zero
  // vector -- bit for bit, which fill() with 0.0 gives.
  fill(impl_->krylov.x, nc, 0.0);

  cfd::algebra::SolverResult result = solveBiCGSTABResident(settings, impl_->pressureMatrix,
                                                            impl_->krylov);
  // p' into the buffer the two corrections already read from, device to device.
  copyWorkspaceSolution(impl_->krylov, nc, impl_->pPrime);
  return result;
}

bool GpuSimpleDiscretization::residentPressureCorrectionAllFinite() {
  const Index nc = impl_->cellCount;
  if (nc == 0) return true;
  resetNonFiniteCounter(impl_->nonFiniteCounter);
  countNonFiniteDevice(nc, impl_->pPrime, impl_->nonFiniteCounter);
  // readNonFiniteCounter is already "all finite" (it returns count == 0), not
  // "any non-finite" -- same convention residentPressureAllFinite uses.
  return readNonFiniteCounter(impl_->nonFiniteCounter);
}

std::size_t GpuSimpleDiscretization::residentSolveBytes() const noexcept {
  return impl_->krylov.residentBytes();
}

void GpuSimpleDiscretization::setPressureCorrection(const cfd::algebra::Vector& pressureCorrection) {
  if (!impl_->ready) return;
  const Index nc = impl_->cellCount;
  std::vector<Real> host(static_cast<std::size_t>(nc));
  for (Index i = 0; i < nc; ++i) host[i] = pressureCorrection[i];
  impl_->pPrime.uploadFrom(host.data(), nc);
}

void GpuSimpleDiscretization::correctVelocity(Index gradientScheme, bool threeDimensional,
                                              cfd::fields::VectorField& corrected) {
  if (!impl_->ready) return;
  DeviceVelocity& result = impl_->correctedVelocity;
  correctVelocityDevice(impl_->velocityCorrection, impl_->velocityStar, impl_->responseU,
                        impl_->responseV, threeDimensional ? &impl_->responseW : nullptr,
                        impl_->pPrime, gradientScheme, impl_->correctionGradX,
                        impl_->correctionGradY, impl_->correctionGradZ, result);
  const Index nc = impl_->cellCount;
  const auto x = download(result.x);
  const auto y = download(result.y);
  const auto z = download(result.z);
  corrected = cfd::fields::VectorField(nc);
  for (Index c = 0; c < nc; ++c) corrected[c] = cfd::Vector3{x[c], y[c], z[c]};
}

void GpuSimpleDiscretization::correctVelocityResident(Index gradientScheme,
                                                      bool threeDimensional) {
  if (!impl_->ready) return;
  correctVelocityDevice(impl_->velocityCorrection, impl_->velocityStar, impl_->responseU,
                        impl_->responseV, threeDimensional ? &impl_->responseW : nullptr,
                        impl_->pPrime, gradientScheme, impl_->correctionGradX,
                        impl_->correctionGradY, impl_->correctionGradZ,
                        impl_->correctedVelocity);
  // Carry the correction into the persistent velocity. Safe here because
  // `velocity` (the start-of-iteration state) has no reader left this
  // iteration: the corrections are the last stage, and both of them read
  // `velocityStar` and `predictorFlux`, never `velocity`.
  const Index nc = impl_->cellCount;
  carryFieldDevice(nc, impl_->correctedVelocity.x, impl_->velocity.x);
  carryFieldDevice(nc, impl_->correctedVelocity.y, impl_->velocity.y);
  carryFieldDevice(nc, impl_->correctedVelocity.z, impl_->velocity.z);
  impl_->velocityAuthority = FieldAuthority::DeviceOwned;
}

void GpuSimpleDiscretization::correctFaceMassFluxResident(bool withExplicitTerm) {
  if (!impl_->ready) return;
  correctFaceMassFluxDevice(impl_->pressureCorrection.mesh(), impl_->predictorFlux,
                            impl_->pressureSystem.faceCoefficient, impl_->pPrime,
                            withExplicitTerm ? &impl_->pressureSystem.explicitFaceFlux : nullptr,
                            impl_->correctedFlux);
  carryFieldDevice(impl_->faceCount, impl_->correctedFlux, impl_->massFlux);
  impl_->massFluxAuthority = FieldAuthority::DeviceOwned;
}

bool GpuSimpleDiscretization::residentPressureAllFinite() {
  if (!impl_->ready) return true;
  // Only the PRESSURE. Velocity and mass flux are downloaded every iteration
  // anyway -- velocity because SIMPLE warm-starts the momentum solve from it,
  // the flux because evaluateContinuity needs the whole face field -- so SIMPLE
  // checks those on the host for free. Pressure is the one field with no host
  // copy during the solve, so it is the one that needs a device check.
  //
  // One persistent counter, reset on device, read back once: 0 allocations,
  // 0 H2D, one 4-byte D2H per iteration.
  resetNonFiniteCounter(impl_->nonFiniteCounter);
  countNonFiniteDevice(impl_->cellCount, impl_->pressure, impl_->nonFiniteCounter);
  return readNonFiniteCounter(impl_->nonFiniteCounter);
}

void GpuSimpleDiscretization::correctFaceMassFlux(bool withExplicitTerm,
                                                  cfd::fields::SurfaceField& corrected) {
  if (!impl_->ready) return;
  DeviceBuffer<Real>& result = impl_->correctedFlux;
  correctFaceMassFluxDevice(impl_->pressureCorrection.mesh(), impl_->predictorFlux,
                            impl_->pressureSystem.faceCoefficient, impl_->pPrime,
                            withExplicitTerm ? &impl_->pressureSystem.explicitFaceFlux : nullptr,
                            result);
  const auto host = download(result);
  corrected = cfd::fields::SurfaceField(impl_->faceCount);
  for (Index f = 0; f < impl_->faceCount; ++f) corrected[f] = host[f];
}

}  // namespace cfd::gpu
