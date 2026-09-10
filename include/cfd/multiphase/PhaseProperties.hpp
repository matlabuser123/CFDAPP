#pragma once

#include <string>

#include "cfd/core/Types.hpp"

namespace cfd::multiphase {

// P3-PHYS-005: identity + constant material properties for one phase of a
// two-phase immiscible mixture. Same thin, validated-at-construction value
// type this project already uses for every other "one physical material"
// concept (cfd::physics::FluidProperties, cfd::thermal::ThermalProperties,
// cfd::species::SpeciesProperties) -- this task's own section 3 sketches a
// richer struct with optional thermal properties, but constant density +
// viscosity is the mandatory completion target (section 9); thermal
// mixture properties are deliberately not added here (disclosed scope,
// see TODO.md's own P3-PHYS-005 status note).
//
// Throws InvalidArgumentError if name is empty, or density/viscosity is
// not finite and strictly positive (same requirement
// cfd::physics::FluidProperties already places on a single-phase fluid --
// a phase is a fluid, just one of two sharing a domain).
class PhaseProperties {
 public:
  PhaseProperties(std::string name, Real density, Real viscosity);

  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] Real density() const noexcept;
  [[nodiscard]] Real viscosity() const noexcept;

 private:
  std::string name_;
  Real density_{};
  Real viscosity_{};
};

}  // namespace cfd::multiphase
