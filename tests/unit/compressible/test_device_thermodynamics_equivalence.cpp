// P17.10 -- the cross-comparison authorization §4 requires for a restated formula.
//
// include/cfd/gpu/DeviceThermodynamics.hpp restates, for the device, the handful of closed forms
// that cannot cross because TotalEnergyState owns a std::vector-backed property model. §4 permits a
// separate implementation "if a direct cross-comparison test is added". This is that test, and what
// it demands is BITWISE agreement -- not closeness -- because the two are supposed to be the same
// arithmetic in the same order.
//
// WHY THIS FILE IS NOT UNDER tests/unit/gpu. The shared header uses CFD_HLLC_HOST_DEVICE, which is
// empty under gcc, so it compiles into the CPU build unchanged. Putting the test here means it runs
// in the CPU-ONLY configuration too -- so the restatement is checked against production on a
// machine with no GPU, and a CUDA-OFF regression cannot silently stop covering it. That matters:
// this project has a recorded finding (CPU-only build blind spot) about code that no routine gate
// compiled.
//
// WHAT THIS FILE DOES NOT PROVE. It compares production against the shared header COMPILED FOR THE
// HOST. It says nothing about whether nvcc's DEVICE code agrees with nvcc's host code -- that is
// the other half of the chain, measured separately in
// tests/unit/gpu/test_density_based_gpu_equivalence.cpp. A single end-to-end comparison would
// conflate the two and, on a failure, name neither.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/DeviceThermodynamics.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::BoundaryKind;
using cfd::compressible::DensityBasedBoundary;
using cfd::compressible::exteriorState;
using cfd::compressible::makePrimitiveState;
using cfd::compressible::PrimitiveState;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::gpu::DeviceBoundaryKind;
using cfd::gpu::DeviceControls;
using cfd::gpu::deviceExteriorState;
using cfd::gpu::deviceRecoverPrimitive;
using cfd::gpu::DeviceRecoveryStatus;
using cfd::gpu::deviceSpeedOfSound;
using cfd::gpu::DeviceThermodynamics;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

std::uint64_t bitsOf(Real v) {
  std::uint64_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

#define EXPECT_BITWISE(a, b, what)                                                          \
  EXPECT_EQ(bitsOf(a), bitsOf(b)) << (what) << ": cpu=" << (a) << " device=" << (b) << " (" \
                                  << (bitsOf(a) ^ bitsOf(b)) << " bits differ)"

// Two gases: the zero-reference convention the P17.7/P17.8 suites use, and a NONZERO reference,
// because the recovery's whole subtlety is the h_ref / T_ref terms. A test that only ever ran with
// h_ref = T_ref = 0 would pass with those terms deleted.
TotalEnergyState zeroReferenceAir() {
  const TemperatureDependentProperty cp(std::vector<Real>{1005.0}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(287.05, 1005.0), SensibleEnthalpy(cp, 0.0, 0.0));
}

TotalEnergyState nonZeroReferenceAir() {
  const TemperatureDependentProperty cp(std::vector<Real>{1005.0}, 200.0, 2500.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(287.05, 1005.0),
                          SensibleEnthalpy(cp, 298.15, 2.5e5));
}

// The nine scalars, built exactly as DeviceDensityBasedSolver::upload() builds them -- including
// eFloor from the PRODUCTION function, so the constant that is shipped is the one the CPU uses.
DeviceThermodynamics pack(const TotalEnergyState& t) {
  const auto& properties = t.thermodynamics();
  const auto& enthalpy = t.enthalpyModel();
  const auto& cpModel = enthalpy.specificHeatModel();
  DeviceThermodynamics d{};
  d.gasConstant = properties.gasConstant();
  d.specificHeatP = properties.specificHeatPressure();
  d.specificHeatV = properties.specificHeatVolume();
  d.specificHeatRatio = properties.specificHeatRatio();
  d.referenceT = enthalpy.referenceTemperature();
  d.referenceH = enthalpy.referenceEnthalpy();
  d.minimumT = cpModel.minimumTemperature();
  d.maximumT = cpModel.maximumTemperature();
  d.internalEnergyFloor = t.specificInternalEnergy(d.minimumT);
  return d;
}

struct StateCase {
  const char* name;
  Real density;
  Vector3 velocity;
  Real temperature;
};

// Chosen to span the regimes the solver actually visits, not to be convenient: stationary, low
// Mach, transonic, supersonic, strongly heated, near-vacuum density, and fully three-dimensional
// velocity so no component is accidentally zero everywhere.
const std::vector<StateCase>& stateMatrix() {
  static const std::vector<StateCase> cases{
      {"still_ambient", 1.225, Vector3{0.0, 0.0, 0.0}, 288.15},
      {"low_mach", 1.2, Vector3{3.0, 0.0, 0.0}, 300.0},
      {"transonic", 1.0, Vector3{340.0, 12.0, 0.0}, 290.0},
      {"supersonic_x", 0.8, Vector3{700.0, 0.0, 0.0}, 300.0},
      {"supersonic_3d", 0.55, Vector3{620.0, -180.0, 95.0}, 420.0},
      {"hot", 0.35, Vector3{120.0, 40.0, -20.0}, 1400.0},
      {"cold_dense", 3.9, Vector3{-55.0, 8.0, 2.0}, 240.0},
      {"thin", 0.02, Vector3{900.0, 15.0, -3.0}, 260.0},
      {"sod_left", 1.0, Vector3{0.0, 0.0, 0.0}, 348.432055749129},
      {"sod_right", 0.125, Vector3{0.0, 0.0, 0.0}, 278.745644599303},
  };
  return cases;
}

}  // namespace

