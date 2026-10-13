#include "cfd/species/SpeciesTransport.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::species {
namespace {

using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

std::string exact(Real value) {
  std::ostringstream out;
  out.precision(std::numeric_limits<Real>::max_digits10);
  out << value;
  return out.str();
}

// Distance weights for an internal face, P14's convention:
//   g_f = (dNf*g_P + dPf*g_N) / (dPf + dNf)
// Returned as the OWNER's weight; the neighbour's is 1 - w, so any quantity
// interpolated this way from two cell values that each sum to 1 over species
// also sums to 1 over species -- which is what makes the closure exact at the
// face. See diffusion_closure.md section 3.
Real ownerWeight(const Mesh& mesh, const Face& face) {
  const Real dPf = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Real dNf = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
  const Real total = dPf + dNf;
  if (!(total > 0.0)) {
    return 0.5;
  }
  return dNf / total;
}

}  // namespace

SpeciesDiffusivities makeSpeciesDiffusivities(const ScalarField& density,
                                              const std::vector<Real>& diffusivities) {
  if (diffusivities.empty()) {
    throw InvalidArgumentError("makeSpeciesDiffusivities: at least one species is required");
  }
  if (density.empty()) {
    throw InvalidArgumentError("makeSpeciesDiffusivities: density field is empty");
  }
  for (Index cell = 0; cell < density.size(); ++cell) {
    if (!std::isfinite(density[cell]) || !(density[cell] > 0.0)) {
      throw InvalidArgumentError("makeSpeciesDiffusivities: density at cell " +
                                 std::to_string(cell) + " is " + exact(density[cell]) +
                                 "; must be finite and > 0");
    }
  }

  SpeciesDiffusivities result;
  result.gamma.reserve(diffusivities.size());
  for (Index i = 0; i < diffusivities.size(); ++i) {
    const Real d = diffusivities[i];
    if (!std::isfinite(d) || !(d > 0.0)) {
      throw InvalidArgumentError(
          "makeSpeciesDiffusivities: diffusivity for species index " + std::to_string(i) + " is " +
          exact(d) +
          "; must be finite and > 0 (pure advection is expressed by disabling the diffusion "
          "term, not by a zero diffusivity)");
    }
    ScalarField gamma(density.size());
    for (Index cell = 0; cell < density.size(); ++cell) {
      gamma[cell] = density[cell] * d;
    }
    result.gamma.push_back(std::move(gamma));
  }
  return result;
}

SpeciesSources zeroSpeciesSources(Index speciesCount, Index cellCount) {
  if (speciesCount == 0) {
    throw InvalidArgumentError("zeroSpeciesSources: at least one species is required");
  }
  if (cellCount == 0) {
    throw InvalidArgumentError("zeroSpeciesSources: at least one cell is required");
  }
  SpeciesSources sources;
  sources.perSpecies.reserve(speciesCount);
  for (Index i = 0; i < speciesCount; ++i) {
    // Explicitly zero, not default-constructed-and-hoped: omega_i = 0 is
    // P18.2's authoritative physical limit and must be verifiably exact.
    sources.perSpecies.push_back(cfd::transport::ScalarSourceFields{ScalarField(cellCount, 0.0),
                                                                    ScalarField(cellCount, 0.0)});
  }
  return sources;
}

Real closureTolerance(Index speciesCount, Real maxAbsoluteRaw) noexcept {
  // Frozen in diffusion_closure.md section 7 before any measurement:
  // 8 * eps * N_species * max|J^0|. Relative, because an absolute tolerance on
  // a dimensional flux is meaningless across meshes and diffusivities.
  return 8.0 * std::numeric_limits<Real>::epsilon() * static_cast<Real>(speciesCount) *
         maxAbsoluteRaw;
}

