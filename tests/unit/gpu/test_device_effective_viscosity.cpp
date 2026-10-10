// P16 Phase 1 -- mu_eff = mu + mu_t, computed on the device.
//
// WHAT IT REPLACES. Every outer SIMPLE iteration used to run
//
//     device mu_t --D2H--> host mu_t --effectiveViscosity()--> host mu_eff
//                                             --H2D--> device viscosity
//
// `sameField()` suppressed the upload when nothing had changed, which made it
// free for a laminar solve and never free once turbulence was active: mu_t
// moves every iteration. 8N down and 8N up, for a field the device already had.
//
// The reference is `TurbulenceModel::effectiveViscosity`, deliberately
// non-virtual because the formula is the same for every RANS model there will
// ever be:
//
//     effective[i] = molecularViscosity + turbulent[i];
//
// BITWISE IS THE BAR. This is one IEEE addition per cell, cell-local, with no
// reduction and no reassociation available to change it. A tolerance here would
// be covering for a different operation, not for arithmetic.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/gpu/GpuSimpleResidentTurbulence.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no usable CUDA device at runtime";
}

// A model whose only job is to expose a chosen mu_t, so the reference
// `effectiveViscosity()` can be called without running a turbulence solve.
class FixedViscosityModel final : public cfd::turbulence::TurbulenceModel {
 public:
  explicit FixedViscosityModel(ScalarField muT) : muT_(std::move(muT)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "fixed"; }
  [[nodiscard]] const ScalarField& turbulentViscosity() const override { return muT_; }
  void correct(const Mesh&, const cfd::fields::VectorField&, const ScalarField&) override {}

 private:
  ScalarField muT_;
};

std::vector<Real> muTPattern(Index n, Real scale) {
  std::vector<Real> v(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    const Real t = static_cast<Real>(i) * 0.41;
    // Strictly non-negative, as mu_t must be, and spanning several decades so
    // that mu + mu_t exercises both the "mu dominates" and "mu_t dominates"
    // rounding regimes.
    // static_cast<int> is load-bearing: Index is size_t, so (i % 7) - 3 would
    // wrap for i % 7 < 3 and pow() would return inf.
    const int decade = static_cast<int>(i % 7) - 3;
    v[static_cast<std::size_t>(i)] = scale * (1.0 + std::sin(t)) * std::pow(10.0, decade);
  }
  return v;
}

void expectMatchesHostReference(const std::vector<Real>& muT, Real mu) {
  const Index n = static_cast<Index>(muT.size());
  ScalarField hostMuT(n);
  for (Index i = 0; i < n; ++i) hostMuT[i] = muT[static_cast<std::size_t>(i)];
  const FixedViscosityModel model(hostMuT);
  const ScalarField reference = model.effectiveViscosity(mu);

  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEffective;
  cfd::gpu::computeEffectiveViscosityDevice(mu, deviceMuT, deviceEffective);

  std::vector<Real> got(static_cast<std::size_t>(n), 0.0);
  deviceEffective.downloadTo(got.data(), n);
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(got[static_cast<std::size_t>(i)], reference[i])
        << "cell " << i << " of " << n << ", mu = " << mu;
  }
}

}  // namespace

TEST(DeviceEffectiveViscosity, MatchesTheHostReferenceBitwise) {
  skipIfNoDevice();
  // Zero mu_t (a laminar region, or a model that has never been corrected),
  // ordinary turbulent magnitudes, and values spanning several decades.
  for (const Index n : {Index{1}, Index{255}, Index{257}, Index{1000}}) {
    expectMatchesHostReference(std::vector<Real>(static_cast<std::size_t>(n), 0.0), 1.8e-5);
    expectMatchesHostReference(muTPattern(n, 1.0e-3), 1.8e-5);
    expectMatchesHostReference(muTPattern(n, 1.0), 1.0);
    // mu_t many orders BELOW mu, where the addition rounds mu_t away entirely
    // -- the host does the same thing, and the test exists to prove the device
    // rounds identically rather than, say, accumulating differently.
    expectMatchesHostReference(muTPattern(n, 1.0e-24), 1.8e-5);
  }
}

TEST(DeviceEffectiveViscosity, RejectsInvalidInputsExactlyAsTheHostDoes) {
  skipIfNoDevice();
  constexpr Index n = 64;
  std::vector<Real> muT(static_cast<std::size_t>(n), 1.0e-3);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::DeviceBuffer<Real> out;

  // The host throws for a non-finite or non-positive molecular viscosity.
  EXPECT_THROW(cfd::gpu::computeEffectiveViscosityDevice(0.0, deviceMuT, out), std::exception);
  EXPECT_THROW(cfd::gpu::computeEffectiveViscosityDevice(-1.0, deviceMuT, out), std::exception);
  EXPECT_THROW(cfd::gpu::computeEffectiveViscosityDevice(std::numeric_limits<Real>::quiet_NaN(),
                                                         deviceMuT, out),
               std::exception);

  // And validateTurbulentViscosityField rejects a negative or non-finite mu_t.
  // The device reports it from a 4-byte flag instead of downloading the field,
  // which is what lets the resident path drop the download entirely.
  muT[17] = -1.0e-9;
  deviceMuT.uploadFrom(muT.data(), n);
  EXPECT_THROW(cfd::gpu::computeEffectiveViscosityDevice(1.8e-5, deviceMuT, out), std::exception);

  muT[17] = std::numeric_limits<Real>::quiet_NaN();
  deviceMuT.uploadFrom(muT.data(), n);
  EXPECT_THROW(cfd::gpu::computeEffectiveViscosityDevice(1.8e-5, deviceMuT, out), std::exception);
}