// =================================================================================================
// A1 / A3 / A4 -- the recovery, bitwise against TotalEnergyState::recover()
// =================================================================================================

TEST(DeviceThermodynamicsEquivalence, A1_RecoveryIsBitwiseIdenticalToProduction) {
  for (const bool nonZeroReference : {false, true}) {
    const TotalEnergyState thermo = nonZeroReference ? nonZeroReferenceAir() : zeroReferenceAir();
    const DeviceThermodynamics packed = pack(thermo);
    const DeviceControls production{};
    int compared = 0;

    for (const auto& c : stateMatrix()) {
      // Build the conserved state through the production forward path, so the inputs are states the
      // solver could actually hold rather than numbers invented for the test.
      const PrimitiveState forward =
          makePrimitiveState(thermo, c.density, c.velocity, c.temperature);
      const Vector3 momentum{c.density * c.velocity.x, c.density * c.velocity.y,
                             c.density * c.velocity.z};
      const Real rhoE = forward.totalEnergyDensity;

      const auto cpu = thermo.recover(c.density, momentum, rhoE);
      PrimitiveState device{};
      const DeviceRecoveryStatus st =
          deviceRecoverPrimitive(packed, c.density, momentum, rhoE, &device, production);

      ASSERT_EQ(st, DeviceRecoveryStatus::Ok)
          << c.name << " (nonZeroReference=" << nonZeroReference
          << "): production accepted this state and the device form refused it";

      EXPECT_BITWISE(cpu.velocity.x, device.velocity.x, c.name);
      EXPECT_BITWISE(cpu.velocity.y, device.velocity.y, c.name);
      EXPECT_BITWISE(cpu.velocity.z, device.velocity.z, c.name);
      EXPECT_BITWISE(cpu.temperature, device.temperature, c.name);
      EXPECT_BITWISE(cpu.pressure, device.pressure, c.name);
      // a comes from P17.2's speedOfSound, which the device form restates.
      EXPECT_BITWISE(thermo.thermodynamics().speedOfSound(cpu.temperature), device.speedOfSound,
                     c.name);
      compared += 6;
    }
    EXPECT_EQ(compared, 6 * static_cast<int>(stateMatrix().size()))
        << "not every case was compared";
  }
}

