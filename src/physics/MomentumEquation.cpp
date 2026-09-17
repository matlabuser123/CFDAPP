#include "cfd/physics/MomentumEquation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::physics {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::VectorBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

Real selectComponent(const Vector2& v, VelocityComponent component) {
  return velocityComponentValue(v, component);
}

// The gradient of `component` in a computed velocity gradient (P12-MESH-006:
// gradW for W, which computeVelocityGradient fills on a 3D mesh).
const VectorField* componentGradient(
    const std::optional<cfd::discretization::VelocityGradientField>& gradient,
    VelocityComponent component) {
  if (!gradient.has_value()) return nullptr;
  switch (component) {
    case VelocityComponent::U:
      return &gradient->gradU;
    case VelocityComponent::V:
      return &gradient->gradV;
    case VelocityComponent::W:
      return &gradient->gradW;
  }
  return nullptr;
}

// Evaluates the boundary velocity for `face` (must be a boundary face)
// against the *current* owner value -- exact for Wall/MovingWall/Inlet
// (which ignore ownerValue), and a lagged/linearized approximation for
// Outlet/Symmetry (which depend on it). This is the same evaluate-at-
// current-state pattern cfd::discretization::interpolateFace already
// uses; matrix assembly cannot do better without turning Outlet/Symmetry
// into an implicit relation, which is out of scope for this phase.
Vector2 boundaryVelocity(const Mesh& mesh, const Face& face, const VectorField& velocity,
                         const BoundaryConditionSet& velocityBoundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), velocityBoundaries);
  const auto* vectorBc = dynamic_cast<const VectorBoundaryCondition*>(&bc);
  if (vectorBc == nullptr) {
    throw InvalidArgumentError("MomentumEquation: boundary condition is not vector-valued");
  }
  const Vector2& ownerValue = velocity[face.owner()];
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Vector2 unitNormal = MeshGeometry::unitNormal(face);
  return vectorBc->boundaryValue(ownerValue, distance, unitNormal);
}

// P12-NUM-003: whether `face`'s velocity condition prescribes the boundary
// value (Wall/MovingWall/Inlet) -- see
// cfd::discretization::prescribesBoundaryValue.
bool prescribesVelocity(const Mesh& mesh, const Face& face,
                        const BoundaryConditionSet& velocityBoundaries) {
  return cfd::discretization::prescribesBoundaryValue(
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), velocityBoundaries).type());
}

}  // namespace

