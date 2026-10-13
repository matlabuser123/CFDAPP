#include "cfd/compressible/TotalEnergy.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::compressible {

using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::thermal::SensibleEnthalpy;

namespace {

void requireFinite(Real value, const char* what, const char* caller) {
  if (!std::isfinite(value)) {
    throw InvalidArgumentError(std::string(caller) + ": " + what + " must be finite");
  }
}

void requirePositiveFinite(Real value, const char* what, const char* caller) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string(caller) + ": " + what + " must be finite and > 0");
  }
}

void requireCellCount(const cfd::mesh::Mesh& mesh, std::size_t size, const char* what,
                      const char* caller) {
  if (size != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string(caller) + ": " + what +
                               " size does not match mesh cell count");
  }
}

}  // namespace

TotalEnergyState::TotalEnergyState(ThermodynamicProperties thermodynamics,
                                   SensibleEnthalpy enthalpy)
    : thermo_(std::move(thermodynamics)), enthalpy_(std::move(enthalpy)) {
  // THE DUAL-cp CHECK. audit.md section 4 measured that nothing anywhere cross-validated
  // physics.thermal.specific_heat against physics.compressible.specific_heat_pressure, so a
  // thermal-coupled compressible case could declare two different gases and run silently. This
  // constructor is the only place that sees both, so it is where the inconsistency has to stop
  // being silent.
  const auto& cpModel = enthalpy_.specificHeatModel();
  if (!cpModel.isConstant()) {
    // Not a limitation of the mathematics -- a limitation of what the EOS block can represent.
    // ThermodynamicProperties stores ONE cp and derives cv = cp - R and gamma = cp/cv from it, so
    // a cp(T) model cannot be made consistent with it at all. Rejecting it by name is the opposite
    // of silently evaluating h = cp(T)*T, which P15's own Enthalpy.hpp measures as 26-103% wrong.
    throw InvalidArgumentError(
        "TotalEnergyState: the enthalpy model's specific heat is temperature-dependent, but "
        "ThermodynamicProperties carries a single cp from which it derives cv and gamma. A "
        "variable-cp energy state would be thermodynamically inconsistent with the EOS block; "
        "supporting it requires a variable-gamma EOS. Supply a constant specific heat.");
  }

  const Real cpEnthalpy = cpModel.evaluate(enthalpy_.referenceTemperature());
  const Real cpEos = thermo_.specificHeatPressure();
  requirePositiveFinite(cpEnthalpy, "the enthalpy model's specific heat", "TotalEnergyState");
  const Real relative = std::abs(cpEnthalpy - cpEos) / std::abs(cpEos);
  if (relative > kSpecificHeatRelativeTolerance) {
    throw InvalidArgumentError(
        "TotalEnergyState: the two specific-heat sources disagree. The enthalpy model gives cp = " +
        std::to_string(cpEnthalpy) + " and ThermodynamicProperties gives cp = " +
        std::to_string(cpEos) + " (relative difference " + std::to_string(relative) +
        ", tolerance " + std::to_string(kSpecificHeatRelativeTolerance) +
        "). These describe two different gases, so the conserved energy rhoE would be ambiguous. "
        "Reconcile physics.thermal.specific_heat with "
        "physics.compressible.specific_heat_pressure.");
  }
}

Real TotalEnergyState::specificStaticEnthalpy(Real temperature) const {
  // Straight from P15. Its own range check throws for an out-of-range temperature, and that error
  // is deliberately not caught or softened here.
  return enthalpy_.enthalpy(temperature);
}

Real TotalEnergyState::specificInternalEnergy(Real temperature) const {
  // THE DEFINING RELATION. e := h(T) - R T, which makes h - e = R T exact by construction for any
  // reference state and gives de/dT = cp - R = cv without asserting it.
  return enthalpy_.enthalpy(temperature) - thermo_.gasConstant() * temperature;
}