SpeciesDiffusiveFluxes speciesDiffusiveFaceFluxes(
    const Mesh& mesh, const SpeciesRegistry& registry, const std::vector<ScalarField>& fieldSet,
    const SpeciesDiffusivities& diffusivities,
    const std::vector<const cfd::boundary::BoundaryConditionSet*>& boundaries) {
  const Index ns = registry.size();
  if (fieldSet.size() != ns) {
    throw InvalidArgumentError("speciesDiffusiveFaceFluxes: " + std::to_string(fieldSet.size()) +
                               " fields for " + std::to_string(ns) + " species");
  }
  if (diffusivities.gamma.size() != ns) {
    throw InvalidArgumentError(
        "speciesDiffusiveFaceFluxes: " + std::to_string(diffusivities.gamma.size()) +
        " diffusivity fields for " + std::to_string(ns) + " species");
  }
  if (boundaries.size() != ns) {
    throw InvalidArgumentError("speciesDiffusiveFaceFluxes: " + std::to_string(boundaries.size()) +
                               " boundary sets for " + std::to_string(ns) + " species");
  }
  for (Index i = 0; i < ns; ++i) {
    if (fieldSet[i].size() != mesh.numberOfCells()) {
      throw InvalidArgumentError("speciesDiffusiveFaceFluxes: field for species \"" +
                                 registry.name(i) + "\" holds " +
                                 std::to_string(fieldSet[i].size()) + " cells; mesh has " +
                                 std::to_string(mesh.numberOfCells()));
    }
  }
  for (Index i = 0; i < ns; ++i) {
    if (boundaries[i] == nullptr) {
      throw InvalidArgumentError("speciesDiffusiveFaceFluxes: null boundary set for species \"" +
                                 registry.name(i) + "\"");
    }
    if (diffusivities.gamma[i].size() != mesh.numberOfCells()) {
      throw InvalidArgumentError("speciesDiffusiveFaceFluxes: diffusivity field for species \"" +
                                 registry.name(i) + "\" has " +
                                 std::to_string(diffusivities.gamma[i].size()) +
                                 " cells; mesh has " + std::to_string(mesh.numberOfCells()));
    }
  }

  const Index nf = mesh.numberOfFaces();
  SpeciesDiffusiveFluxes out;
  out.raw.assign(ns, SurfaceField(nf, 0.0));
  out.corrected.assign(ns, SurfaceField(nf, 0.0));
  out.rawSum = SurfaceField(nf, 0.0);

  // A1: Sigma_f is DEFINED as the flux the diffusion assembler actually
  // assembles, not as a separately-computed estimate of it. So the gradient is
  // built exactly as assembleScalarDiffusionContribution builds it, per species,
  // and the same cfd::discretization primitives are called with the same
  // operands. Under the original explicit split these two were computed
  // independently and agreed only to their own rounding -- which was the residue
  // that survived 2000 inner iterations.
  // See results/p18/species_transport/diffusion_closure_A1.md section 3(a).
  std::vector<cfd::fields::VectorField> gradients;
  gradients.reserve(ns);
  for (Index i = 0; i < ns; ++i) {
    gradients.push_back(cfd::discretization::gradient(
        mesh, fieldSet[i], *boundaries[i], cfd::discretization::GradientScheme::GreenGauss));
  }

  for (Index faceId = 0; faceId < nf; ++faceId) {
    const Face& face = mesh.face(faceId);
    const Index owner = face.owner();

    if (face.isBoundary()) {
      const Real distance = MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      for (Index i = 0; i < ns; ++i) {
        const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, faceId, *boundaries[i]);
        const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
        if (scalarBc == nullptr) {
          throw InvalidArgumentError(
              "speciesDiffusiveFaceFluxes: boundary condition for species \"" + registry.name(i) +
              "\" is not scalar-valued");
        }
        const bool prescribed = cfd::discretization::prescribesBoundaryValue(bc.type());
        const Real gammaFace = diffusivities.gamma[i][owner];
        const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
            mesh, face, gammaFace, distance, &gradients[i], prescribed);
        const Real yP = fieldSet[i][owner];

        // flux INTO the owner, read off the assembled row (which holds
        // -flux_into_owner):
        //   builder.add(P,P, coefficient) ; rhs += bvc*y_b ; rhs += explicitFlux ;
        //   builder.add(P,far, -farCellCoefficient)
        if (prescribed) {
          const Real yB = scalarBc->boundaryValue(yP, distance);
          Real flux =
              (terms.boundaryValueCoefficient * yB) - (terms.coefficient * yP) + terms.explicitFlux;
          if (terms.farCellCoefficient != 0.0) {
            flux += terms.farCellCoefficient * fieldSet[i][terms.farCell];
          }
          out.raw[i][faceId] = flux;
        } else {
          // Exact gradient treatment: no dependence on yP at all.
          out.raw[i][faceId] =
              (terms.boundaryValueCoefficient * scalarBc->boundaryValue(0.0, distance)) +
              terms.explicitFlux;
        }
      }
    } else {
      const Index neighbor = *face.neighbor();
      const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
      for (Index i = 0; i < ns; ++i) {
        const Real gammaFace = cfd::transport::faceDiffusivity(
            mesh, face, diffusivities.gamma[i],
            cfd::transport::FaceDiffusivityInterpolation::Arithmetic);
        const auto terms =
            cfd::discretization::internalFaceDiffusionTerms(mesh, face, gammaFace, dPN, nullptr);
        out.raw[i][faceId] = terms.coefficient * (fieldSet[i][neighbor] - fieldSet[i][owner]);
      }
    }

    // Sigma_f = sum_j J^0_{j,f}, in FIXED ascending species order.
    Real sigma = 0.0;
    for (Index i = 0; i < ns; ++i) {
      sigma += out.raw[i][faceId];
    }
    out.rawSum[faceId] = sigma;

    // The corrected flux, reported for section 30. Sigma_f acts as an
    // OUT-of-owner face flux, so the face composition is taken UPWIND on its
    // sign -- one cell's COMPLETE composition, whose sum is 1 to G2(a). That is
    // what the implicit convection assembler will use, so the reported corrected
    // flux is the one actually assembled.
    const Index upwindCell = (sigma >= 0.0 || face.isBoundary()) ? owner : *face.neighbor();
    for (Index i = 0; i < ns; ++i) {
      out.corrected[i][faceId] = out.raw[i][faceId] - (fieldSet[i][upwindCell] * sigma);
    }
  }

  return out;
}