void assembleDiffusionContribution(const Mesh& mesh, Real dynamicViscosity,
                                   const VectorField& velocity,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component, SparseMatrixBuilder& builder,
                                   Vector& rhs, bool applyNonOrthogonalCorrection,
                                   cfd::discretization::GradientScheme correctionGradientScheme) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: velocity size does not match mesh cell count");
  }

  // P12-NUM-003: computed once, only when requested -- every pre-
  // P12-NUM-003 call site (which never passes this flag) pays no extra
  // cost at all and is byte-identical to before this parameter existed.
  // P12-DIFF-002 A2: always computed -- the Dirichlet wall-flux reconstruction needs it and
  // WHICH wall-flux scheme is used must not depend on the iterative non-orthogonal control
  // (a2/activation_architecture.md). applyNonOrthogonalCorrection now gates only the
  // INTERNAL-face correction.
  std::optional<cfd::discretization::VelocityGradientField> velocityGradient =
      cfd::discretization::computeVelocityGradient(mesh, velocity, velocityBoundaries,
                                                   correctionGradientScheme);
  const VectorField* gradPhi = componentGradient(velocityGradient, component);
  const VectorField* internalGradPhi = applyNonOrthogonalCorrection ? gradPhi : nullptr;

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      // P12-NUM-003: the shared face-diffusion geometry (corrected only for a
      // Dirichlet-type velocity face when the correction is enabled;
      // otherwise exactly the pre-existing mu*|Sf|/distance).
      const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
          mesh, face, dynamicViscosity, distance, gradPhi,
          prescribesVelocity(mesh, face, velocityBoundaries));
      const Real diffusionCoefficient = terms.coefficient;

      const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
      const Real phiB = selectComponent(uB, component);
      // A2: always applied -- the transfer term is exactly 0 on an orthogonal face.
      rhs[ownerId] += terms.explicitFlux;

      // A(P,P) += Df; the -Df*phiB (known) part of the boundary flux
      // moves to the RHS as +Df*phiB (see TODO.md section 10/26 sign
      // derivation in the physics assembly comment above).
      // P12-DIFF-002: the prescribed value carries its own coefficient
      // (equal to Df for the two-point form), and the second-order
      // reconstruction adds one implicit entry coupling this cell to the far
      // cell across its opposite interior face.
      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += terms.boundaryValueCoefficient * phiB;
      if (terms.farCellCoefficient != 0.0) {
        builder.add(ownerId, terms.farCell, -terms.farCellCoefficient);
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    // P12-NUM-003: shared face-diffusion geometry -- see
    // NonOrthogonalDiffusion.hpp. The known part moves to the RHS
    // face-once, equal/opposite (sign derivation: this function's header).
    const auto terms = cfd::discretization::internalFaceDiffusionTerms(mesh, face, dynamicViscosity,
                                                                       dPN, internalGradPhi);
    const Real diffusionCoefficient = terms.coefficient;
    if (internalGradPhi != nullptr) {
      rhs[ownerId] += terms.explicitFlux;
      rhs[neighborId] -= terms.explicitFlux;
    }

    // Face-once assembly: both rows' equal/opposite contributions are
    // added right here, from a single face visit (TODO.md section 51).
    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleDiffusionContribution(const Mesh& mesh, const ScalarField& effectiveViscosity,
                                   const VectorField& velocity,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component, SparseMatrixBuilder& builder,
                                   Vector& rhs, bool applyNonOrthogonalCorrection,
                                   cfd::discretization::GradientScheme correctionGradientScheme,
                                   const VectorField* correctionVelocity) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: velocity size does not match mesh cell count");
  }
  if (correctionVelocity != nullptr && correctionVelocity->size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: correctionVelocity size does not match mesh cell count");
  }
  if (effectiveViscosity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleDiffusionContribution: effectiveViscosity size does not match mesh cell count");
  }
  // A diffusion coefficient built from a non-finite or non-positive
  // mu_eff would either corrupt the assembled system with a NaN/Inf (only
  // sometimes caught downstream by matrix.allFinite()) or silently flip
  // the sign of a diffusion term for a negative-but-finite value (never
  // caught downstream at all) -- reject it here, at the one point mu_eff
  // enters momentum assembly, the same "validate at the entry point"
  // convention FluidProperties's own constructor already applies to
  // molecular viscosity.
  for (Index i = 0; i < effectiveViscosity.size(); ++i) {
    if (!std::isfinite(effectiveViscosity[i]) || !(effectiveViscosity[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleDiffusionContribution: effectiveViscosity must be finite and > 0 in every "
          "cell");
    }
  }

  // P12-NUM-003: same as the constant-viscosity overload above, except
  // the gradient may come from `correctionVelocity` (see header comment).
  // P12-DIFF-002 A2: always computed -- the Dirichlet wall-flux reconstruction needs it and
  // WHICH wall-flux scheme is used must not depend on the iterative non-orthogonal control
  // (a2/activation_architecture.md). applyNonOrthogonalCorrection now gates only the
  // INTERNAL-face correction.
  std::optional<cfd::discretization::VelocityGradientField> velocityGradient =
      cfd::discretization::computeVelocityGradient(
          mesh, (correctionVelocity != nullptr) ? *correctionVelocity : velocity,
          velocityBoundaries, correctionGradientScheme);
  const VectorField* gradPhi = componentGradient(velocityGradient, component);
  const VectorField* internalGradPhi = applyNonOrthogonalCorrection ? gradPhi : nullptr;

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      // No neighbor cell to interpolate against at a boundary face -- use
      // the owner cell's own effective viscosity (see this overload's
      // header comment).
      const Real muFace = effectiveViscosity[ownerId];
      const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
          mesh, face, muFace, distance, gradPhi,
          prescribesVelocity(mesh, face, velocityBoundaries));
      const Real diffusionCoefficient = terms.coefficient;

      const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
      const Real phiB = selectComponent(uB, component);
      // A2: always applied -- the transfer term is exactly 0 on an orthogonal face.
      rhs[ownerId] += terms.explicitFlux;

      // P12-DIFF-002: as in the constant-viscosity overload above.
      builder.add(ownerId, ownerId, diffusionCoefficient);
      rhs[ownerId] += terms.boundaryValueCoefficient * phiB;
      if (terms.farCellCoefficient != 0.0) {
        builder.add(ownerId, terms.farCell, -terms.farCellCoefficient);
      }
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
    const Real muFace =
        cfd::discretization::interpolateInternalFace(mesh, face, effectiveViscosity);
    const auto terms =
        cfd::discretization::internalFaceDiffusionTerms(mesh, face, muFace, dPN, internalGradPhi);
    const Real diffusionCoefficient = terms.coefficient;
    if (internalGradPhi != nullptr) {
      rhs[ownerId] += terms.explicitFlux;
      rhs[neighborId] -= terms.explicitFlux;
    }

    builder.add(ownerId, ownerId, diffusionCoefficient);
    builder.add(ownerId, neighborId, -diffusionCoefficient);
    builder.add(neighborId, neighborId, diffusionCoefficient);
    builder.add(neighborId, ownerId, -diffusionCoefficient);
  }
}

