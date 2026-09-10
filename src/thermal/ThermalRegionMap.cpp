#include "cfd/thermal/ThermalRegionMap.hpp"

#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::thermal {

ThermalRegionMap::ThermalRegionMap(std::vector<ThermalRegion> regions,
                                   std::vector<Index> cellRegionIndex)
    : regions_(std::move(regions)), cellRegionIndex_(std::move(cellRegionIndex)) {
  if (regions_.empty()) {
    throw InvalidArgumentError("ThermalRegionMap: regions must not be empty");
  }
  for (std::size_t i = 0; i < regions_.size(); ++i) {
    for (std::size_t j = i + 1; j < regions_.size(); ++j) {
      if (regions_[i].name == regions_[j].name) {
        throw InvalidArgumentError("ThermalRegionMap: duplicate region name \"" + regions_[i].name +
                                   "\"");
      }
    }
  }
  if (cellRegionIndex_.empty()) {
    throw InvalidArgumentError("ThermalRegionMap: cellRegionIndex must not be empty");
  }
  for (const Index regionIndex : cellRegionIndex_) {
    if (regionIndex >= regions_.size()) {
      throw InvalidArgumentError("ThermalRegionMap: cellRegionIndex entry out of range");
    }
  }
}

const ThermalRegion& ThermalRegionMap::regionForCell(Index cellId) const {
  if (cellId >= cellRegionIndex_.size()) {
    throw InvalidArgumentError("ThermalRegionMap::regionForCell: cellId out of range");
  }
  return regions_[cellRegionIndex_[cellId]];
}

bool ThermalRegionMap::sameRegion(Index cellA, Index cellB) const noexcept {
  if (cellA >= cellRegionIndex_.size() || cellB >= cellRegionIndex_.size()) return false;
  return cellRegionIndex_[cellA] == cellRegionIndex_[cellB];
}

std::size_t ThermalRegionMap::numberOfCells() const noexcept { return cellRegionIndex_.size(); }

const std::vector<ThermalRegion>& ThermalRegionMap::regions() const noexcept { return regions_; }

}  // namespace cfd::thermal
