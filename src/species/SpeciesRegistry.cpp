#include "cfd/species/SpeciesRegistry.hpp"

#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::species {

SpeciesRegistry::SpeciesRegistry(std::vector<std::string> names) : names_(std::move(names)) {
  if (names_.empty()) {
    throw InvalidArgumentError(
        "species registry must contain at least one species; received an empty list");
  }

  indexByName_.reserve(names_.size());
  for (Index i = 0; i < names_.size(); ++i) {
    if (names_[i].empty()) {
      throw InvalidArgumentError("species name at index " + std::to_string(i) + " is empty");
    }
    // emplace reports the collision rather than overwriting, so the
    // duplicate is named in the message instead of silently shadowing the
    // earlier entry and leaving the map smaller than the vector.
    const auto [it, inserted] = indexByName_.emplace(names_[i], i);
    if (!inserted) {
      throw InvalidArgumentError("duplicate species name \"" + names_[i] + "\" at index " +
                                 std::to_string(i) + "; first declared at index " +
                                 std::to_string(it->second));
    }
  }
}

const std::string& SpeciesRegistry::name(Index index) const {
  if (index >= names_.size()) {
    throw InvalidArgumentError("species index " + std::to_string(index) +
                               " is out of range; registry holds " + std::to_string(names_.size()) +
                               " species");
  }
  return names_[index];
}

Index SpeciesRegistry::index(const std::string& name) const {
  const auto it = indexByName_.find(name);
  if (it == indexByName_.end()) {
    throw InvalidArgumentError("unknown species \"" + name + "\"");
  }
  return it->second;
}

bool SpeciesRegistry::contains(const std::string& name) const {
  return indexByName_.find(name) != indexByName_.end();
}

bool operator==(const SpeciesRegistry& lhs, const SpeciesRegistry& rhs) {
  return lhs.names() == rhs.names();
}

bool operator!=(const SpeciesRegistry& lhs, const SpeciesRegistry& rhs) { return !(lhs == rhs); }

}  // namespace cfd::species