void assembleConvectionContribution(const Mesh& mesh, const SurfaceField& massFlux,
                                    const VectorField& velocity,
                                    const BoundaryConditionSet& velocityBoundaries,
                                    VelocityComponent component, SparseMatrixBuilder& builder,
                                    Vector& rhs, cfd::discretization::ConvectionScheme scheme) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleConvectionContribution: velocity size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleConvectionContribution: massFlux size does not match mesh face count");
  }

  // P12-NUM-001: computed once, only for LinearUpwind -- Upwind (the
  // default, every pre-P12-NUM-001 call site) pays no extra cost at all
  // and is byte-identical to before this parameter existed. Reuses
  // computeVelocityGradient (VectorGradient.hpp) rather than a second
  // gradient implementation here.
  std::optional<cfd::discretization::VelocityGradientField> velocityGradient;
  if (scheme == cfd::discretization::ConvectionScheme::LinearUpwind) {
    velocityGradient =
        cfd::discretization::computeVelocityGradient(mesh, velocity, velocityBoundaries);
  }
  const VectorField* gradPhi = componentGradient(velocityGradient, component);

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Real ownerFlux = massFlux[faceId];  // owner-oriented: >=0 means owner -> neighbor/out.

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      if (ownerFlux >= 0.0) {
        // Outflow: upwind value is the (unknown) owner value itself.
        builder.add(ownerId, ownerId, ownerFlux);
      } else {
        // Inflow: upwind value is the (known) boundary value -> RHS.
        const Vector2 uB = boundaryVelocity(mesh, face, velocity, velocityBoundaries);
        const Real phiB = selectComponent(uB, component);
        rhs[ownerId] -= ownerFlux * phiB;
      }
      // Boundary faces are unaffected by `scheme` regardless of choice
      // (P12-NUM-001 explicit scope limit -- results/p12-num-001/summary.md).
      continue;
    }

    const Index ownerId = face.owner();
    const Index neighborId = *face.neighbor();
    // Single upwind decision per face, reused for both rows (matches
    // cfd::discretization::convection's face-once evaluation). These
    // implicit coefficients are ALWAYS plain upwind, regardless of
    // `scheme` -- see this function's own header comment on deferred
    // correction.
    if (ownerFlux >= 0.0) {
      builder.add(ownerId, ownerId, ownerFlux);
      builder.add(neighborId, ownerId, -ownerFlux);
    } else {
      builder.add(ownerId, neighborId, ownerFlux);
      builder.add(neighborId, neighborId, -ownerFlux);
    }

    if (scheme == cfd::discretization::ConvectionScheme::Upwind) {
      continue;
    }

    // P12-NUM-001: explicit deferred correction, evaluated from the
    // current/lagged `velocity` -- the same lagged-current-state
    // convention this file's boundaryVelocity() already uses for
    // Outlet/Symmetry. Face-once, equal/opposite between the two rows,
    // matching every other contribution in this file.
    const bool ownerIsUpwind = ownerFlux >= 0.0;
    const Index upwindId = ownerIsUpwind ? ownerId : neighborId;
    const Index downwindId = ownerIsUpwind ? neighborId : ownerId;
    const Real phiUpwind = selectComponent(velocity[upwindId], component);
    const Real phiDownwind = selectComponent(velocity[downwindId], component);
    const Vector2 upwindCentroid = mesh.cell(upwindId).centroid();

    // Far-upstream cell (QUICK's own "C"), shared with the TVD limiter
    // below -- see cfd::discretization::smoothnessRatio's own header
    // comment for why every scheme (not just QUICK) needs it.
    std::optional<Index> farCellId;
    Real hCU = 0.0;
    if (const std::optional<Index> farFaceId =
            MeshGeometry::oppositeInteriorFace(mesh, mesh.cell(upwindId), face);
        farFaceId.has_value()) {
      const Face& farFace = mesh.face(*farFaceId);
      farCellId = (farFace.owner() == upwindId) ? *farFace.neighbor() : farFace.owner();
      hCU = MeshGeometry::ownerNeighborDistance(mesh, farFace);
    }

    Real phiHighOrder = phiUpwind;
    switch (scheme) {
      case cfd::discretization::ConvectionScheme::Upwind:
        break;  // unreachable: handled by the `continue` above.
      case cfd::discretization::ConvectionScheme::Central: {
        const Vector2 uFace = cfd::discretization::interpolateInternalFace(mesh, face, velocity);
        phiHighOrder = selectComponent(uFace, component);
        break;
      }
      case cfd::discretization::ConvectionScheme::LinearUpwind: {
        phiHighOrder = cfd::discretization::linearUpwindFaceValue(phiUpwind, (*gradPhi)[upwindId],
                                                                  upwindCentroid, face.centroid());
        break;
      }
      case cfd::discretization::ConvectionScheme::QUICK: {
        if (farCellId.has_value()) {
          const Real hUf = MeshGeometry::distance(upwindCentroid, face.centroid());
          const Real hfD =
              MeshGeometry::distance(face.centroid(), mesh.cell(downwindId).centroid());
          const Real phiC = selectComponent(velocity[*farCellId], component);
          phiHighOrder =
              cfd::discretization::quickFaceValue(phiC, hCU, phiUpwind, hUf, phiDownwind, hfD);
        }
        // else: no further-upstream interior neighbor for this upwind
        // cell -- documented deterministic fallback, phiHighOrder stays
        // phiUpwind (zero correction), never an out-of-bounds read.
        break;
      }
    }

    // P12-NUM-001 boundedness: Sweby (1984) TVD blend -- see
    // cfd::discretization::smoothnessRatio/vanLeerLimiter's own header
    // comment. A raw per-face clip is NOT sufficient on its own (two
    // independently-clipped faces of the same cell can still combine
    // into an out-of-bounds update) -- this blend is, plus the final
    // clamp below as an unconditional safety net. psi is 0 (pure upwind)
    // whenever no far-upstream cell exists -- NOT 1 ("assume smooth"):
    // at a boundary-adjacent cell, the boundary's own face already uses
    // an upwind-consistent stand-in value whose accuracy relies on
    // EVERY face of that cell using the same convention (a telescoping-
    // cancellation property, see upwindBoundaryFaceValue/
    // upwindFaceValue's own comments) -- blending the other face toward
    // a genuinely higher-order value there breaks that cancellation and
    // introduces a bias that does not shrink under refinement (see
    // cfd::discretization::convection's identical policy and
    // results/p12-num-001/summary.md for the full derivation).
    const std::optional<Real> r =
        farCellId.has_value()
            ? cfd::discretization::smoothnessRatio(selectComponent(velocity[*farCellId], component),
                                                   phiUpwind, phiDownwind)
            : std::nullopt;
    const Real psi = cfd::discretization::vanLeerLimiter(r);
    const Real blended = phiUpwind + (psi * (phiHighOrder - phiUpwind));
    const Real phiFace =
        std::clamp(blended, std::min(phiUpwind, phiDownwind), std::max(phiUpwind, phiDownwind));
    const Real correction = ownerFlux * (phiFace - phiUpwind);
    rhs[ownerId] -= correction;
    rhs[neighborId] += correction;
  }
}

