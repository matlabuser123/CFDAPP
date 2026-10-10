#include "cfd/transport/ScalarTransport.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

// P14 -- see ScalarTransport.hpp for why this is a PROMOTION rather than a new
// implementation. Every loop below was lifted from
// cfd::turbulence::assembleScalarTransportEquation and its helpers with the
// arithmetic, the operand order and the sequence of SparseMatrixBuilder::add
// calls unchanged, because SparseMatrixBuilder accumulates and floating-point
// addition is not associative: reordering would change results that the
// project's standard requires to be bitwise stable.

namespace cfd::transport {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

Real faceDiffusivity(const Mesh& mesh, const Face& face, const ScalarField& diffusivity,
                     FaceDiffusivityInterpolation interpolation) {
  if (interpolation == FaceDiffusivityInterpolation::Arithmetic) {
    // The production convention, delegated rather than re-derived so the two
    // can never drift.
    return cfd::discretization::interpolateInternalFace(mesh, face, diffusivity);
  }
  // Harmonic, distance weighted:  Gamma_f = d_PN / (d_Pf/Gamma_P + d_fN/Gamma_N)
  // At a midpoint face (d_Pf == d_fN) this is 2 GP GN / (GP + GN).
  //
  // Why this exists alongside the arithmetic form: across a DISCONTINUITY the
  // physically correct face value is the one that preserves flux continuity,
  // not the average of the two sides. An arithmetic mean across a 1000:1 jump
  // in conductivity overestimates the interface conductance by orders of
  // magnitude. Needed by a conjugate solid/fluid interface (P15) and by
  // multiphase property jumps (P19); never selected automatically.
  const Real dPf = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Real dNf = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
  const Real gammaP = diffusivity[face.owner()];
  const Real gammaN = diffusivity[*face.neighbor()];
  const Real denominator = (dPf / gammaP) + (dNf / gammaN);
  if (!(denominator > 0.0)) {
    throw InvalidArgumentError("faceDiffusivity: harmonic interpolation needs positive Gamma");
  }
  return (dPf + dNf) / denominator;
}

void assembleScalarDiffusionContribution(
    const Mesh& mesh, const ScalarField& diffusivity, const ScalarField& phi,
    const BoundaryConditionSet& boundaries, SparseMatrixBuilder& builder, Vector& rhs,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal,
    FaceDiffusivityInterpolation interpolation, GradientBoundaryTreatment gradientBoundary) {
  const Index n = mesh.numberOfCells();
  if (phi.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarDiffusionContribution: phi size does not match mesh cell count");
  }
  if (diffusivity.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarDiffusionContribution: diffusivity size does not match mesh cell count");
  }
  for (Index i = 0; i < n; ++i) {
    if (!std::isfinite(diffusivity[i]) || !(diffusivity[i] > 0.0)) {
      throw InvalidArgumentError(
          "assembleScalarDiffusionContribution: diffusivity must be finite and > 0 in every cell");
    }
  }

  // The Dirichlet wall-flux scheme must not depend on the iterative
  // non-orthogonal control (P12-DIFF-002 A2): the gradient is always built,
  // and `enabled` gates only the INTERNAL-face correction.
  const cfd::fields::VectorField gradPhi =
      cfd::discretization::gradient(mesh, phi, boundaries, nonOrthogonal.gradientScheme);
  const cfd::fields::VectorField* gradPhiPtr = &gradPhi;
  const cfd::fields::VectorField* internalGradPhi = nonOrthogonal.enabled ? &gradPhi : nullptr;

  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);

