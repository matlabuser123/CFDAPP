#include "cfd/turbulence/TurbulenceModelFactory.hpp"

#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/LaminarModel.hpp"

namespace cfd::turbulence {

using cfd::boundary::BoundaryConditionSet;
using cfd::mesh::Mesh;
using cfd::physics::FluidProperties;

std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const Mesh& mesh, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& kBoundaries,
    const BoundaryConditionSet& epsilonBoundaries, KEpsilonConfig config) {
  if (modelName == "laminar") {
    return std::make_unique<LaminarModel>(mesh);
  }
  if (modelName == "k_epsilon") {
    return std::make_unique<KEpsilonModel>(mesh, fluid, velocityBoundaries, kBoundaries,
                                           epsilonBoundaries, std::move(config));
  }
  // "k_omega"/"sst"/anything else: explicitly unsupported through *this*
  // overload, not a placeholder -- P2-TURB-004 section 27 (k_omega has
  // its own KOmegaConfig overload below; requesting it here anyway is a
  // caller error, not silently redirected).
  throw InvalidArgumentError(std::string("createTurbulenceModel: unsupported turbulence model \"") +
                             std::string(modelName) + "\"");
}

std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const Mesh& mesh, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& kBoundaries,
    const BoundaryConditionSet& omegaBoundaries, KOmegaConfig config) {
  if (modelName == "laminar") {
    return std::make_unique<LaminarModel>(mesh);
  }
  if (modelName == "k_omega") {
    return std::make_unique<KOmegaModel>(mesh, fluid, velocityBoundaries, kBoundaries,
                                         omegaBoundaries, std::move(config));
  }
  // "k_epsilon"/"sst"/anything else: explicitly unsupported through
  // *this* overload -- P2-TURB-005 section 28.
  throw InvalidArgumentError(std::string("createTurbulenceModel: unsupported turbulence model \"") +
                             std::string(modelName) + "\"");
}

std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const Mesh& mesh, const FluidProperties& fluid,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& kBoundaries,
    const BoundaryConditionSet& omegaBoundaries, SSTConfig config) {
  if (modelName == "laminar") {
    return std::make_unique<LaminarModel>(mesh);
  }
  if (modelName == "sst") {
    return std::make_unique<SSTModel>(mesh, fluid, velocityBoundaries, kBoundaries,
                                      omegaBoundaries, std::move(config));
  }
  // "k_epsilon"/"k_omega"/anything else: explicitly unsupported through
  // *this* overload -- P2-TURB-006 section 36.
  throw InvalidArgumentError(std::string("createTurbulenceModel: unsupported turbulence model \"") +
                             std::string(modelName) + "\"");
}

}  // namespace cfd::turbulence