void assemblePressureSourceContribution(const Mesh& mesh, const ScalarField& pressure,
                                        const BoundaryConditionSet& pressureBoundaries,
                                        VelocityComponent component, Vector& rhs,
                                        cfd::discretization::GradientScheme scheme) {
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assemblePressureSourceContribution: pressure size does not match mesh cell count");
  }

  const VectorField gradP =
      cfd::discretization::gradient(mesh, pressure, pressureBoundaries, scheme);
  for (const auto& cell : mesh.cells()) {
    const Real gradComponent = selectComponent(gradP[cell.id()], component);
    rhs[cell.id()] += -cell.volume() * gradComponent;
  }
}

void assembleBuoyancySourceContribution(const Mesh& mesh, const ScalarField& temperature,
                                        const BoussinesqBuoyancy& buoyancy,
                                        VelocityComponent component, Vector& rhs) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleBuoyancySourceContribution: temperature size does not match mesh cell count");
  }
  for (const auto& cell : mesh.cells()) {
    const Vector2 sourcePerVolume = buoyancy.source(temperature[cell.id()]);
    rhs[cell.id()] += cell.volume() * selectComponent(sourcePerVolume, component);
  }
}

void assembleMomentumSourceContribution(const Mesh& mesh, const VectorField& sourcePerUnitVolume,
                                        VelocityComponent component, Vector& rhs) {
  if (sourcePerUnitVolume.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "assembleMomentumSourceContribution: source size does not match mesh cell count");
  }
  for (const auto& cell : mesh.cells()) {
    const Vector2& source = sourcePerUnitVolume[cell.id()];
    if (!isFinite(source)) {
      throw InvalidArgumentError("assembleMomentumSourceContribution: source must be finite");
    }
    rhs[cell.id()] += cell.volume() * selectComponent(source, component);
  }
}