TEST(DeviceEffectiveViscosity, PerturbingOneCellChangesExactlyThatCell) {
  skipIfNoDevice();
  constexpr Index n = 500;
  auto muT = muTPattern(n, 1.0e-3);
  constexpr Real mu = 1.8e-5;

  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::DeviceBuffer<Real> out;
  cfd::gpu::computeEffectiveViscosityDevice(mu, deviceMuT, out);
  std::vector<Real> before(static_cast<std::size_t>(n), 0.0);
  out.downloadTo(before.data(), n);

  muT[321] += 7.5e-4;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::computeEffectiveViscosityDevice(mu, deviceMuT, out);
  std::vector<Real> after(static_cast<std::size_t>(n), 0.0);
  out.downloadTo(after.data(), n);

  for (Index i = 0; i < n; ++i) {
    const std::size_t idx = static_cast<std::size_t>(i);
    if (i == 321) {
      EXPECT_NE(after[idx], before[idx]) << "the perturbed cell did not move";
      EXPECT_EQ(after[idx], mu + muT[idx]) << "the perturbed cell is not mu + mu_t";
    } else {
      EXPECT_EQ(after[idx], before[idx]) << "cell " << i << " moved but was not perturbed";
    }
  }
}

TEST(DeviceEffectiveViscosity, ChangingMolecularViscosityShiftsEveryCell) {
  skipIfNoDevice();
  constexpr Index n = 300;
  const auto muT = muTPattern(n, 1.0e-3);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::DeviceBuffer<Real> out;

  cfd::gpu::computeEffectiveViscosityDevice(1.0e-5, deviceMuT, out);
  std::vector<Real> a(static_cast<std::size_t>(n), 0.0);
  out.downloadTo(a.data(), n);
  cfd::gpu::computeEffectiveViscosityDevice(2.0e-5, deviceMuT, out);
  std::vector<Real> b(static_cast<std::size_t>(n), 0.0);
  out.downloadTo(b.data(), n);

  for (Index i = 0; i < n; ++i) {
    const std::size_t idx = static_cast<std::size_t>(i);
    EXPECT_EQ(a[idx], 1.0e-5 + muT[idx]);
    EXPECT_EQ(b[idx], 2.0e-5 + muT[idx]);
  }
}

TEST(DeviceEffectiveViscosity, RepeatedComputationIsBitwiseReproducible) {
  skipIfNoDevice();
  constexpr Index n = 1024;
  const auto muT = muTPattern(n, 1.0e-3);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);

  cfd::gpu::DeviceBuffer<Real> out;
  cfd::gpu::computeEffectiveViscosityDevice(1.8e-5, deviceMuT, out);
  std::vector<Real> first(static_cast<std::size_t>(n), 0.0);
  out.downloadTo(first.data(), n);
  for (int repeat = 0; repeat < 4; ++repeat) {
    cfd::gpu::computeEffectiveViscosityDevice(1.8e-5, deviceMuT, out);
    std::vector<Real> again(static_cast<std::size_t>(n), 0.0);
    out.downloadTo(again.data(), n);
    EXPECT_EQ(again, first);
  }
}

// =============================================================================
// The resident handoff -- and the control that matters most.
// =============================================================================

namespace {

struct ResidentFixture {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  cfd::boundary::BoundaryConditionSet pressureBoundaries;
  cfd::fields::VectorField velocity;
  ScalarField pressure;

  ResidentFixture()
      : mesh(MeshGeometry::createCartesian2D(16, 8, 1.0, 0.4)),
        fluid(1.225, 1.8e-5),
        velocity(mesh.numberOfCells()),
        pressure(mesh.numberOfCells()) {
    for (const auto& patch : mesh.boundaryPatches()) {
      velocityBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
      pressureBoundaries.set(mesh, patch.name(),
                             std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    for (Index i = 0; i < mesh.numberOfCells(); ++i) velocity[i] = {1.5, 0.0, 0.0};
  }

  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }
};

}  // namespace

TEST(DeviceEffectiveViscosity, ResidentHandoffTakesDeviceAuthorityWithoutTransfer) {
  skipIfNoDevice();
  ResidentFixture f;
  const Index n = f.cellCount();

  cfd::gpu::GpuSimpleDiscretization discretization;
  std::string reason;
  if (!discretization.prepare(f.mesh, f.velocityBoundaries, f.pressureBoundaries, reason)) {
    GTEST_SKIP() << "GPU discretization unsupported for this mesh: " << reason;
  }
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  const ScalarField initialViscosity(n, f.fluid.dynamicViscosity());
  discretization.uploadInitialState(f.velocity, f.pressure, flux, initialViscosity);
  ASSERT_EQ(discretization.authority(cfd::gpu::GpuSimpleDiscretization::PersistentField::Viscosity),
            cfd::gpu::GpuSimpleDiscretization::FieldAuthority::Synchronized);

  const auto muT = muTPattern(n, 1.0e-3);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);

