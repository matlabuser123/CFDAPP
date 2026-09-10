#pragma once

#include <string>

#include "cfd/thermal/ThermalProperties.hpp"

namespace cfd::thermal {

// P2-THERMAL-005 (conjugate heat-transfer foundation): the minimum
// distinction a CHT model needs between a fluid and a solid conduction
// region -- see ThermalRegionMap.hpp for how cells are assigned to one.
// Fluid vs Solid is currently descriptive/reporting metadata only: this
// foundation's assembly (ThermalInterface.hpp) treats every region as
// conduction-only regardless of type (TODO.md P2 -- Thermal "Conjugate
// heat-transfer foundation": "no solid convection... this foundation may
// remain conduction-only everywhere" -- a live fluid region with real
// convection is not implemented here).
enum class ThermalRegionType {
  Fluid,
  Solid,
};

// A named material: its own ThermalProperties (conductivity/specific
// heat), reusing the already-validated value type rather than
// duplicating its finite/positive checks here.
struct ThermalRegion {
  std::string name;
  ThermalRegionType type{ThermalRegionType::Fluid};
  ThermalProperties properties;
};

}  // namespace cfd::thermal
