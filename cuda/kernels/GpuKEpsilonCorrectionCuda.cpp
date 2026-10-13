// P16-GPU-TURB Stage 7D -- the real implementation behind the CUDA-free facade.
//
// Forwards to `correctKEpsilonDevice`, which is the same implementation the
// Stage 6/7 equivalence tests exercise. That is deliberate: if production and
// the tests called different code, the equivalence those tests establish would
// say nothing about what ships.
//
// Compiled only when CFDAPP_ENABLE_CUDA is ON; src/gpu/GpuKEpsilonCorrection.cpp
// is the stub for the other configuration.

#include "cfd/gpu/GpuKEpsilonCorrection.hpp"

#include <limits>
#include <vector>

#include <cmath>
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/gpu/DeviceKEpsilon.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuSimpleResidentTurbulence.hpp"
#include "cfd/gpu/TurbulenceBackendTrace.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

struct GpuKEpsilonCorrection::Impl {
  bool prepared{false};
  std::string unsupportedReason;

  // TWO plan pairs. See the header: sharing them applies k's boundary values
  // to epsilon, which Stage 6 measured as a 23.5% epsilon error.
  DeviceDiffusionPlan kDiffusion;
  DeviceConvectionPlan kConvection;
  DeviceDiffusionPlan epsilonDiffusion;
  DeviceConvectionPlan epsilonConvection;

  // Resident turbulence state, including the two persistent Krylov
  // workspaces. Living here across corrections is what makes steady-state
  // allocations zero.
  DeviceKEpsilonState state;

  // Per-correction scratch that is resident but not state.
  DeviceBuffer<Real> cellVolumes;
  DeviceBuffer<Real> massFlux;
  // P16 Step 4 -- the TURBULENCE convection flux, rebuilt on device from the
  // resident velocity. Persistent, so a steady-state correction still
  // allocates nothing.
  DeviceBuffer<Real> turbulenceFlux;
  DeviceVelocityGradient velocityGradient;
  DeviceWallTreatment wall;
  DeviceKEpsilonCorrection correction;

  // Non-null only for the duration of a correctResident() call: the BORROWED
  // resident flux. runCorrection prefers it over the locally-uploaded one, so
  // the resident path never recomputes or re-uploads a flux the device already
  // holds. Cleared immediately afterwards so a stale borrow cannot survive
  // into a later non-resident call.
  const DeviceBuffer<Real>* residentMassFlux{nullptr};

  // Stage 7I step 3: the compact wall exchange. `wallOwners` is the wall-owner
  // cell list in WALL-FACE ORDER -- the order the sequential corner rule
  // depends on -- uploaded once per correction because the owner list is
  // geometry and does not change, but the values gathered through it do.
  DeviceBuffer<Index> wallOwners;
  DeviceBuffer<Real> wallOwnerU;
  DeviceBuffer<Real> wallOwnerV;
  DeviceBuffer<Real> wallOwnerK;
};

GpuKEpsilonCorrection::GpuKEpsilonCorrection() : impl_(std::make_unique<Impl>()) {}
GpuKEpsilonCorrection::~GpuKEpsilonCorrection() = default;
GpuKEpsilonCorrection::GpuKEpsilonCorrection(GpuKEpsilonCorrection&&) noexcept = default;
GpuKEpsilonCorrection& GpuKEpsilonCorrection::operator=(GpuKEpsilonCorrection&&) noexcept = default;

bool GpuKEpsilonCorrection::available() noexcept { return cudaAvailable(); }

