#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::boundary {

enum class BoundaryConditionType {
  FixedValue,
  FixedGradient,
  Wall,
  MovingWall,
  Inlet,
  Outlet,
  Symmetry,
  // Thermal-specific scalar conditions (P2 -- Thermal, P2-THERMAL-003):
  // mathematically FixedTemperature==FixedValue and HeatFlux/Adiabatic
  // are Neumann conditions like FixedGradient, but each gets its own
  // type/name here rather than reusing the generic ones -- same
  // precedent as Outlet (a named, physically-meaningful zero-gradient
  // condition distinct from a bare FixedGradient(0.0), see Outlet.hpp).
  FixedTemperature,
  HeatFlux,
  Adiabatic,
  // P2-TURB-006: SST's near-wall omega Dirichlet value (Wilcox's
  // asymptotic formula, evaluated at the owner cell's own distance to
  // the wall face) -- see WallOmega.hpp. Its own named type for the same
  // reason as the thermal conditions above: physically meaningful and
  // distinct from a bare FixedValue/FixedGradient.
  WallOmega,
};

// Minimal common interface: identity only. Deliberately does not force a
// single apply(field) signature -- finite-volume BCs ultimately modify
// equation coefficients (aP, source), not just overwrite cell/face
// values, so ScalarBoundaryCondition/VectorBoundaryCondition instead
// expose boundary-evaluation semantics that a later discretization layer
// can query. See PROJECT_STRUCTURE.md:
//
//   Boundary conditions specify the mathematical boundary information;
//   discretization operators decide how that information modifies the
//   finite-volume matrix.
class BoundaryCondition {
 public:
  virtual ~BoundaryCondition() = default;

  [[nodiscard]] virtual BoundaryConditionType type() const noexcept = 0;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

class ScalarBoundaryCondition : public BoundaryCondition {
 public:
  // The boundary (face) value implied by this condition, given the owner
  // cell's value and the normal distance from the owner centroid to the
  // boundary face.
  [[nodiscard]] virtual Real boundaryValue(Real ownerValue, Real normalDistance) const = 0;
};

class VectorBoundaryCondition : public BoundaryCondition {
 public:
  // unitNormal is the boundary face's outward unit normal -- needed by
  // Symmetry's projection; the other vector BCs (Wall, MovingWall, Inlet,
  // Outlet) ignore it.
  [[nodiscard]] virtual Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                              const Vector2& unitNormal) const = 0;
};

// Associates BoundaryCondition instances with mesh patches by name. Mesh
// continues to own BoundaryPatch/Face/Cell; this only holds physical
// configuration, validated against the mesh's actual patches.
class BoundaryConditionSet {
 public:
  BoundaryConditionSet() = default;
  // Move-only (conditions_ holds std::unique_ptr) -- previously left
  // implicit, made explicit because at least one MSVC STL build
  // instantiates std::vector<T>'s reallocation path (e.g.
  // std::vector<SpeciesSetup>::push_back(), SpeciesSetup holding a
  // BoundaryConditionSet by value -- see SimulationSetup.hpp) by
  // attempting the copy constructor rather than consulting
  // is_nothrow_move_constructible when a class's move members are only
  // implicitly declared; explicit noexcept move + deleted copy sidesteps
  // it without changing this type's behavior at all (it was already
  // move-only, just implicitly).
  BoundaryConditionSet(const BoundaryConditionSet&) = delete;
  BoundaryConditionSet& operator=(const BoundaryConditionSet&) = delete;
  BoundaryConditionSet(BoundaryConditionSet&&) noexcept = default;
  BoundaryConditionSet& operator=(BoundaryConditionSet&&) noexcept = default;

  // Throws InvalidArgumentError if patchName does not name a patch on
  // mesh, or if patchName already has a condition assigned -- call
  // replace() to change an existing assignment intentionally.
  void set(const cfd::mesh::Mesh& mesh, std::string patchName,
           std::unique_ptr<BoundaryCondition> condition);

  // Like set(), but overwrites an existing assignment instead of
  // rejecting it.
  void replace(const cfd::mesh::Mesh& mesh, std::string patchName,
               std::unique_ptr<BoundaryCondition> condition);

  [[nodiscard]] bool has(std::string_view patchName) const noexcept;
  [[nodiscard]] const BoundaryCondition& get(std::string_view patchName) const;

  // Patches on mesh that have no assigned condition yet -- lets a caller
  // report "missing velocity BC for patch 'right'" before assembly
  // rather than failing deep inside SIMPLE.
  [[nodiscard]] std::vector<std::string> missingPatches(const cfd::mesh::Mesh& mesh) const;

 private:
  std::map<std::string, std::unique_ptr<BoundaryCondition>, std::less<>> conditions_;
};

// Finds the name of the boundary patch that owns faceId. Throws
// InvalidArgumentError if faceId is not part of any patch (e.g. an
// internal face). Used by the discretization layer to resolve which
// condition applies to a given boundary face.
[[nodiscard]] std::string_view boundaryPatchNameForFace(const cfd::mesh::Mesh& mesh, Index faceId);

// Convenience: boundaries.get(boundaryPatchNameForFace(mesh, faceId)).
[[nodiscard]] const BoundaryCondition& boundaryConditionForFace(
    const cfd::mesh::Mesh& mesh, Index faceId, const BoundaryConditionSet& boundaries);

}  // namespace cfd::boundary