    if (face.isBoundary()) {
      const Index ownerId = face.owner();
      const Real distance = MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
      // Boundary faces use the OWNER cell's diffusivity: there is no
      // neighbour to interpolate against.
      const Real gammaFace = diffusivity[ownerId];

      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
      const auto* scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: boundary condition is not scalar-valued");
      }
      // P15: the generic path cannot form a convective (Robin) coefficient.
      //
      //   G = 1 / (d/k + 1/h_c)
      //
      // needs the runtime diffusivity. This layer HAS Gamma, but the boundary
      // query that would consume it (ScalarBoundaryCondition::boundaryValue)
      // is not given it, and changing that signature touches every scalar and
      // vector BC -- deliberately outside P15 (Option B; see
      // results/p15/thermal-energy/robin_architecture_decision.md).
      //
      // Refused HERE rather than by making boundaryValue() throw: that broke
      // the gradient operator, which legitimately needs a value and for which
      // zero-gradient is sound. This is the one place a generic solve could
      // otherwise treat a convective wall as adiabatic without complaint.
      if (bc.type() == cfd::boundary::BoundaryConditionType::ConvectiveHeatTransfer) {
        throw InvalidArgumentError(
            "assembleScalarDiffusionContribution: a ConvectiveHeatTransfer (Robin) boundary "
            "requires the runtime diffusivity to form its coefficient and is not supported by "
            "the generic scalar transport path; cfd::thermal's energy assembly handles it");
      }
      const bool prescribed = cfd::discretization::prescribesBoundaryValue(bc.type());
      const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(
          mesh, face, gammaFace, distance, gradPhiPtr, prescribed);

      // P18.2. A GRADIENT face under Exact treatment contributes NO diagonal and
      // a fully known RHS, because Df (phi_b - phi_P) = Df g d has no phi_P in
      // it. `boundaryValue(0.0, distance)` returns exactly g*d, so the RHS term
      // is Df g d -- identical in form to cfd::thermal's energy assembly.
      //
      // Lagged (the default) is the pre-P18.2 path, byte for byte: same
      // `terms`, same diagonal, same operand order. Only the two lines below
      // differ between the branches, so a Lagged call cannot be perturbed by
      // this change.
      if (!prescribed && gradientBoundary == GradientBoundaryTreatment::Exact) {
        rhs[ownerId] += terms.explicitFlux;
        rhs[ownerId] += terms.boundaryValueCoefficient * scalarBc->boundaryValue(0.0, distance);
        // No builder.add: the diagonal contribution is exactly zero. And no
        // far-cell entry -- the three-point reconstruction applies only to a
        // prescribed-value face, where farCellCoefficient is the only case it is
        // non-zero, so there is nothing to add here by construction.
        continue;
      }

      const Real diffusionCoefficient = terms.coefficient;
      const Real phiB = scalarBc->boundaryValue(phi[ownerId], distance);
      rhs[ownerId] += terms.explicitFlux;

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
    // Distance-weighted face interpolation -- the convention the production
    // variable-diffusivity path already uses (P14 Phase B6: adopted, not chosen).
    const Real gammaFace = faceDiffusivity(mesh, face, diffusivity, interpolation);
    const auto terms = cfd::discretization::internalFaceDiffusionTerms(mesh, face, gammaFace, dPN,
                                                                       internalGradPhi);
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

void assembleScalarConvectionContribution(const Mesh& mesh, const SurfaceField& massFlux,
                                          const ScalarField& phi,
                                          const BoundaryConditionSet& boundaries,
                                          SparseMatrixBuilder& builder, Vector& rhs) {
  // Delegates to the production convection assembler with a unit coefficient,
  // exactly as the promoted turbulence path did. NOT re-implemented here: a
  // second upwind loop would be the tenth scalar convection implementation in
  // the tree and a second place for the sign convention to drift.
  //
  // The layering is wrong-way (transport -> thermal) and is inherited, not
  // introduced: cfd::turbulence already made this call. Recorded as a P15
  // migration item rather than fixed here, because fixing it means either
  // editing thermal/ (excluded from P14) or copying the loop (forbidden).
  cfd::thermal::assembleThermalConvectionContribution(mesh, /*specificHeat=*/1.0, massFlux, phi,
                                                      boundaries, builder, rhs);
}

void applyImplicitScalarSource(const Mesh& mesh, SparseMatrixBuilder& builder, Vector& rhs,
                               const ScalarField& Su, const ScalarField& Sp,
                               bool requireNonPositiveSp) {
  const Index n = mesh.numberOfCells();
  if (Su.size() != n || Sp.size() != n) {
    throw InvalidArgumentError(
        "applyImplicitScalarSource: Su/Sp size does not match mesh cell count");
  }
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    if (!std::isfinite(Su[id]) || !std::isfinite(Sp[id])) {
      throw InvalidArgumentError("applyImplicitScalarSource: Su/Sp must be finite in every cell");
    }
    // Phase B5: validated, never clamped. A positive Sp removes diagonal
    // dominance; silently zeroing it would hide a caller's modelling error
    // behind a matrix that merely looks well behaved.
    if (requireNonPositiveSp && Sp[id] > 0.0) {
      throw InvalidArgumentError(
          "applyImplicitScalarSource: Sp must be <= 0 for Patankar stability (cell " +
          std::to_string(static_cast<long long>(id)) + ")");
    }
    builder.add(id, id, -Sp[id] * cell.volume());
    rhs[id] += Su[id] * cell.volume();
  }
}