bool GpuKEpsilonCorrection::prepare(const cfd::mesh::Mesh& mesh,
                                    const cfd::boundary::BoundaryConditionSet& kBoundaries,
                                    const cfd::boundary::BoundaryConditionSet& epsilonBoundaries,
                                    const cfd::fields::ScalarField& k,
                                    const cfd::fields::ScalarField& epsilon,
                                    const cfd::fields::ScalarField& turbulentViscosity) {
  impl_->prepared = false;
  impl_->unsupportedReason.clear();
  if (!cudaAvailable()) {
    impl_->unsupportedReason = "no usable CUDA device at runtime";
    return false;
  }

  if (!impl_->kDiffusion.build(mesh, kBoundaries)) {
    impl_->unsupportedReason = "k diffusion plan: " + impl_->kDiffusion.unsupportedReason();
    return false;
  }
  if (!impl_->kConvection.build(mesh, kBoundaries)) {
    impl_->unsupportedReason = "k convection plan: " + impl_->kConvection.unsupportedReason();
    return false;
  }
  if (!impl_->epsilonDiffusion.build(mesh, epsilonBoundaries)) {
    impl_->unsupportedReason =
        "epsilon diffusion plan: " + impl_->epsilonDiffusion.unsupportedReason();
    return false;
  }
  if (!impl_->epsilonConvection.build(mesh, epsilonBoundaries)) {
    impl_->unsupportedReason =
        "epsilon convection plan: " + impl_->epsilonConvection.unsupportedReason();
    return false;
  }

  const Index n = mesh.numberOfCells();
  impl_->state.k.uploadFrom(k.data(), n);
  impl_->state.epsilon.uploadFrom(epsilon.data(), n);
  impl_->state.turbulentViscosity.uploadFrom(turbulentViscosity.data(), n);

  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = mesh.cell(i).volume();
  impl_->cellVolumes.uploadFrom(volumes.data(), n);

  impl_->prepared = true;
  return true;
}

bool GpuKEpsilonCorrection::prepared() const noexcept { return impl_->prepared; }

const std::string& GpuKEpsilonCorrection::unsupportedReason() const noexcept {
  return impl_->unsupportedReason;
}

void GpuKEpsilonCorrection::correct(const cfd::mesh::Mesh& mesh,
                                    const cfd::fields::VectorField& velocity,
                                    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                    const cfd::physics::FluidProperties& fluid,
                                    const cfd::turbulence::KEpsilonConfig& config,
                                    const GpuKEpsilonWallInputs& wall, GpuKEpsilonResult& result) {
  if (!impl_->prepared) {
    throw cfd::InvalidArgumentError("GpuKEpsilonCorrection::correct: prepare() was not called");
  }
  const Index n = mesh.numberOfCells();

  // The mass flux and the velocity gradient come from the SAME host functions
  // the CPU model calls. They are not re-derived here: the gradient is the
  // GreenGauss operator `computeVelocityGradientDevice` already ports, and
  // using a different one would change the discretization rather than the
  // backend.
  const auto hostMassFlux =
      cfd::physics::calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const auto hostGradient =
      cfd::discretization::computeVelocityGradient(mesh, velocity, velocityBoundaries);

  impl_->massFlux.uploadFrom(hostMassFlux.data(), mesh.numberOfFaces());
  std::vector<Real> gx(static_cast<std::size_t>(n)), gy(static_cast<std::size_t>(n)),
      hx(static_cast<std::size_t>(n)), hy(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    gx[static_cast<std::size_t>(i)] = hostGradient.gradU[i].x;
    gy[static_cast<std::size_t>(i)] = hostGradient.gradU[i].y;
    hx[static_cast<std::size_t>(i)] = hostGradient.gradV[i].x;
    hy[static_cast<std::size_t>(i)] = hostGradient.gradV[i].y;
  }
  impl_->velocityGradient.gradUx.uploadFrom(gx.data(), n);
  impl_->velocityGradient.gradUy.uploadFrom(gy.data(), n);
  impl_->velocityGradient.gradVx.uploadFrom(hx.data(), n);
  impl_->velocityGradient.gradVy.uploadFrom(hy.data(), n);

  runCorrection(mesh, fluid, config, wall, result);
}

