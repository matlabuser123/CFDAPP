#pragma once

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/thermal/ThermalRegion.hpp"

namespace cfd::thermal {

// P2-THERMAL-005: which material each mesh cell belongs to. A dense,
// one-entry-per-cell array (cellRegionIndex[cellId] -> an index into
// regions()) rather than a sparse/per-cell-polymorphic design -- TODO.md
// P2 -- Thermal's own "avoid per-cell virtual polymorphism if a simple
// deterministic region-id map is sufficient" guidance. This shape also
// structurally rules out two failure modes the task spec calls out
// ("unassigned cells", "overlapping assignments") by construction: every
// cell has exactly one array slot, so there is nothing left to validate
// for those two cases beyond what the constructor already checks
// (in-range indices, correct array length).
class ThermalRegionMap {
 public:
  // Throws InvalidArgumentError if regions is empty, if any two regions
  // share a name, if cellRegionIndex is empty, or if any entry of
  // cellRegionIndex is >= regions.size() ("unknown region id", rejected
  // rather than silently clamped or defaulted).
  ThermalRegionMap(std::vector<ThermalRegion> regions, std::vector<Index> cellRegionIndex);

  [[nodiscard]] const ThermalRegion& regionForCell(Index cellId) const;
  [[nodiscard]] bool sameRegion(Index cellA, Index cellB) const noexcept;
  [[nodiscard]] std::size_t numberOfCells() const noexcept;
  [[nodiscard]] const std::vector<ThermalRegion>& regions() const noexcept;

 private:
  std::vector<ThermalRegion> regions_;
  std::vector<Index> cellRegionIndex_;
};

}  // namespace cfd::thermal
