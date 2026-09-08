#pragma once

#include "cfd/io/SimulationSetup.hpp"
#include "cfd/io/case/CaseDefinition.hpp"

namespace cfd::io {

// Converts an already-validated CaseDefinition into runtime CFD objects
// (TODO.md P1 section 24-25). Everything CaseBuilder constructs
// (FluidProperties, MovingWall/Inlet/etc. BC objects, ...) still runs
// through its own constructor validation -- CaseReader's parse-time
// validation and each runtime type's own construction-time validation
// are two independent, consistent layers (section 9: "do not let JSON
// bypass FluidProperties validation"), not a second competing policy.
class CaseBuilder {
 public:
  [[nodiscard]] SimulationSetup build(const CaseDefinition& definition) const;
};

}  // namespace cfd::io
