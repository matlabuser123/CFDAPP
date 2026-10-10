#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/SpeciesRegistry.hpp"

namespace cfd::species {

// P18.1 -- the authoritative multi-species mass-fraction state: one
// Y_i per species per cell, with the mixture invariants ENFORCED rather
// than merely documented.
//
// This is a genuinely new capability, not a rename. The pre-P18 species
// stack transports a GENERIC PASSIVE SCALAR: `SpeciesProperties` states
// outright that it "does not itself enforce 0 <= Y <= 1 or sum(Y_i) = 1 --
// those are properties of a mass-fraction mixture convention, not of a
// generic constant-diffusivity passive scalar", and
// `cfd::species::concentrationBounds` reports a global {min, max} without
// ever enforcing anything. That remains true and remains supported; this
// type is the mass-fraction MIXTURE the pre-P18 stack deliberately was not.
//
// P18.1 IS STRICTLY ADDITIVE. Nothing in SpeciesEquation, SpeciesSolver,
// SpeciesProperties, the case schema, the parser, CaseBuilder,
// ProjectRunner or export changes, so existing passive-scalar behaviour is
// preserved by construction and the regression measures that rather than
// assuming it.
//
// ===================================================================
// THE THREE INVARIANTS, every one inherited verbatim from the P18 gate
// frozen at results/p18/gates/P18_GATES.md (sha256 921616544e3631d4...)
// BEFORE any of this existed. None was chosen here, and none was derived
// after seeing a measurement.
//
//   finiteness   std::isfinite(Y_i) at every (species, cell)
//   bounds       0 <= Y_i <= 1.  G2(b): "boolean, no tolerance. A mass
//                fraction is in [0,1] or it is not."
//   sum          |sum_i Y_i - 1| <= 4 * eps * N_species, PER CELL.
//                G2(a) derives it: a sum of N_species doubles each <= 1
//                accumulates at most ~N_species/2 eps of round-off, and 4x
//                that is a bound rather than a fitted number. Deliberately
//                NOT zero -- a normalised set cannot be exactly 1 in binary
//                floating point for general N.
// ===================================================================
//
// SUMMATION ORDER IS FIXED AND IS PART OF THE CONTRACT. The sum runs over
// ascending species index, accumulating left to right into one Real. No
// unordered iteration, no pairwise or compensated summation, no parallel
// reduction -- so validating the same state twice gives a bitwise identical
// sum, and the registry's ordering (which is part of its value) is what
// makes that reproducible across copies.
//
// LAYOUT: species-major, one ScalarField per species, Y_i[cell] ==
// field(i)[cell]. This is the repository's established field-per-species
// convention, so this type holds exactly the objects the existing stack
// already holds and the passive-scalar compatibility claim is a bitwise
// identity rather than a reinterpretation across a layout change. Cell-major
// would suit a future device kernel better; P18.8 owns that decision and
// P18.1 does not pre-empt it.
class MassFractionState {
 public:
  // Uniform composition: every cell gets `uniformComposition`, indexed by
  // species. Throws InvalidArgumentError if cellCount is zero, if the
  // composition length does not match the registry, or if the composition
  // violates any invariant.
  MassFractionState(SpeciesRegistry registry, Index cellCount,
                    const std::vector<Real>& uniformComposition);

  // Per-cell composition from one field per species, in species-index
  // order. Throws InvalidArgumentError if the field count does not match
  // the registry, if the fields differ in size, if any field is empty, or
  // if any CELL violates any invariant.
  MassFractionState(SpeciesRegistry registry, std::vector<fields::ScalarField> fields);

  [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] Index speciesCount() const noexcept { return registry_.size(); }
  [[nodiscard]] Index cellCount() const noexcept { return cellCount_; }

  // Both throw InvalidArgumentError for an unknown species or an
  // out-of-range cell -- never a placeholder value.
  [[nodiscard]] Real massFraction(Index speciesIndex, Index cell) const;
  [[nodiscard]] Real massFraction(const std::string& name, Index cell) const;

  // The species' field, for consumers that work field-at-a-time -- export,
  // and P18.2's transport. Returned by const reference; the state stays the
  // owner, and there is no mutable field accessor because handing one out
  // would be exactly the unvalidated single-value write that
  // setCellComposition exists to prevent.
  [[nodiscard]] const fields::ScalarField& field(Index speciesIndex) const;
  [[nodiscard]] const fields::ScalarField& field(const std::string& name) const;

  // Every species' field, in species-index order. Exposed for P18.2: a Picard
  // or deferred-correction ITERATE legitimately does not satisfy sum(Y_i) = 1
  // until it converges, so the transport layer works on the raw fields and
  // rebuilds a validated state only at a step boundary. Handing out const
  // references does not weaken the invariant -- nothing can be written through
  // them, and a state can still only be CONSTRUCTED from a composition that
  // passes every check.
  [[nodiscard]] const std::vector<fields::ScalarField>& fields() const noexcept { return fields_; }

  // The full composition of one cell, in species-index order.
  [[nodiscard]] std::vector<Real> cellComposition(Index cell) const;

  // THE ONLY MUTATOR, and it is atomic: the incoming composition is checked
  // for length, finiteness, bounds and sum BEFORE anything is written, so a
  // rejected call leaves the state exactly as it was.
  //
  // There is deliberately no setMassFraction(species, cell, value).
  // Changing one Y_i necessarily breaks sum(Y) = 1, so a single-species
  // setter could only either leave the object invalid or silently repair the
  // remainder -- the first is the temporary-invalid-state leak this policy
  // exists to prevent, and the second is the silent normalisation that
  // section 8 forbids. A caller that wants to change a composition supplies
  // the whole composition.
  void setCellComposition(Index cell, const std::vector<Real>& composition);

  // Re-checks every cell. Cannot fail on a state built through the public
  // API -- that is the point, and it is what the P18.1 negative controls
  // attack by removing each check in turn. Reports the FIRST offending cell
  // by index (section 16: per-cell, so one bad cell among thousands cannot
  // hide behind a global min/max/mean).
  void validate() const;

  // sum_i Y_i for one cell, in the fixed order described above. Exposed so
  // a caller can report the sum without having to reproduce the accumulation
  // order and risk measuring a different number than validate() does.
  [[nodiscard]] Real compositionSum(Index cell) const;

 private:
  void validateCell(Index cell, const std::vector<Real>& composition) const;

  SpeciesRegistry registry_;
  std::vector<fields::ScalarField> fields_;
  Index cellCount_{};
};

// The frozen G2(a) tolerance, 4 * eps * N_species, as one function so that
// production, tests and evidence all quote the same number and none of them
// can drift.
[[nodiscard]] Real massFractionSumTolerance(Index speciesCount) noexcept;

// EXPLICIT, NAMED, AND NEVER CALLED IMPLICITLY -- not by any constructor,
// not by setCellComposition. Y = [0.8, 0.8] is REJECTED by the state and
// only becomes [0.5, 0.5] if a caller asks for that here. Keeping
// normalisation out of validation is what keeps a conservation error
// visible instead of silently absorbed.
//
// Throws InvalidArgumentError for an empty composition, any non-finite
// entry, any negative entry, or a sum that is zero or non-finite. Entries
// greater than 1 are NOT rejected: [2, 2] is a legitimate un-normalised
// ratio and normalises to [0.5, 0.5]. The result is still subject to the
// ordinary validation when it is used -- normalising is not a side door
// into the state.
[[nodiscard]] std::vector<Real> normalizeMassFractions(const std::vector<Real>& composition);

}  // namespace cfd::species