Real TotalEnergyState::specificKineticEnergy(Vector2 velocity) {
  // dot() is the 3-component product and Vector2 is a Vector3 with z = 0, so this is u^2 + v^2 in
  // 2D and u^2 + v^2 + w^2 in 3D with no special case -- the "inactive dimensions handled
  // consistently" requirement is satisfied by the type, not by a branch.
  return 0.5 * dot(velocity, velocity);
}

Real TotalEnergyState::specificTotalEnergy(Real temperature, Vector2 velocity) const {
  return specificInternalEnergy(temperature) + specificKineticEnergy(velocity);
}

Real TotalEnergyState::specificTotalEnthalpy(Real temperature, Vector2 velocity) const {
  return specificStaticEnthalpy(temperature) + specificKineticEnergy(velocity);
}

Real TotalEnergyState::conservedTotalEnergy(Real density, Real temperature,
                                            Vector2 velocity) const {
  requirePositiveFinite(density, "density", "TotalEnergyState::conservedTotalEnergy");
  return density * specificTotalEnergy(temperature, velocity);
}

Real TotalEnergyState::temperatureFromInternalEnergy(Real specificInternalEnergy) const {
  requireFinite(specificInternalEnergy, "specific internal energy",
                "TotalEnergyState::temperatureFromInternalEnergy");
  // Closed form, exact for the calorically perfect gas this class enforces:
  //   e = h_ref + cp (T - T_ref) - R T = (h_ref - cp T_ref) + cv T
  // so no root find is introduced. P15's bisection is not duplicated; it stays the authority for
  // the forward direction and the two are checked against each other in the tests.
  const Real cp = thermo_.specificHeatPressure();
  const Real cv = thermo_.specificHeatVolume();
  const Real tRef = enthalpy_.referenceTemperature();
  const Real hRef = enthalpy_.referenceEnthalpy();
  const Real temperature = (specificInternalEnergy - hRef + cp * tRef) / cv;
  if (!std::isfinite(temperature)) {
    throw NumericalError(
        "TotalEnergyState::temperatureFromInternalEnergy: recovered a non-finite temperature");
  }
  // The range check is P15's and is reused rather than reimplemented: asking the enthalpy model for
  // h(T) throws for an out-of-range T. Never clamped -- a clamped temperature corresponds to an
  // energy the state does not have.
  (void)enthalpy_.enthalpy(temperature);
  return temperature;
}

bool TotalEnergyState::hasZeroReference() const noexcept {
  return enthalpy_.referenceTemperature() == 0.0 && enthalpy_.referenceEnthalpy() == 0.0;
}

TotalEnergyState::RecoveredState TotalEnergyState::recover(Real density, Vector2 momentumDensity,
                                                           Real rhoE) const {
  constexpr const char* kCaller = "TotalEnergyState::recover";
  requirePositiveFinite(density, "density", kCaller);
  requireFinite(rhoE, "rhoE", kCaller);
  requireFinite(momentumDensity.x, "momentum density x", kCaller);
  requireFinite(momentumDensity.y, "momentum density y", kCaller);
  requireFinite(momentumDensity.z, "momentum density z", kCaller);

  RecoveredState out;
  out.velocity = Vector2{momentumDensity.x / density, momentumDensity.y / density,
                         momentumDensity.z / density};
  out.specificKineticEnergy = specificKineticEnergy(out.velocity);
  const Real eTotal = rhoE / density;
  out.specificInternalEnergy = eTotal - out.specificKineticEnergy;

  // The physically important rejection, and the reason it is not a clamp. A conserved state whose
  // kinetic energy exceeds its total energy has no internal energy left; silently flooring it would
  // produce a negative temperature, then a negative or NaN pressure, and a diverging solve would
  // look converged. The comparison is against the ADMISSIBLE floor for this reference state, not
  // against zero: under a nonzero reference e can legitimately be negative, because e's zero point
  // is inherited from h's.
  const Real eFloor = specificInternalEnergy(enthalpy_.specificHeatModel().minimumTemperature());
  if (!std::isfinite(out.specificInternalEnergy) || out.specificInternalEnergy < eFloor) {
    throw NumericalError(
        std::string(kCaller) + ": the recovered specific internal energy " +
        std::to_string(out.specificInternalEnergy) +
        " is below the thermodynamically admissible floor " + std::to_string(eFloor) +
        " (kinetic energy " + std::to_string(out.specificKineticEnergy) + " against a total of " +
        std::to_string(eTotal) + "). Nothing is clamped: this state is not recoverable.");
  }

  out.temperature = temperatureFromInternalEnergy(out.specificInternalEnergy);
  // Pressure from P17.2's EOS, never from p = (gamma-1)(rhoE - ke): that form is 83% wrong under a
  // nonzero reference, measured in logs/01_dryrun.log section F.
  out.pressure = thermo_.equationOfState().pressure(density, out.temperature);
  return out;
}