// P16-GPU-TURB Stage 7I step 2 -- the RESIDENT entry point.
//
// Everything after the gradient and the flux is IDENTICAL to correct(): both
// call runCorrection below, which calls the one qualified
// correctKEpsilonDevice. The two entry points differ only in where those two
// device buffers come from.
void GpuKEpsilonCorrection::correctResident(const ResidentTurbulenceInputs& inputs,
                                            const cfd::mesh::Mesh& mesh,
                                            const cfd::physics::FluidProperties& fluid,
                                            const cfd::turbulence::KEpsilonConfig& config,
                                            const GpuKEpsilonWallInputs& wall,
                                            GpuKEpsilonResult& result) {
  if (!impl_->prepared) {
    throw cfd::InvalidArgumentError(
        "GpuKEpsilonCorrection::correctResident: prepare() was not called");
  }
  if (!inputs.usable()) {
    throw cfd::InvalidArgumentError(
        "GpuKEpsilonCorrection::correctResident: the resident view is not usable -- the "
        "discretization has no resident state yet");
  }
  const Index n = mesh.numberOfCells();
  if (inputs.cellCount != n) {
    throw cfd::InvalidArgumentError(
        "GpuKEpsilonCorrection::correctResident: the resident view is sized for a different mesh");
  }

  // THE DEVICE GRADIENT. The already-qualified GreenGauss port, applied to the
  // resident corrected velocity from the end of iteration N-1 using the plan
  // the resident loop already built. No host VectorField exists anywhere in
  // this call.
  computeVelocityGradientDevice(*inputs.convectionPlan, *inputs.velocity, impl_->velocityGradient);

  // THE RESIDENT CORRECTED FLUX, ADOPTED not recomputed. `Impl::massFlux` is
  // documented as "carried between iterations, not the predictor", so it is
  // the same flux state the CPU turbulence correction receives. Recomputing it
  // from velocity would be a different number (the corrected flux is not the
  // divergence-free reconstruction of the corrected velocity), and downloading
  // and re-uploading it would defeat the whole point.
  // P16 Step 4 -- THE TURBULENCE FLUX IS REBUILT, NOT BORROWED.
  //
  // `inputs.massFlux` is SIMPLE's carried CORRECTED face flux. The CPU
  // reference does not use it: KEpsilonModel::correctOnHost calls
  // calculateMassFlux(mesh, velocity, ...) and builds its own from the cell
  // velocity. The two differ by the Rhie-Chow face correction -- 3.231e-06
  // relative on the validated channel, which propagated to k and mu_t at
  // 2-4e-06 and failed the cross-backend continuation gate.
  //
  // calculateMassFluxDevice is the device port of that same host routine, so
  // this builds the SAME quantity from the SAME velocity. No download, no host
  // call, no new kernel.
  //
  // Whether turbulence SHOULD consume the conservative carried flux instead is
  // a real numerical question, recorded separately; it would be a change to the
  // CPU reference and is not part of an equivalence fix.
  calculateMassFluxDevice(*inputs.fluxPlan, *inputs.velocity, fluid.density(),
                          impl_->turbulenceFlux);
  impl_->residentMassFlux = &impl_->turbulenceFlux;

  runCorrection(mesh, fluid, config, wall, result);
  impl_->residentMassFlux = nullptr;
}

