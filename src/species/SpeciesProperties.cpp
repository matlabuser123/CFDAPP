#include "cfd/species/SpeciesProperties.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::species {

SpeciesProperties::SpeciesProperties(std::string name, Real diffusivity)
    : name_(std::move(name)), diffusivity_(diffusivity) {
  if (name_.empty()) {
    throw InvalidArgumentError("SpeciesProperties: name must not be empty");
  }
  if (!std::isfinite(diffusivity_) || diffusivity_ < 0.0) {
    throw InvalidArgumentError("SpeciesProperties: diffusivity must be finite and >= 0");
  }
}

const std::string& SpeciesProperties::name() const noexcept { return name_; }
Real SpeciesProperties::diffusivity() const noexcept { return diffusivity_; }

}  // namespace cfd::species
