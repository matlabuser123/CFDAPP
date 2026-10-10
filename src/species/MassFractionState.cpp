#include "cfd/species/MassFractionState.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::species {
namespace {

// Enough digits to round-trip a double, so a rejection message states the
// value that was actually rejected rather than a rounded stand-in a reader
// would then fail to reproduce.
std::string exact(Real value) {
  std::ostringstream out;
  out.precision(std::numeric_limits<Real>::max_digits10);
  out << value;
  return out.str();
}

}  // namespace

Real massFractionSumTolerance(Index speciesCount) noexcept {
  // G2(a), frozen: 4 * eps * N_species. Not derived here and not adjustable.
  return 4.0 * std::numeric_limits<Real>::epsilon() * static_cast<Real>(speciesCount);
}

MassFractionState::MassFractionState(SpeciesRegistry registry, Index cellCount,
                                     const std::vector<Real>& uniformComposition)
    : registry_(std::move(registry)), cellCount_(cellCount) {
  if (cellCount_ == 0) {
    throw InvalidArgumentError("mass-fraction state must have at least one cell");
  }
  // Validate the single composition ONCE against cell 0 rather than once
  // per cell: every cell is about to hold the same values, so a per-cell
  // loop would report the same defect N times and cost N times as much.
  validateCell(0, uniformComposition);

  fields_.reserve(registry_.size());
  for (Index i = 0; i < registry_.size(); ++i) {
    fields_.emplace_back(cellCount_, uniformComposition[i]);
  }
}

MassFractionState::MassFractionState(SpeciesRegistry registry,
                                     std::vector<fields::ScalarField> fields)
    : registry_(std::move(registry)), fields_(std::move(fields)) {
  if (fields_.size() != registry_.size()) {
    throw InvalidArgumentError("mass-fraction state given " + std::to_string(fields_.size()) +
                               " fields for " + std::to_string(registry_.size()) +
                               " species; one field per species is required");
  }

  cellCount_ = fields_[0].size();
  if (cellCount_ == 0) {
    throw InvalidArgumentError("mass-fraction state must have at least one cell");
  }
  for (Index i = 1; i < fields_.size(); ++i) {
    if (fields_[i].size() != cellCount_) {
      throw InvalidArgumentError("mass-fraction field for species \"" + registry_.name(i) +
                                 "\" has " + std::to_string(fields_[i].size()) +
                                 " cells; species \"" + registry_.name(0) + "\" has " +
                                 std::to_string(cellCount_));
    }
  }

  validate();
}

Real MassFractionState::massFraction(Index speciesIndex, Index cell) const {
  if (speciesIndex >= registry_.size()) {
    throw InvalidArgumentError("species index " + std::to_string(speciesIndex) +
                               " is out of range; state holds " + std::to_string(registry_.size()) +
                               " species");
  }
  if (cell >= cellCount_) {
    throw InvalidArgumentError("cell " + std::to_string(cell) + " is out of range; state holds " +
                               std::to_string(cellCount_) + " cells");
  }
  return fields_[speciesIndex][cell];
}

Real MassFractionState::massFraction(const std::string& name, Index cell) const {
  return massFraction(registry_.index(name), cell);
}

const fields::ScalarField& MassFractionState::field(Index speciesIndex) const {
  if (speciesIndex >= registry_.size()) {
    throw InvalidArgumentError("species index " + std::to_string(speciesIndex) +
                               " is out of range; state holds " + std::to_string(registry_.size()) +
                               " species");
  }
  return fields_[speciesIndex];
}

const fields::ScalarField& MassFractionState::field(const std::string& name) const {
  return field(registry_.index(name));
}

std::vector<Real> MassFractionState::cellComposition(Index cell) const {
  if (cell >= cellCount_) {
    throw InvalidArgumentError("cell " + std::to_string(cell) + " is out of range; state holds " +
                               std::to_string(cellCount_) + " cells");
  }
  std::vector<Real> composition(registry_.size());
  for (Index i = 0; i < registry_.size(); ++i) {
    composition[i] = fields_[i][cell];
  }
  return composition;
}