// The shared tail. Reached identically from both entry points, so a semantic
// added to one cannot be missing from the other.
void GpuKEpsilonCorrection::runCorrection(const cfd::mesh::Mesh& mesh,
                                          const cfd::physics::FluidProperties& fluid,
                                          const cfd::turbulence::KEpsilonConfig& config,
                                          const GpuKEpsilonWallInputs& wall,
                                          GpuKEpsilonResult& result) {
  const Index n = mesh.numberOfCells();
  // The wall treatment's COMPACT outputs, O(wall cells). No turbulence field
  // is downloaded to produce these -- the caller evaluated the wall law from
  // the k it already holds.
  const bool wallActive = wall.wallCells != nullptr && !wall.wallCells->empty();
  if (wallActive) {
    const Index count = static_cast<Index>(wall.wallCells->size());
    impl_->wall.wallCells.uploadFrom(wall.wallCells->data(), count);
    impl_->wall.wallCellProduction.uploadFrom(wall.wallCellProduction->data(), count);
    uploadImposedCellsDevice(*wall.wallCells, *wall.wallCellDissipation, n,
                             impl_->wall.imposedEpsilon);
  } else {
    impl_->wall.wallCells.resize(0);
    impl_->wall.wallCellProduction.resize(0);
    impl_->wall.imposedEpsilon.cells.resize(0);
    impl_->wall.imposedEpsilon.values.resize(0);
  }

  DeviceKEpsilonConfig deviceConfig;
  deviceConfig.density = fluid.density();
  deviceConfig.dynamicViscosity = fluid.dynamicViscosity();
  deviceConfig.cMu = config.coefficients.cMu;
  deviceConfig.c1Epsilon = config.coefficients.c1Epsilon;
  deviceConfig.c2Epsilon = config.coefficients.c2Epsilon;
  deviceConfig.sigmaK = config.coefficients.sigmaK;
  deviceConfig.sigmaEpsilon = config.coefficients.sigmaEpsilon;
  deviceConfig.kFloor = config.kFloor;
  deviceConfig.epsilonFloor = config.epsilonFloor;
  deviceConfig.kRelaxation = config.kRelaxation;
  deviceConfig.epsilonRelaxation = config.epsilonRelaxation;
  deviceConfig.kSolver = config.kSolver;
  deviceConfig.epsilonSolver = config.epsilonSolver;
  deviceConfig.nonOrthogonal = config.nonOrthogonal.enabled;

  DeviceKEpsilonPlans plans;
  plans.kDiffusion = &impl_->kDiffusion;
  plans.kConvection = &impl_->kConvection;
  plans.epsilonDiffusion = &impl_->epsilonDiffusion;
  plans.epsilonConvection = &impl_->epsilonConvection;

  // THE SAME implementation the equivalence tests drive.
  const DeviceBuffer<Real>& massFluxForAssembly =
      impl_->residentMassFlux != nullptr ? *impl_->residentMassFlux : impl_->massFlux;
  correctKEpsilonDevice(plans, impl_->cellVolumes, impl_->velocityGradient, massFluxForAssembly,
                        impl_->wall, deviceConfig, impl_->state, impl_->correction);

  // The three fields come down because TurbulenceModel's interface publishes
  // host ScalarFields. That is an interface fact, not a residency defect in
  // this path -- the CSR, RHS and solution never left the device. Making the
  // fields stay resident too would need the model to expose device handles,
  // which is a larger change than P16 authorizes.
  result.k = cfd::fields::ScalarField(n);
  result.epsilon = cfd::fields::ScalarField(n);
  result.turbulentViscosity = cfd::fields::ScalarField(n);
  impl_->state.k.downloadTo(result.k.data(), n);
  impl_->state.epsilon.downloadTo(result.epsilon.data(), n);
  impl_->state.turbulentViscosity.downloadTo(result.turbulentViscosity.data(), n);
  result.convergenceResidual = impl_->correction.convergenceResidual;
  result.kIterations = impl_->correction.kSolve.iterations;
  result.epsilonIterations = impl_->correction.epsilonSolve.iterations;
  result.kResidual = impl_->correction.kSolve.residual;
  result.epsilonResidual = impl_->correction.epsilonSolve.residual;
}