namespace {

MomentumAssembly assembleComponent(const Mesh& mesh, const VectorField& velocity,
                                   const ScalarField& pressure, const SurfaceField& massFlux,
                                   const FluidProperties& fluid,
                                   const BoundaryConditionSet& velocityBoundaries,
                                   const BoundaryConditionSet& pressureBoundaries,
                                   VelocityComponent component) {
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  assembleDiffusionContribution(mesh, fluid.dynamicViscosity(), velocity, velocityBoundaries,
                                component, builder, rhs);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs);

  SparseMatrix matrix = builder.build();

  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("assembleMomentum: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

}  // namespace

MomentumSystems assembleMomentum(const Mesh& mesh, const VectorField& velocity,
                                 const ScalarField& pressure, const SurfaceField& massFlux,
                                 const FluidProperties& fluid,
                                 const BoundaryConditionSet& velocityBoundaries,
                                 const BoundaryConditionSet& pressureBoundaries) {
  cfd::mesh::requireTwoDimensional(mesh, "assembleMomentum");
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("assembleMomentum: velocity size does not match mesh cell count");
  }
  if (pressure.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("assembleMomentum: pressure size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("assembleMomentum: massFlux size does not match mesh face count");
  }

  MomentumAssembly u =
      assembleComponent(mesh, velocity, pressure, massFlux, fluid, velocityBoundaries,
                        pressureBoundaries, VelocityComponent::U);
  MomentumAssembly v =
      assembleComponent(mesh, velocity, pressure, massFlux, fluid, velocityBoundaries,
                        pressureBoundaries, VelocityComponent::V);
  return MomentumSystems{std::move(u), std::move(v)};
}

}  // namespace cfd::physics
