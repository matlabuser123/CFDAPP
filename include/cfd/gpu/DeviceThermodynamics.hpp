#pragma once

// P17.10 -- the thermodynamic closed forms, and the three small solver helpers, written ONCE for
// both host and device.
//
// This header is NOT CUDA-only. Like HllcFlux.hpp, it uses CFD_HLLC_HOST_DEVICE, which expands to
// `__host__ __device__` under nvcc and to NOTHING under gcc, so the same text compiles into the CPU
// build and into a kernel. That matters for more than convenience: it lets the cross-comparison
// tests run in the CPU-ONLY configuration too, so the restatements below are checked against
// production even on a machine with no GPU.
//
// WHY ANYTHING IS RESTATED AT ALL. TotalEnergyState holds a SensibleEnthalpy, which holds a
// TemperatureDependentProperty, which is std::vector-backed and cannot cross to the device. But the
// recovery the density-based solver performs never evaluates that polynomial -- the constructor
// REFUSES a temperature-dependent cp outright (src/compressible/TotalEnergy.cpp), so the gas is
// calorically perfect by enforcement and the recovery is a closed form in five scalars.
// audit.md §5 records this as a decision; authorization §4 permits it "if a direct cross-comparison
// test is added", and tests/unit/gpu/test_device_thermodynamics_equivalence.cpp is that test.
//
// EVERY EXPRESSION BELOW IS PARENTHESISED THE WAY THE CPU WRITES IT. That is not style. The CPU
// computes `density * gasConstant_ * temperature`, which is (rho*R)*T left to right; writing
// rho*(R*T) is the same number in exact arithmetic and a different one in floating point. The
// cross-comparison test is what makes that claim checkable rather than asserted, and
// kernel_inventory.md §4 lists the seven places where the obvious simplification is the wrong one.

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector3.hpp"