// THE PARENTHESISATION CONTROL, declared in the frozen gate before any of this was measured.
// (rho*R)*T and rho*(R*T) are the same number in exact arithmetic. If A1 cannot tell them apart,
// A1 is not measuring operand order and its "bitwise" claim is decoration.
TEST(DeviceThermodynamicsEquivalence, A1_NonVacuity_RegroupingTheProductsBreaksBitwiseEquality) {
  const TotalEnergyState thermo = nonZeroReferenceAir();
  const DeviceThermodynamics packed = pack(thermo);
  DeviceControls regrouped{};
  regrouped.regroupProducts = true;

  int differing = 0;
  int total = 0;
  for (const auto& c : stateMatrix()) {
    const PrimitiveState forward = makePrimitiveState(thermo, c.density, c.velocity, c.temperature);
    const Vector3 momentum{c.density * c.velocity.x, c.density * c.velocity.y,
                           c.density * c.velocity.z};
    const auto cpu = thermo.recover(c.density, momentum, forward.totalEnergyDensity);

    PrimitiveState device{};
    ASSERT_EQ(deviceRecoverPrimitive(packed, c.density, momentum, forward.totalEnergyDensity,
                                     &device, regrouped),
              DeviceRecoveryStatus::Ok);
    total += 2;
    if (bitsOf(cpu.pressure) != bitsOf(device.pressure)) ++differing;
    if (bitsOf(thermo.thermodynamics().speedOfSound(cpu.temperature)) !=
        bitsOf(device.speedOfSound)) {
      ++differing;
    }
  }
  std::printf("  A1 control: regrouped products differ in %d of %d values\n", differing, total);
  EXPECT_GT(differing, 0)
      << "regrouping (rho*R)*T into rho*(R*T) changed NOTHING in " << total
      << " comparisons, so A1's bitwise claim is not actually sensitive to operand order";
}

// =================================================================================================
// A2 -- the DECISIONS, which are what a divergence in the admissible set looks like
// =================================================================================================

TEST(DeviceThermodynamicsEquivalence, A2_RefusalsAgreeAndForTheSameReason) {
  const TotalEnergyState thermo = nonZeroReferenceAir();
  const DeviceThermodynamics packed = pack(thermo);
  const DeviceControls production{};

  struct Refusal {
    const char* name;
    Real density;
    Vector3 momentum;
    Real rhoE;
    DeviceRecoveryStatus expected;
  };

  // A state whose kinetic energy swamps its total energy has no internal energy left. Built from a
  // real state so only the intended property is wrong.
  const PrimitiveState good = makePrimitiveState(thermo, 1.2, Vector3{100.0, 0.0, 0.0}, 300.0);
  const Vector3 goodMomentum{1.2 * 100.0, 0.0, 0.0};

  const std::vector<Refusal> refusals{
      {"non_positive_density", 0.0, goodMomentum, good.totalEnergyDensity,
       DeviceRecoveryStatus::NonPositiveDensity},
      {"negative_density", -1.2, goodMomentum, good.totalEnergyDensity,
       DeviceRecoveryStatus::NonPositiveDensity},
      {"nan_density", std::nan(""), goodMomentum, good.totalEnergyDensity,
       DeviceRecoveryStatus::NonFiniteDensity},
      {"nan_momentum", 1.2, Vector3{std::nan(""), 0.0, 0.0}, good.totalEnergyDensity,
       DeviceRecoveryStatus::NonFiniteMomentum},
      {"nan_energy", 1.2, goodMomentum, std::nan(""), DeviceRecoveryStatus::NonFiniteTotalEnergy},
      {"kinetic_exceeds_total", 1.2, Vector3{1.2 * 5000.0, 0.0, 0.0}, good.totalEnergyDensity,
       DeviceRecoveryStatus::InternalEnergyBelowFloor},
  };

  for (const auto& r : refusals) {
    bool cpuRefused = false;
    std::string cpuMessage;
    try {
      (void)thermo.recover(r.density, r.momentum, r.rhoE);
    } catch (const cfd::Error& e) {
      cpuRefused = true;
      cpuMessage = e.what();
    }
    PrimitiveState device{};
    const DeviceRecoveryStatus st =
        deviceRecoverPrimitive(packed, r.density, r.momentum, r.rhoE, &device, production);

    EXPECT_TRUE(cpuRefused) << r.name << ": production ACCEPTED a state the device refuses";
    EXPECT_EQ(st, r.expected) << r.name << ": the device refused for a different reason";
    std::printf("  A2 %-22s cpu refused=%s device=%d\n", r.name, cpuRefused ? "YES" : "NO",
                static_cast<int>(st));
  }
}