// P16 Step 4 -- the production seam. See the header for the ordering it
// preserves and for why the host velocity is never read.
bool GpuKEpsilonCorrection::correctFromResidentDiscretization(
    GpuSimpleDiscretization& discretization, const cfd::mesh::Mesh& mesh,
    const cfd::physics::FluidProperties& fluid, const cfd::turbulence::KEpsilonConfig& config,
    const std::vector<cfd::turbulence::WallFace>& wallFaces,
    cfd::turbulence::WallTreatment& treatmentOut, GpuKEpsilonResult& result) {
  if (!impl_->prepared) {
    throw cfd::InvalidArgumentError(
        "correctFromResidentDiscretization: prepare() was not called");
  }
  const ResidentTurbulenceInputs inputs = residentTurbulenceInputs(discretization);
  if (!inputs.usable()) return false;

  // The already-qualified resident correction, unchanged. It borrows U(N-1)
  // and REBUILDS its own convection flux from it, matching the CPU reference,
  // which does the same; it does NOT consume SIMPLE's carried corrected flux.
  // It owns the old-state lag, the compact wall exchange and the atomic
  // publication.
  correctResidentWithWallTreatment(inputs, mesh, fluid, config, wallFaces, treatmentOut, result);

  // mu_eff = mu + mu_t, on device, straight into the buffer the momentum
  // assembly reads. The host mu_t mirror is not consulted.
  ResidentTurbulentViscosity viscosityView;
  viscosityView.turbulentViscosity = &impl_->state.turbulentViscosity;
  viscosityView.molecularViscosity = fluid.dynamicViscosity();
  if (!adoptDeviceEffectiveViscosity(discretization, viscosityView)) return false;

  // The wall function's FACE-local viscosity, compacted. treatmentOut.wallViscosity
  // is a face-length host SurfaceField that is zero except at wall faces, so
  // only the wall faces are sent -- O(N_wall), never a face-length upload.
  //
  // The face ORDER is `wallFaces`'s own, untouched, and the `!= 0.0` convention
  // is preserved by sending only non-zero entries: a wall face whose treatment
  // produced exactly 0.0 means "no override" on the host and is therefore
  // omitted here, which is the same thing.
  std::vector<cfd::Index> overrideFaces;
  std::vector<cfd::Real> overrideValues;
  overrideFaces.reserve(wallFaces.size());
  overrideValues.reserve(wallFaces.size());
  for (const auto& wallFace : wallFaces) {
    const cfd::Index faceId = wallFace.faceId;
    if (faceId >= treatmentOut.wallViscosity.size()) continue;
    const cfd::Real value = treatmentOut.wallViscosity[faceId];
    if (value == 0.0) continue;
    overrideFaces.push_back(faceId);
    overrideValues.push_back(value);
  }
  // setWallFaceViscosity zero-fills before scattering, so last iteration's
  // overrides cannot survive into this one; an empty list clears them outright.
  discretization.setWallFaceViscosity(overrideFaces, overrideValues);
  return true;
}

