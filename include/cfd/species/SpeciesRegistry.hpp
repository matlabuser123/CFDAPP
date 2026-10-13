#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "cfd/core/Types.hpp"

namespace cfd::species {

// P18.1 -- the authoritative identity of the species in one mixture: an
// ordered set of names with a bijective name <-> index mapping.
//
// This is deliberately SEPARATE from the mass-fraction values in
// `MassFractionState`, and separate again from `SpeciesProperties` (which
// pairs one name with one constant diffusivity for the passive transport
// stack). The three answer different questions and have different
// lifetimes: P18.3's mixture properties will need per-species molecular
// weights and cp keyed by identity with no field in sight, while P18.2's
// transport needs the fields. Folding identity into the field container
// would force a consumer to carry a whole field set to ask what species 2
// is called.
//
// ORDER IS PART OF THE VALUE. The names are stored in a vector in the
// order given, and that order fixes the species index, the layout of every
// composition vector, and -- through `MassFractionState` -- the
// accumulation order of the sum(Y_i) = 1 check. Two registries built from
// the same names in a different order are NOT equal, because a state
// validated against one would be misinterpreted against the other. The
// hash map exists only to make name lookup O(1); it never determines
// ordering, and nothing iterates it (P18.1 section 17: no unordered
// iteration anywhere that could change floating-point accumulation order).
class SpeciesRegistry {
 public:
  // Throws InvalidArgumentError if `names` is empty, if any name is empty,
  // or if any name is duplicated -- the error names the offending entry and
  // its index. An empty registry is rejected rather than treated as a
  // degenerate mixture: sum(Y_i) = 1 is unsatisfiable over zero species, so
  // an empty registry could only ever produce an invalid state.
  explicit SpeciesRegistry(std::vector<std::string> names);

  [[nodiscard]] Index size() const noexcept { return names_.size(); }

  // Throws InvalidArgumentError for an out-of-range index. Never returns a
  // placeholder name for an invalid index.
  [[nodiscard]] const std::string& name(Index index) const;

  // Throws InvalidArgumentError for a name this registry does not hold.
  [[nodiscard]] Index index(const std::string& name) const;

  [[nodiscard]] bool contains(const std::string& name) const;

  // In species-index order.
  [[nodiscard]] const std::vector<std::string>& names() const noexcept { return names_; }

 private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, Index> indexByName_;
};

// Equal iff the same names in the same order -- see the ordering note above.
[[nodiscard]] bool operator==(const SpeciesRegistry& lhs, const SpeciesRegistry& rhs);
[[nodiscard]] bool operator!=(const SpeciesRegistry& lhs, const SpeciesRegistry& rhs);

}  // namespace cfd::species