namespace cfd::gpu {

using cfd::Real;
using cfd::Vector3;
using cfd::compressible::PrimitiveState;

// The nine scalars a device needs in place of the whole TotalEnergyState. A flat POD, uploaded once
// per solve and never mutated.
struct DeviceThermodynamics {
  Real gasConstant{};        // R
  Real specificHeatP{};      // cp
  Real specificHeatV{};      // cv
  Real specificHeatRatio{};  // gamma
  Real referenceT{};         // T_ref
  Real referenceH{};         // h_ref
  Real minimumT{};           // the property model's range floor
  Real maximumT{};           // and its ceiling
  // e(T_min), the admissible floor for the specific internal energy. COMPUTED ON THE HOST by
  // calling the production TotalEnergyState::specificInternalEnergy(), never recomputed here: it
  // runs the cp polynomial's antiderivative, and a constant that is shipped cannot drift from the
  // one the CPU uses.
  Real internalEnergyFloor{};
};

// Why a recovery was refused. The device cannot throw, so the reason is carried out as a value --
// and the equivalence suite compares the REASON, not merely the fact of refusal, because two
// backends rejecting the same state for different reasons is a divergence in the admissible set.
enum class DeviceRecoveryStatus {
  Ok = 0,
  NonFiniteDensity = 1,
  NonPositiveDensity = 2,
  NonFiniteMomentum = 3,
  NonFiniteTotalEnergy = 4,
  InternalEnergyBelowFloor = 5,
  NonFiniteTemperature = 6,
  TemperatureOutOfRange = 7,
  InvalidRecoveredState = 8
};

// The control switches that make every negative control of the frozen gate's Group V a FLAG rather
// than a source edit. All false is the production path.
//
// This is deliberate and it is stronger than editing the source for a control run: the control and
// the production path are THE SAME BINARY, compiled once, so a control can never accidentally
// measure a different build. Each flag names the trap it re-introduces
// (kernel_inventory.md §4), and the gate requires each to break a NAMED criterion.
struct DeviceControls {
  bool useHllInsteadOfHllc{false};          // V1  the contact wave deleted
  bool reverseGatherOrder{false};           // V3  the gather no longer matches the CPU's face loop
  bool neighbourUsesOwnNormal{false};       // V4  trap 1
  bool exteriorCarriesRhoE{false};          // V5  trap 3
  bool cflSoundFromMeanTemperature{false};  // V6  trap 5
  bool skipTemperatureRangeCheck{false};    // V8  trap 7
  bool regroupProducts{false};              // A1  rho*(R*T) instead of (rho*R)*T
};

// p = rho R T, IdealGasEOS.cpp:66. The CPU writes `density * gasConstant_ * temperature`.
CFD_HLLC_HOST_DEVICE inline Real devicePressure(const DeviceThermodynamics& t, Real density,
                                                Real temperature, bool regroup = false) {
  return regroup ? density * (t.gasConstant * temperature) : density * t.gasConstant * temperature;
}

// a = sqrt(gamma R T), ThermodynamicProperties.cpp:40. The CPU writes
// `specificHeatRatio_ * gasConstant() * temperature`.
CFD_HLLC_HOST_DEVICE inline Real deviceSpeedOfSound(const DeviceThermodynamics& t, Real temperature,
                                                    bool regroup = false) {
  const Real arg = regroup ? t.specificHeatRatio * (t.gasConstant * temperature)
                           : t.specificHeatRatio * t.gasConstant * temperature;
  return cfd::compressible::hllc_detail::sqrtR(arg);
}

// T from e, TotalEnergy.cpp:120. Closed form, exact for the calorically perfect gas the
// TotalEnergyState constructor enforces.
CFD_HLLC_HOST_DEVICE inline Real deviceTemperatureFromInternalEnergy(const DeviceThermodynamics& t,
                                                                     Real specificInternalEnergy) {
  return (specificInternalEnergy - t.referenceH + t.specificHeatP * t.referenceT) / t.specificHeatV;
}

// rhoE from (rho, T, U). TotalEnergy.cpp:89 + :95 + :104 --
//   rhoE = rho * (e(T) + 0.5 |U|^2), with e(T) = h(T) - R T and h(T) = h_ref + cp (T - T_ref).
// Present for the forward direction the boundary states need; the recovery below is its inverse.
CFD_HLLC_HOST_DEVICE inline Real deviceConservedTotalEnergy(const DeviceThermodynamics& t,
                                                            Real density, Real temperature,
                                                            const Vector3& velocity) {
  const Real h = t.referenceH + t.specificHeatP * (temperature - t.referenceT);
  const Real e = h - t.gasConstant * temperature;
  const Real ke = 0.5 * cfd::compressible::hllc_detail::dotHD(velocity, velocity);
  return density * (e + ke);
}

// The inverse: Q -> primitive, reproducing makePrimitiveStateFromConserved -> recover().
// Returns Ok and fills `out`, or a status and leaves `out` unspecified. NOTHING IS CLAMPED --
// that policy is inherited from P17.5 deliberately, because an energy floor lets a diverging solve
// look converged.
CFD_HLLC_HOST_DEVICE inline DeviceRecoveryStatus deviceRecoverPrimitive(
    const DeviceThermodynamics& t, Real density, const Vector3& momentumDensity,
    Real totalEnergyDensity, PrimitiveState* out, const DeviceControls& controls) {
  namespace hd = cfd::compressible::hllc_detail;
  if (!hd::finiteR(density)) return DeviceRecoveryStatus::NonFiniteDensity;
  if (!(density > 0.0)) return DeviceRecoveryStatus::NonPositiveDensity;
  if (!hd::finiteVec(momentumDensity)) return DeviceRecoveryStatus::NonFiniteMomentum;
  if (!hd::finiteR(totalEnergyDensity)) return DeviceRecoveryStatus::NonFiniteTotalEnergy;

  const Vector3 velocity{momentumDensity.x / density, momentumDensity.y / density,
                         momentumDensity.z / density};
  const Real ke = 0.5 * hd::dotHD(velocity, velocity);
  const Real eTotal = totalEnergyDensity / density;
  const Real e = eTotal - ke;
  if (!hd::finiteR(e) || e < t.internalEnergyFloor) {
    return DeviceRecoveryStatus::InternalEnergyBelowFloor;
  }

  const Real temperature = deviceTemperatureFromInternalEnergy(t, e);
  if (!hd::finiteR(temperature)) return DeviceRecoveryStatus::NonFiniteTemperature;
  // TRAP 7. The CPU's `(void)enthalpy_.enthalpy(temperature)` is called for its EXCEPTION, not its
  // value: it reuses P15's range check. Dropping it would make the GPU ACCEPT states the CPU
  // REFUSES -- a divergence in the admissible set rather than in a number.
  if (!controls.skipTemperatureRangeCheck) {
    if (temperature < t.minimumT || temperature > t.maximumT) {
      return DeviceRecoveryStatus::TemperatureOutOfRange;
    }
  }

  out->density = density;
  out->velocity = velocity;
  out->temperature = temperature;
  out->pressure = devicePressure(t, density, temperature, controls.regroupProducts);
  out->speedOfSound = deviceSpeedOfSound(t, temperature, controls.regroupProducts);
  out->totalEnergyDensity = totalEnergyDensity;

  if (cfd::compressible::validateState(*out) != cfd::compressible::StateValidity::Valid) {
    return DeviceRecoveryStatus::InvalidRecoveredState;
  }
  return DeviceRecoveryStatus::Ok;
}

// The outward unit normal of a face seen from the owner (s = +1) or the neighbour (s = -1).
// DensityBasedSolver.cpp:35-38.
CFD_HLLC_HOST_DEVICE inline Vector3 deviceOutwardNormal(const Vector3& areaVector, Real area,
                                                        bool fromOwner) {
  const Real s = fromOwner ? 1.0 : -1.0;
  return Vector3{s * areaVector.x / area, s * areaVector.y / area, s * areaVector.z / area};
}

// The arithmetic mean used ONLY for the CFL face speed. DensityBasedSolver.cpp:50-58.
//
// TRAP 5: it averages the SOUND SPEED ITSELF. 0.5*(a_L + a_R) is not sqrt(gamma R (0.5(T_L+T_R))),
// and recomputing a from the mean temperature is the physically tidier choice and the wrong one.
CFD_HLLC_HOST_DEVICE inline PrimitiveState deviceMeanForCfl(const DeviceThermodynamics& t,
                                                            const PrimitiveState& a,
                                                            const PrimitiveState& b,
                                                            const DeviceControls& controls) {
  PrimitiveState m{};
  m.density = 0.5 * (a.density + b.density);
  m.velocity = Vector3{0.5 * (a.velocity.x + b.velocity.x), 0.5 * (a.velocity.y + b.velocity.y),
                       0.5 * (a.velocity.z + b.velocity.z)};
  m.pressure = 0.5 * (a.pressure + b.pressure);
  m.temperature = 0.5 * (a.temperature + b.temperature);
  m.speedOfSound = controls.cflSoundFromMeanTemperature ? deviceSpeedOfSound(t, m.temperature)
                                                        : 0.5 * (a.speedOfSound + b.speedOfSound);
  m.totalEnergyDensity = 0.5 * (a.totalEnergyDensity + b.totalEnergyDensity);
  return m;
}

// The three boundary treatments, matching cfd::compressible::BoundaryKind's order exactly so the
// host can upload the enum's underlying value with no translation table to get wrong.
enum class DeviceBoundaryKind { InviscidWall = 0, SupersonicInflow = 1, SupersonicOutflow = 2 };

// The exterior state for a boundary face. DensityBasedSolver.cpp:181-215.
//
// TRAP 3: rhoE is REBUILT from the mirrored velocity rather than carried over. |U_ext| == |U_int|
// holds in exact arithmetic and is not guaranteed in floating point, so recomputing is the honest
// choice -- and "simplifying" it back is the obvious optimisation that breaks bitwise equality.
CFD_HLLC_HOST_DEVICE inline PrimitiveState deviceExteriorState(DeviceBoundaryKind kind,
                                                               const PrimitiveState& interior,
                                                               const PrimitiveState& prescribed,
                                                               const Vector3& outwardUnitNormal,
                                                               const DeviceControls& controls) {
  if (kind == DeviceBoundaryKind::SupersonicInflow) return prescribed;
  if (kind == DeviceBoundaryKind::SupersonicOutflow) return interior;

  const Real un = cfd::compressible::hllc_detail::dotHD(interior.velocity, outwardUnitNormal);
  PrimitiveState ext = interior;
  ext.velocity = Vector3{interior.velocity.x - 2.0 * un * outwardUnitNormal.x,
                         interior.velocity.y - 2.0 * un * outwardUnitNormal.y,
                         interior.velocity.z - 2.0 * un * outwardUnitNormal.z};
  if (controls.exteriorCarriesRhoE) return ext;  // V5 -- the trap, on purpose
  const Real keInt =
      0.5 * interior.density *
      (interior.velocity.x * interior.velocity.x + interior.velocity.y * interior.velocity.y +
       interior.velocity.z * interior.velocity.z);
  const Real keExt = 0.5 * ext.density *
                     (ext.velocity.x * ext.velocity.x + ext.velocity.y * ext.velocity.y +
                      ext.velocity.z * ext.velocity.z);
  ext.totalEnergyDensity = interior.totalEnergyDensity - keInt + keExt;
  return ext;
}

}  // namespace cfd::gpu
