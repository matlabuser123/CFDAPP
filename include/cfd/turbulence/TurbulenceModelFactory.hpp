#pragma once

#include <memory>
#include <string_view>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::turbulence {

// P2-TURB-004 section 27 (extended by P2-TURB-005 section 28): the one
// place a model-name string (as it appears in case configuration, e.g.
// "laminar"/"k_epsilon"/"k_omega") is turned into a concrete
// TurbulenceModel. `kBoundaries`/`epsilonBoundaries` are only read for
// modelName == "k_epsilon" (KEpsilonModel::name() itself still reports
// "kEpsilon", matching TurbulenceModel.hpp's own "kEpsilon, kOmegaSST"
// naming convention -- the JSON-facing snake_case selector string and
// the runtime camelCase identity label are deliberately two separate
// vocabularies for two separate audiences, the same way this codebase's
// "incompressible_laminar" PhysicsConfig::model string is unrelated to
// any internal C++ type name).
//
// Throws InvalidArgumentError for "k_omega"/"sst"/any other name --
// explicitly, not a silently-accepted placeholder (section 27's own "no
// placeholders" instruction): k-omega does not exist through *this*
// overload (see the KOmegaConfig overload below for that), and sst does
// not exist at all yet, so failing loudly at case-load time is strictly
// better than pretending to honor an unsupported request.
[[nodiscard]] std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const cfd::mesh::Mesh& mesh,
    const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& kBoundaries,
    const cfd::boundary::BoundaryConditionSet& epsilonBoundaries, KEpsilonConfig config);

// P2-TURB-005 section 28: the k-omega counterpart of the overload above
// -- distinguished purely by the config parameter's type (KEpsilonConfig
// vs. KOmegaConfig), matching how a caller already knows which model it
// is building a config for (CaseBuilder only ever constructs the one
// config matching physics.json's own "turbulence.model" string, never
// both). `kBoundaries`/`omegaBoundaries` are only read for modelName ==
// "k_omega"; modelName == "laminar" still returns a LaminarModel here
// too (both overloads agree on "laminar" -> LaminarModel, since that
// needs no model-specific config either way).
//
// Throws InvalidArgumentError for "k_epsilon"/"sst"/any other name --
// this overload only knows how to build a KOmegaConfig-shaped model, so
// requesting "k_epsilon" through it is a caller error, not silently
// redirected to the other overload.
[[nodiscard]] std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const cfd::mesh::Mesh& mesh,
    const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& kBoundaries,
    const cfd::boundary::BoundaryConditionSet& omegaBoundaries, KOmegaConfig config);

// P2-TURB-006 section 36: the SST counterpart of the two overloads
// above -- distinguished by the config parameter's type (SSTConfig).
// modelName == "laminar" still returns a LaminarModel here too (all
// three overloads agree on that). `kBoundaries`/`omegaBoundaries` are
// only read for modelName == "sst" (SST transports k/omega, the same
// two fields as standard k-omega, so it reuses the same "kBoundaries"/
// "omegaBoundaries" naming as the KOmegaConfig overload -- not a third,
// differently-named boundary pair).
//
// Throws InvalidArgumentError for "k_epsilon"/"k_omega"/any other name
// -- this overload only knows how to build an SSTConfig-shaped model.
[[nodiscard]] std::unique_ptr<TurbulenceModel> createTurbulenceModel(
    std::string_view modelName, const cfd::mesh::Mesh& mesh,
    const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& kBoundaries,
    const cfd::boundary::BoundaryConditionSet& omegaBoundaries, SSTConfig config);

}  // namespace cfd::turbulence