// TRAP 7. temperatureFromInternalEnergy ends with `(void)enthalpy_.enthalpy(temperature)` -- called
// for its EXCEPTION, not its value, to reuse P15's range check. Reading that as dead code and
// dropping it would make the GPU ACCEPT states the CPU REFUSES: a divergence in the admissible set
// rather than in a number, which no amount of bitwise field comparison would reveal.
TEST(DeviceThermodynamicsEquivalence, A2_TemperatureRangeIsEnforced_AndItsRemovalIsDetected) {
  const TotalEnergyState thermo = nonZeroReferenceAir();  // range [200, 2500] K
  const DeviceThermodynamics packed = pack(thermo);
  ASSERT_DOUBLE_EQ(packed.minimumT, 200.0);
  ASSERT_DOUBLE_EQ(packed.maximumT, 2500.0);

  // A conserved state whose recovered temperature lands ABOVE the model's ceiling. Built by asking
  // for an in-range state and then adding energy, so density and momentum stay ordinary.
  const PrimitiveState hot = makePrimitiveState(thermo, 0.5, Vector3{10.0, 0.0, 0.0}, 2400.0);
  const Vector3 momentum{0.5 * 10.0, 0.0, 0.0};
  const Real tooMuchEnergy = hot.totalEnergyDensity * 2.0;

  bool cpuRefused = false;
  try {
    (void)thermo.recover(0.5, momentum, tooMuchEnergy);
  } catch (const cfd::Error&) {
    cpuRefused = true;
  }
  ASSERT_TRUE(cpuRefused) << "the case does not actually leave the property model's range, so it "
                             "cannot test the range check";

  PrimitiveState device{};
  const DeviceControls production{};
  EXPECT_EQ(deviceRecoverPrimitive(packed, 0.5, momentum, tooMuchEnergy, &device, production),
            DeviceRecoveryStatus::TemperatureOutOfRange)
      << "the device accepted a state production refuses";

  DeviceControls trap{};
  trap.skipTemperatureRangeCheck = true;  // V8 -- the trap, on purpose
  PrimitiveState slipped{};
  const DeviceRecoveryStatus without =
      deviceRecoverPrimitive(packed, 0.5, momentum, tooMuchEnergy, &slipped, trap);
  EXPECT_EQ(without, DeviceRecoveryStatus::Ok)
      << "V8's control did not actually let the out-of-range state through, so A2's range check is "
         "not the thing being measured";
  std::printf(
      "  A2 trap7: cpu refuses; device refuses; with the check removed device ACCEPTS "
      "T=%.4f K (range %.1f..%.1f)\n",
      slipped.temperature, packed.minimumT, packed.maximumT);
}

// =================================================================================================
// A6 -- exteriorState, bitwise against the production free function
// =================================================================================================