void applyTransientContribution(const Mesh& mesh, SparseMatrixBuilder& builder, Vector& rhs,
                                const TransportTimeState& timeState) {
  if (!timeState.transient) return;

  if (!std::isfinite(timeState.dt) || !(timeState.dt > 0.0)) {
    throw InvalidArgumentError("applyTransientContribution: dt must be finite and > 0");
  }
  if (timeState.phiOld == nullptr || timeState.rhoOld == nullptr || timeState.rhoNew == nullptr) {
    throw InvalidArgumentError(
        "applyTransientContribution: a transient state needs phiOld, rhoOld and rhoNew");
  }
  const Index n = mesh.numberOfCells();
  if (timeState.phiOld->size() != n || timeState.rhoOld->size() != n ||
      timeState.rhoNew->size() != n) {
    throw InvalidArgumentError(
        "applyTransientContribution: phiOld/rhoOld/rhoNew size does not match mesh cell count");
  }

  // Conservative implicit Euler for d(rho phi)/dt:
  //   a_P += rho^{n+1}_P V_P / dt        <- NEW density
  //   b_P += rho^n_P phi^n_P V_P / dt    <- OLD density AND old phi
  // Using rho^{n+1} phi^n on the right is the non-conservative form and is the
  // easiest error to write here; the two density levels are separate named
  // members precisely so writing it requires naming the wrong one.
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real rhoNew = (*timeState.rhoNew)[id];
    const Real rhoOld = (*timeState.rhoOld)[id];
    if (!std::isfinite(rhoNew) || !(rhoNew > 0.0) || !std::isfinite(rhoOld) || !(rhoOld > 0.0)) {
      throw InvalidArgumentError(
          "applyTransientContribution: density must be finite and > 0 in every cell");
    }
    builder.add(id, id, rhoNew * cell.volume() / timeState.dt);
    rhs[id] += rhoOld * (*timeState.phiOld)[id] * cell.volume() / timeState.dt;
  }
}

ScalarTransportAssembly assembleScalarTransportEquation(
    const Mesh& mesh, const ScalarField& phi, const SurfaceField& massFlux,
    const ScalarField& diffusivity, const BoundaryConditionSet& boundaries, const ScalarField& Su,
    const ScalarField& Sp, const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal,
    const TransportTimeState& timeState, FaceDiffusivityInterpolation interpolation,
    GradientBoundaryTreatment gradientBoundary) {
  const Index n = mesh.numberOfCells();
  if (phi.size() != n) {
    throw InvalidArgumentError(
        "assembleScalarTransportEquation: phi size does not match mesh cell count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleScalarTransportEquation: massFlux size does not match mesh face count");
  }

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  // Order is load-bearing, not stylistic: the builder accumulates, so this
  // sequence is what makes the steady path bitwise identical to the turbulence
  // assembler it was promoted from.
  assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder, rhs,
                                      nonOrthogonal, interpolation, gradientBoundary);
  assembleScalarConvectionContribution(mesh, massFlux, phi, boundaries, builder, rhs);
  applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);
  // Added last, and only when transient: a steady solve executes no transient
  // code at all rather than adding a zero, so the steady result cannot be
  // perturbed by the transient path existing.
  applyTransientContribution(mesh, builder, rhs, timeState);

  SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleScalarTransportEquation: assembled system contains a non-finite value");
  }

  return ScalarTransportAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs),
                                 std::move(diagonal)};
}

}  // namespace cfd::transport