Real MassFractionState::compositionSum(Index cell) const {
  if (cell >= cellCount_) {
    throw InvalidArgumentError("cell " + std::to_string(cell) + " is out of range; state holds " +
                               std::to_string(cellCount_) + " cells");
  }
  // FIXED ORDER: ascending species index, accumulated left to right into
  // one Real. Nothing here may become a parallel reduction or a compensated
  // sum without changing the number this returns.
  //
  // validateCell() accumulates its OWN sum, over the composition vector it
  // is handed rather than over fields_. The two agree because validate()
  // builds that vector from fields_ in this same ascending order
  // immediately before summing it, so both perform the identical sequence
  // of additions on the identical values -- but that is an agreement of
  // construction, NOT something this function enforces. Change the order in
  // either loop and the other must change with it.
  Real sum = 0.0;
  for (Index i = 0; i < fields_.size(); ++i) {
    sum += fields_[i][cell];
  }
  return sum;
}

void MassFractionState::validateCell(Index cell, const std::vector<Real>& composition) const {
  if (composition.size() != registry_.size()) {
    throw InvalidArgumentError("composition for cell " + std::to_string(cell) + " has " +
                               std::to_string(composition.size()) + " entries; expected " +
                               std::to_string(registry_.size()));
  }

  Real sum = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    const Real value = composition[i];
    if (!std::isfinite(value)) {
      throw InvalidArgumentError("mass fraction for species \"" + registry_.name(i) +
                                 "\" at cell " + std::to_string(cell) + " is not finite (" +
                                 exact(value) + ")");
    }
    // Boolean, no tolerance -- G2(b). A value of -1e-12 must fail here,
    // which is exactly what a tolerance would hide.
    if (value < 0.0 || value > 1.0) {
      throw InvalidArgumentError("mass fraction for species \"" + registry_.name(i) +
                                 "\" at cell " + std::to_string(cell) + " is " + exact(value) +
                                 "; must be in [0, 1]");
    }
    sum += value;
  }

  const Real tolerance = massFractionSumTolerance(registry_.size());
  if (std::abs(sum - 1.0) > tolerance) {
    throw InvalidArgumentError("mass fractions at cell " + std::to_string(cell) + " sum to " +
                               exact(sum) + "; must be within " + exact(tolerance) +
                               " of 1 (deviation " + exact(std::abs(sum - 1.0)) + ")");
  }
}

void MassFractionState::validate() const {
  // Per cell, not aggregate: section 16 -- one invalid cell among thousands
  // must not hide behind a global min/max/mean. Reports the first offender.
  std::vector<Real> composition(registry_.size());
  for (Index cell = 0; cell < cellCount_; ++cell) {
    for (Index i = 0; i < registry_.size(); ++i) {
      composition[i] = fields_[i][cell];
    }
    validateCell(cell, composition);
  }
}

void MassFractionState::setCellComposition(Index cell, const std::vector<Real>& composition) {
  if (cell >= cellCount_) {
    throw InvalidArgumentError("cell " + std::to_string(cell) + " is out of range; state holds " +
                               std::to_string(cellCount_) + " cells");
  }
  // Validate the WHOLE composition before writing any of it, so a rejected
  // call leaves the state bitwise unchanged rather than half-applied.
  validateCell(cell, composition);
  for (Index i = 0; i < registry_.size(); ++i) {
    fields_[i][cell] = composition[i];
  }
}

std::vector<Real> normalizeMassFractions(const std::vector<Real>& composition) {
  if (composition.empty()) {
    throw InvalidArgumentError("cannot normalise an empty composition");
  }

  Real sum = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    const Real value = composition[i];
    if (!std::isfinite(value)) {
      throw InvalidArgumentError(
          "cannot normalise a composition with a non-finite entry at index " + std::to_string(i) +
          " (" + exact(value) + ")");
    }
    // Negative entries are refused: a negative mass fraction is not an
    // un-normalised ratio, and dividing through would keep it negative
    // while making the sum 1 -- an invalid state wearing a valid sum.
    if (value < 0.0) {
      throw InvalidArgumentError("cannot normalise a composition with a negative entry at index " +
                                 std::to_string(i) + " (" + exact(value) + ")");
    }
    sum += value;
  }

  if (sum == 0.0) {
    throw InvalidArgumentError("cannot normalise a composition that sums to zero");
  }
  if (!std::isfinite(sum)) {
    throw InvalidArgumentError("cannot normalise a composition whose sum is not finite (" +
                               exact(sum) + ")");
  }

  std::vector<Real> normalized(composition.size());
  for (Index i = 0; i < composition.size(); ++i) {
    normalized[i] = composition[i] / sum;
  }
  return normalized;
}

}  // namespace cfd::species