TEST(DeviceThermodynamicsEquivalence, A6_ExteriorStateIsBitwiseIdenticalForAllThreeKinds) {
  const TotalEnergyState thermo = nonZeroReferenceAir();
  const DeviceControls production{};

  const PrimitiveState interior =
      makePrimitiveState(thermo, 1.1, Vector3{420.0, -75.0, 33.0}, 330.0);
  const PrimitiveState prescribed =
      makePrimitiveState(thermo, 0.9, Vector3{800.0, 20.0, -5.0}, 280.0);

  // Normals that are NOT axis aligned, so the mirror term cannot cancel by accident. Each is a unit
  // vector by construction.
  const Real s = 1.0 / std::sqrt(3.0);
  const std::vector<Vector3> normals{Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0},
                                     Vector3{0.0, 0.0, 1.0}, Vector3{-1.0, 0.0, 0.0},
                                     Vector3{s, s, s},       Vector3{s, -s, s}};

  const std::vector<std::pair<BoundaryKind, DeviceBoundaryKind>> kinds{
      {BoundaryKind::InviscidWall, DeviceBoundaryKind::InviscidWall},
      {BoundaryKind::SupersonicInflow, DeviceBoundaryKind::SupersonicInflow},
      {BoundaryKind::SupersonicOutflow, DeviceBoundaryKind::SupersonicOutflow}};

  int compared = 0;
  for (const auto& [hostKind, deviceKind] : kinds) {
    DensityBasedBoundary boundary{};
    boundary.kind = hostKind;
    boundary.prescribed = prescribed;
    for (const Vector3& n : normals) {
      const PrimitiveState cpu = exteriorState(boundary, interior, n);
      const PrimitiveState dev =
          deviceExteriorState(deviceKind, interior, prescribed, n, production);
      EXPECT_BITWISE(cpu.density, dev.density, "exterior density");
      EXPECT_BITWISE(cpu.velocity.x, dev.velocity.x, "exterior velocity x");
      EXPECT_BITWISE(cpu.velocity.y, dev.velocity.y, "exterior velocity y");
      EXPECT_BITWISE(cpu.velocity.z, dev.velocity.z, "exterior velocity z");
      EXPECT_BITWISE(cpu.pressure, dev.pressure, "exterior pressure");
      EXPECT_BITWISE(cpu.temperature, dev.temperature, "exterior temperature");
      EXPECT_BITWISE(cpu.speedOfSound, dev.speedOfSound, "exterior sound speed");
      EXPECT_BITWISE(cpu.totalEnergyDensity, dev.totalEnergyDensity, "exterior rhoE");
      compared += 8;
    }
  }
  EXPECT_EQ(compared, 8 * 3 * static_cast<int>(normals.size()));
  std::printf("  A6: %d values compared across 3 kinds x %zu normals, all bitwise\n", compared,
              normals.size());
}

// TRAP 3. The wall REBUILDS rhoE from the mirrored velocity instead of carrying it over, because
// |U_ext| == |U_int| holds in exact arithmetic and not in floating point. "Simplifying" that away
// is the obvious optimisation, and this is the control that shows it is not free.
TEST(DeviceThermodynamicsEquivalence, A6_NonVacuity_CarryingRhoEOverUnchangedIsDetected) {
  const TotalEnergyState thermo = nonZeroReferenceAir();
  DeviceControls trap{};
  trap.exteriorCarriesRhoE = true;

  const PrimitiveState interior =
      makePrimitiveState(thermo, 1.1, Vector3{420.0, -75.0, 33.0}, 330.0);
  DensityBasedBoundary wall{};
  wall.kind = BoundaryKind::InviscidWall;

  const Real s = 1.0 / std::sqrt(3.0);
  const std::vector<Vector3> normals{Vector3{s, s, s}, Vector3{s, -s, s}, Vector3{1.0, 0.0, 0.0}};

  int differing = 0;
  for (const Vector3& n : normals) {
    const PrimitiveState cpu = exteriorState(wall, interior, n);
    const PrimitiveState dev =
        deviceExteriorState(DeviceBoundaryKind::InviscidWall, interior, PrimitiveState{}, n, trap);
    if (bitsOf(cpu.totalEnergyDensity) != bitsOf(dev.totalEnergyDensity)) ++differing;
  }
  std::printf("  A6 control: carrying rhoE over changes it on %d of %zu non-axis-aligned normals\n",
              differing, normals.size());
  EXPECT_GT(differing, 0)
      << "carrying rhoE over unchanged made no difference on any normal, so A6 is not sensitive to "
         "the rebuild and trap 3 is not actually being tested";
}
