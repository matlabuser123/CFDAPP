#include "cfd/boundary/BoundaryCondition.hpp"

#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

namespace {

bool patchExists(const cfd::mesh::Mesh& mesh, std::string_view patchName) {
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == patchName) {
      return true;
    }
  }
  return false;
}

}  // namespace

void BoundaryConditionSet::set(const cfd::mesh::Mesh& mesh, std::string patchName,
                               std::unique_ptr<BoundaryCondition> condition) {
  if (!patchExists(mesh, patchName)) {
    throw InvalidArgumentError("BoundaryConditionSet: unknown patch '" + patchName + "'");
  }
  if (conditions_.find(patchName) != conditions_.end()) {
    throw InvalidArgumentError("BoundaryConditionSet: patch '" + patchName +
                               "' already has a condition assigned (use replace() instead)");
  }
  conditions_.emplace(std::move(patchName), std::move(condition));
}

void BoundaryConditionSet::replace(const cfd::mesh::Mesh& mesh, std::string patchName,
                                   std::unique_ptr<BoundaryCondition> condition) {
  if (!patchExists(mesh, patchName)) {
    throw InvalidArgumentError("BoundaryConditionSet: unknown patch '" + patchName + "'");
  }
  conditions_[patchName] = std::move(condition);
}

bool BoundaryConditionSet::has(std::string_view patchName) const noexcept {
  return conditions_.find(patchName) != conditions_.end();
}

const BoundaryCondition& BoundaryConditionSet::get(std::string_view patchName) const {
  const auto it = conditions_.find(patchName);
  if (it == conditions_.end()) {
    throw InvalidArgumentError("BoundaryConditionSet: no condition assigned for patch '" +
                               std::string(patchName) + "'");
  }
  return *it->second;
}

std::vector<std::string> BoundaryConditionSet::missingPatches(const cfd::mesh::Mesh& mesh) const {
  std::vector<std::string> missing;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (!has(patch.name())) {
      missing.push_back(patch.name());
    }
  }
  return missing;
}

std::string_view boundaryPatchNameForFace(const cfd::mesh::Mesh& mesh, Index faceId) {
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index candidateId : patch.faceIds()) {
      if (candidateId == faceId) {
        return patch.name();
      }
    }
  }
  throw InvalidArgumentError("boundaryPatchNameForFace: face " + std::to_string(faceId) +
                             " does not belong to any boundary patch");
}

const BoundaryCondition& boundaryConditionForFace(const cfd::mesh::Mesh& mesh, Index faceId,
                                                  const BoundaryConditionSet& boundaries) {
  return boundaries.get(boundaryPatchNameForFace(mesh, faceId));
}

}  // namespace cfd::boundary