ClosureResidual closureResidual(const SpeciesDiffusiveFluxes& fluxes) {
  ClosureResidual result;
  if (fluxes.corrected.empty()) {
    return result;
  }
  const Index nf = fluxes.corrected[0].size();
  const Index ns = fluxes.corrected.size();
  for (Index faceId = 0; faceId < nf; ++faceId) {
    Real sum = 0.0;
    for (Index i = 0; i < ns; ++i) {
      sum += fluxes.corrected[i][faceId];
      const Real rawMagnitude = std::abs(fluxes.raw[i][faceId]);
      if (rawMagnitude > result.maxAbsoluteRaw) {
        result.maxAbsoluteRaw = rawMagnitude;
      }
    }
    if (std::abs(sum) > result.maxAbsoluteSum) {
      result.maxAbsoluteSum = std::abs(sum);
      result.worstFace = faceId;
    }
  }
  return result;
}

cfd::transport::ScalarTransportAssembly speciesTransportAssembly(
    const Mesh& mesh, Index speciesIndex, const SpeciesRegistry& registry,
    const std::vector<ScalarField>& fieldSet, const SurfaceField& massFlux,
    const SpeciesDiffusivities& diffusivities,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    const cfd::transport::ScalarSourceFields& source, const SpeciesTimeState& timeState,
    const SpeciesDiffusiveFluxes* closure, const SpeciesTransportOptions& options) {
  const Index n = mesh.numberOfCells();
  if (speciesIndex >= registry.size()) {
    throw InvalidArgumentError(
        "speciesTransportAssembly: species index " + std::to_string(speciesIndex) +
        " is out of range; the mixture holds " + std::to_string(registry.size()) + " species");
  }
  if (fieldSet.size() != registry.size()) {
    throw InvalidArgumentError("speciesTransportAssembly: " + std::to_string(fieldSet.size()) +
                               " fields for " + std::to_string(registry.size()) + " species");
  }
  if (fieldSet[speciesIndex].size() != n) {
    throw InvalidArgumentError("speciesTransportAssembly: field holds " +
                               std::to_string(fieldSet[speciesIndex].size()) + " cells; mesh has " +
                               std::to_string(n));
  }
  if (options.closureCorrection && options.diffusion && closure == nullptr) {
    throw InvalidArgumentError(
        "speciesTransportAssembly: the cross-species closure correction is enabled but no "
        "precomputed flux set was supplied; call speciesDiffusiveFaceFluxes first");
  }

  const ScalarField& phi = fieldSet[speciesIndex];

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  // Order matches P14's own combined assembler exactly: diffusion, convection,
  // source, transient last. The builder accumulates, so the order is
  // load-bearing for bitwise reproduction of the steady single-species path.
  if (options.diffusion) {
    cfd::transport::assembleScalarDiffusionContribution(
        mesh, diffusivities.gamma[speciesIndex], phi, boundaries, builder, rhs,
        options.nonOrthogonal, options.interpolation, options.gradientBoundary);

    // A1: THE CROSS-SPECIES CORRECTION, ASSEMBLED IMPLICITLY.
    //
    //   C_{i,f} = -Y_{i,f} Sigma_f
    //
    // is LINEAR IN Y_i with Sigma_f in the role of a face mass flux, so it is
    // not a source term at all -- it is transport of species i by the mixture
    // correction velocity. P14's upwind convection assembler is exactly that
    // operator, and passing Sigma_f as its massFlux assembles the correction
    // with no sign adjustment and no separate loop: the row holds
    // -flux_into_owner, and C is the flux INTO the owner, so as an out-of-owner
    // flux it is +Y_{i,f} Sigma_f, which is what convection on Sigma_f gives.
    //
    // WHY IMPLICIT MATTERS, and it is the whole of amendment A1: K is the SAME
    // operator for every species because Sigma_f does not depend on i, so
    //     sum_i K(Y_i) = K(sum_i Y_i) = K(1) = +sum_f Sigma_f
    // which cancels sum_i L_i(Y_i) = -sum_f Sigma_f EXACTLY, per cell. The
    // explicit split cancelled only to the accuracy with which a separately
    // computed correction reproduced the assembled flux -- measured residue
    // 1.577e-06, unchanged by 2000 inner iterations.
    // results/p18/species_transport/diffusion_closure_A1.md
    if (options.closureCorrection) {
      cfd::transport::assembleScalarConvectionContribution(mesh, closure->rawSum, phi, boundaries,
                                                           builder, rhs);
    }
  }

  if (options.convection) {
    cfd::transport::assembleScalarConvectionContribution(mesh, massFlux, phi, boundaries, builder,
                                                         rhs);
  }

  cfd::transport::applyImplicitScalarSource(mesh, builder, rhs, source.Su, source.Sp);

  // Transient last, and only when asked: a steady assembly executes no
  // transient code at all rather than adding a zero, so the steady result
  // cannot be perturbed by the transient path existing.
  cfd::transport::TransportTimeState p14Time;
  if (timeState.transient) {
    if (timeState.fieldsOld == nullptr || timeState.rhoOld == nullptr ||
        timeState.rhoNew == nullptr) {
      throw InvalidArgumentError(
          "speciesTransportAssembly: a transient step requires stateOld, rhoOld and rhoNew; "
          "rho^{n+1} Y^n on the right-hand side is not conservative when the density changes");
    }
    if (timeState.fieldsOld->size() != registry.size() ||
        (*timeState.fieldsOld)[speciesIndex].size() != n) {
      throw InvalidArgumentError(
          "speciesTransportAssembly: the old time level's shape does not match the current one");
    }
    p14Time.transient = true;
    p14Time.dt = timeState.dt;
    p14Time.phiOld = &(*timeState.fieldsOld)[speciesIndex];
    p14Time.rhoOld = timeState.rhoOld;
    p14Time.rhoNew = timeState.rhoNew;
  }
  cfd::transport::applyTransientContribution(mesh, builder, rhs, p14Time);

  cfd::algebra::SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("speciesTransportAssembly: assembled system contains a non-finite value");
  }
  return {cfd::algebra::LinearSystem{std::move(matrix), std::move(rhs)}, std::move(diagonal)};
}

SpeciesDiffusiveFluxes speciesDiffusiveFaceFluxes(
    const Mesh& mesh, const MassFractionState& state, const SpeciesDiffusivities& diffusivities,
    const std::vector<const cfd::boundary::BoundaryConditionSet*>& boundaries) {
  return speciesDiffusiveFaceFluxes(mesh, state.registry(), state.fields(), diffusivities,
                                    boundaries);
}

cfd::transport::ScalarTransportAssembly speciesTransportAssembly(
    const Mesh& mesh, Index speciesIndex, const MassFractionState& state,
    const SurfaceField& massFlux, const SpeciesDiffusivities& diffusivities,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    const cfd::transport::ScalarSourceFields& source, const SpeciesTimeState& timeState,
    const SpeciesDiffusiveFluxes* closure, const SpeciesTransportOptions& options) {
  return speciesTransportAssembly(mesh, speciesIndex, state.registry(), state.fields(), massFlux,
                                  diffusivities, boundaries, source, timeState, closure, options);
}

}  // namespace cfd::species