void GpuKEpsilonCorrection::correctResidentWithWallTreatment(
    const ResidentTurbulenceInputs& inputs, const cfd::mesh::Mesh& mesh,
    const cfd::physics::FluidProperties& fluid, const cfd::turbulence::KEpsilonConfig& config,
    const std::vector<cfd::turbulence::WallFace>& wallFaces,
    cfd::turbulence::WallTreatment& treatmentOut, GpuKEpsilonResult& result) {
  if (!impl_->prepared) {
    throw cfd::InvalidArgumentError("correctResidentWithWallTreatment: prepare() was not called");
  }
  if (!inputs.usable()) {
    throw cfd::InvalidArgumentError(
        "correctResidentWithWallTreatment: the resident view is not usable");
  }
  const Index n = mesh.numberOfCells();

  // --- the compact gather, O(wall faces) --------------------------------
  //
  // The owner list is in WALL-FACE ORDER and is NOT sorted or deduplicated: a
  // cell touching two wall faces appears twice here, exactly as it appears
  // twice in `wallFaces`, because the corner rule resolves that duplication
  // itself by nearest distance and first claim. Deduplicating here would
  // decide it differently and silently.
  const Index wallCount = static_cast<Index>(wallFaces.size());
  std::vector<Index> owners(static_cast<std::size_t>(wallCount));
  for (Index i = 0; i < wallCount; ++i) {
    owners[static_cast<std::size_t>(i)] = wallFaces[static_cast<std::size_t>(i)].ownerCell;
  }
  impl_->wallOwners.uploadFrom(owners.data(), wallCount);

  gatherByIndexDevice(impl_->wallOwners, inputs.velocity->x, impl_->wallOwnerU);
  gatherByIndexDevice(impl_->wallOwners, inputs.velocity->y, impl_->wallOwnerV);
  gatherByIndexDevice(impl_->wallOwners, impl_->state.k, impl_->wallOwnerK);

  std::vector<Real> hostU(static_cast<std::size_t>(wallCount));
  std::vector<Real> hostV(static_cast<std::size_t>(wallCount));
  std::vector<Real> hostK(static_cast<std::size_t>(wallCount));
  if (wallCount > 0) {
    impl_->wallOwnerU.downloadTo(hostU.data(), wallCount);
    impl_->wallOwnerV.downloadTo(hostV.data(), wallCount);
    impl_->wallOwnerK.downloadTo(hostK.data(), wallCount);
  }

  // --- the sparse host view, POISONED outside the wall cells -------------
  //
  // Full length because evaluateWallFace size-checks against the cell count;
  // NaN everywhere the audit says is never read, so a stray read is a loud
  // failure rather than a plausible number.
  const Real poison = std::numeric_limits<Real>::quiet_NaN();
  cfd::fields::VectorField sparseVelocity(n, cfd::Vector2{poison, poison});
  cfd::fields::ScalarField sparseK(n, poison);
  for (Index i = 0; i < wallCount; ++i) {
    const Index owner = owners[static_cast<std::size_t>(i)];
    sparseVelocity[owner] = {hostU[static_cast<std::size_t>(i)], hostV[static_cast<std::size_t>(i)],
                             0.0};
    sparseK[owner] = hostK[static_cast<std::size_t>(i)];
  }

  // --- the production wall law, UNCHANGED --------------------------------
  treatmentOut = cfd::turbulence::computeWallTreatment(mesh, wallFaces, sparseVelocity, sparseK,
                                                       fluid.density(), fluid.dynamicViscosity(),
                                                       config.wallFunctionCoefficients);
  // P16 Step 4. RECORDED, on the host, deliberately.
  //
  // TurbulenceOperation::WallTreatment existed but was never recorded ANYWHERE
  // in the codebase -- only its name was defined -- so a provenance gate
  // demanding "WallTreatment = Cpu" could not have passed on any path, and one
  // demanding Gpu would have failed for the right answer with the wrong
  // evidence. The trace's own rule is that NotAttempted is as bad as Cpu for a
  // gate wanting Gpu, which cuts both ways: an operation that genuinely ran on
  // the host has to say so.
  turbulenceBackendTrace().record(TurbulenceOperation::WallTreatment, ExecutionBackend::Cpu);

  // --- the compact scatter, O(wall cells) --------------------------------
  GpuKEpsilonWallInputs wallInputs;
  wallInputs.wallCells = &treatmentOut.wallCells;
  wallInputs.wallCellProduction = &treatmentOut.wallCellProduction;
  wallInputs.wallCellDissipation = &treatmentOut.wallCellDissipation;

  computeVelocityGradientDevice(*inputs.convectionPlan, *inputs.velocity, impl_->velocityGradient);
  // P16 Step 4 -- THE TURBULENCE FLUX IS REBUILT, NOT BORROWED.
  //
  // `inputs.massFlux` is SIMPLE's carried CORRECTED face flux. The CPU
  // reference does not use it: KEpsilonModel::correctOnHost calls
  // calculateMassFlux(mesh, velocity, ...) and builds its own from the cell
  // velocity. The two differ by the Rhie-Chow face correction -- 3.231e-06
  // relative on the validated channel, which propagated to k and mu_t at
  // 2-4e-06 and failed the cross-backend continuation gate.
  //
  // calculateMassFluxDevice is the device port of that same host routine, so
  // this builds the SAME quantity from the SAME velocity. No download, no host
  // call, no new kernel.
  //
  // Whether turbulence SHOULD consume the conservative carried flux instead is
  // a real numerical question, recorded separately; it would be a change to the
  // CPU reference and is not part of an equivalence fix.
  calculateMassFluxDevice(*inputs.fluxPlan, *inputs.velocity, fluid.density(),
                          impl_->turbulenceFlux);
  impl_->residentMassFlux = &impl_->turbulenceFlux;
  runCorrection(mesh, fluid, config, wallInputs, result);
  impl_->residentMassFlux = nullptr;
}

}  // namespace cfd::gpu