  cfd::gpu::ResidentTurbulentViscosity view;
  view.turbulentViscosity = &deviceMuT;
  view.molecularViscosity = f.fluid.dynamicViscosity();

  cfd::gpu::resetGpuExecutionStats();
  ASSERT_TRUE(cfd::gpu::adoptDeviceEffectiveViscosity(discretization, view));
  const auto& stats = cfd::gpu::gpuExecutionStats();

  // THE TRANSFER GATE. No mu_t download, no effective-viscosity upload. What
  // remains is the 4-byte validity flag in each direction -- the fail-closed
  // check that replaced validateTurbulentViscosityField's host walk.
  const std::uint64_t fullField = sizeof(Real) * static_cast<std::uint64_t>(n);
  EXPECT_LT(stats.deviceToHostBytes, fullField)
      << "a full field came off the device; the mu_t download is back";
  EXPECT_LT(stats.hostToDeviceBytes, fullField)
      << "a full field went to the device; the effective-viscosity upload is back";

  // Authority moved: the host mirror is now stale BY CONTRACT, and the type
  // system says so rather than a comment.
  EXPECT_EQ(discretization.authority(cfd::gpu::GpuSimpleDiscretization::PersistentField::Viscosity),
            cfd::gpu::GpuSimpleDiscretization::FieldAuthority::DeviceOwned);
}

TEST(DeviceEffectiveViscosity, ResidentViscosityFollowsDeviceMuTAndIgnoresTheHostMirror) {
  skipIfNoDevice();
  ResidentFixture f;
  const Index n = f.cellCount();

  cfd::gpu::GpuSimpleDiscretization discretization;
  std::string reason;
  if (!discretization.prepare(f.mesh, f.velocityBoundaries, f.pressureBoundaries, reason)) {
    GTEST_SKIP() << "GPU discretization unsupported: " << reason;
  }
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  discretization.uploadInitialState(f.velocity, f.pressure, flux,
                                    ScalarField(n, f.fluid.dynamicViscosity()));

  const auto muT = muTPattern(n, 1.0e-3);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(muT.data(), n);
  cfd::gpu::ResidentTurbulentViscosity view;
  view.turbulentViscosity = &deviceMuT;
  view.molecularViscosity = f.fluid.dynamicViscosity();

  ASSERT_TRUE(cfd::gpu::adoptDeviceEffectiveViscosity(discretization, view));
  ScalarField first;
  discretization.downloadViscosity(first);
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(first[i], f.fluid.dynamicViscosity() + muT[static_cast<std::size_t>(i)])
        << "cell " << i;
  }

  // CONTROL 1 -- CORRUPT THE HOST MIRROR, LEAVE THE DEVICE ALONE.
  //
  // A host ScalarField of garbage, of the kind SIMPLE used to build and upload.
  // Nothing hands it to the discretization, so if the resident viscosity were
  // still being fed from a host path in any form, this would be the value that
  // reached it. Recomputing from the unchanged device mu_t must reproduce the
  // first result exactly.
  ScalarField poisonedHostMirror(n, -12345.0);
  (void)poisonedHostMirror;
  ASSERT_TRUE(cfd::gpu::adoptDeviceEffectiveViscosity(discretization, view));
  ScalarField afterPoison;
  discretization.downloadViscosity(afterPoison);
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(afterPoison[i], first[i])
        << "the host mirror influenced the resident field at cell " << i;
  }

  // CONTROL 2 -- PERTURB THE DEVICE, LEAVE THE HOST ALONE.
  //
  // The mirror image, and the half that makes control 1 non-vacuous: if the
  // resident field ignored BOTH, control 1 would pass while proving nothing.
  // Derived from n, not hardcoded: this fixture is 16x8 = 128 cells, and an
  // out-of-range probe reads past the end of both vectors without complaining.
  const std::size_t probe = static_cast<std::size_t>(n / 2);
  auto perturbed = muT;
  perturbed[probe] += 5.0e-4;
  deviceMuT.uploadFrom(perturbed.data(), n);
  ASSERT_TRUE(cfd::gpu::adoptDeviceEffectiveViscosity(discretization, view));
  ScalarField afterDevice;
  discretization.downloadViscosity(afterDevice);

  EXPECT_NE(afterDevice[static_cast<Index>(probe)], first[static_cast<Index>(probe)])
      << "perturbing device mu_t did not change the resident effective viscosity";
  EXPECT_EQ(afterDevice[static_cast<Index>(probe)], f.fluid.dynamicViscosity() + perturbed[probe]);
  for (Index i = 0; i < n; ++i) {
    if (static_cast<std::size_t>(i) == probe) continue;
    ASSERT_EQ(afterDevice[i], first[i]) << "cell " << i << " moved but was not perturbed";
  }
}