// --- field-level ---------------------------------------------------------

ScalarField conservedTotalEnergyField(const cfd::mesh::Mesh& mesh, const ScalarField& density,
                                      const ScalarField& temperature, const VectorField& velocity,
                                      const TotalEnergyState& state) {
  constexpr const char* kCaller = "conservedTotalEnergyField";
  requireCellCount(mesh, density.size(), "density", kCaller);
  requireCellCount(mesh, temperature.size(), "temperature", kCaller);
  requireCellCount(mesh, velocity.size(), "velocity", kCaller);

  ScalarField result(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!std::isfinite(density[i]) || !(density[i] > 0.0)) {
      throw InvalidArgumentError(std::string(kCaller) + ": cell " + std::to_string(i) +
                                 ": density must be finite and > 0");
    }
    result[i] = state.conservedTotalEnergy(density[i], temperature[i], velocity[i]);
  }
  return result;
}

ScalarField specificTotalEnthalpyField(const cfd::mesh::Mesh& mesh, const ScalarField& temperature,
                                       const VectorField& velocity, const TotalEnergyState& state) {
  constexpr const char* kCaller = "specificTotalEnthalpyField";
  requireCellCount(mesh, temperature.size(), "temperature", kCaller);
  requireCellCount(mesh, velocity.size(), "velocity", kCaller);

  ScalarField result(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    result[i] = state.specificTotalEnthalpy(temperature[i], velocity[i]);
  }
  return result;
}

RecoveredFields recoverPrimitiveFields(const cfd::mesh::Mesh& mesh, const ScalarField& density,
                                       const VectorField& momentumDensity, const ScalarField& rhoE,
                                       const TotalEnergyState& state) {
  constexpr const char* kCaller = "recoverPrimitiveFields";
  requireCellCount(mesh, density.size(), "density", kCaller);
  requireCellCount(mesh, momentumDensity.size(), "momentumDensity", kCaller);
  requireCellCount(mesh, rhoE.size(), "rhoE", kCaller);

  const Index n = mesh.numberOfCells();
  RecoveredFields out{VectorField(n), ScalarField(n), ScalarField(n), ScalarField(n)};
  for (Index i = 0; i < n; ++i) {
    TotalEnergyState::RecoveredState cell;
    try {
      cell = state.recover(density[i], momentumDensity[i], rhoE[i]);
    } catch (const Error& error) {
      // Name the offending cell, the same convention evaluateDensityField and
      // evaluatePropertyField already use -- a bare "invalid state" is not actionable on a mesh.
      throw NumericalError(std::string(kCaller) + ": cell " + std::to_string(i) + ": " +
                           error.what());
    }
    out.velocity[i] = cell.velocity;
    out.temperature[i] = cell.temperature;
    out.pressure[i] = cell.pressure;
    out.specificInternalEnergy[i] = cell.specificInternalEnergy;
  }
  return out;
}

}  // namespace cfd::compressible
