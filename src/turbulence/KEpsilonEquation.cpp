#include "cfd/turbulence/KEpsilonEquation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/transport/ScalarTransport.hpp"

namespace cfd::turbulence {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool allFinite(const ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

ScalarField toScalarField(const Vector& v) {
  ScalarField field(v.size());
  for (Index i = 0; i < v.size(); ++i) field[i] = v[i];
  return field;
}

Vector toVector(const ScalarField& field) {
  Vector v(field.size());
  for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
  return v;
}

}  // namespace

ScalarField computeEffectiveDiffusivity(Real molecularViscosity,
                                        const ScalarField& turbulentViscosity, Real sigma) {
  if (!std::isfinite(molecularViscosity) || !(molecularViscosity > 0.0)) {
    throw InvalidArgumentError(
        "computeEffectiveDiffusivity: molecularViscosity must be finite and > 0");
  }
  if (!std::isfinite(sigma) || !(sigma > 0.0)) {
    throw InvalidArgumentError("computeEffectiveDiffusivity: sigma must be finite and > 0");
  }
  ScalarField gamma(turbulentViscosity.size());
  for (Index i = 0; i < turbulentViscosity.size(); ++i) {
    const Real muT = turbulentViscosity[i];
    if (!std::isfinite(muT) || muT < 0.0) {
      throw InvalidArgumentError(
          "computeEffectiveDiffusivity: turbulentViscosity must be finite and >= 0");
    }
    // Gamma = mu + mu_t/sigma -- NOT (mu+mu_t)/sigma.
    gamma[i] = molecularViscosity + (muT / sigma);
  }
  return gamma;
}

KSourceTerms computeKSourceTerms(Real density, const ScalarField& k, const ScalarField& epsilon,
                                 const ScalarField& production, Real kFloor) {
  const Index n = k.size();
  KSourceTerms terms{ScalarField(n), ScalarField(n)};
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k[i], kFloor);
    terms.Su[i] = production[i];
    terms.Sp[i] = -density * epsilon[i] / kSafe;
  }
  return terms;
}

EpsilonSourceTerms computeEpsilonSourceTerms(Real density, Real c1Epsilon, Real c2Epsilon,
                                             const ScalarField& k, const ScalarField& epsilon,
                                             const ScalarField& production, Real kFloor) {
  const Index n = k.size();
  EpsilonSourceTerms terms{ScalarField(n), ScalarField(n)};
  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k[i], kFloor);
    const Real epsOverK = epsilon[i] / kSafe;
    terms.Su[i] = c1Epsilon * epsOverK * production[i];
    terms.Sp[i] = -c2Epsilon * density * epsOverK;
  }
  return terms;
}

ScalarField solveRelaxedScalarTransport(
    const Mesh& mesh, const ScalarField& phi, const SurfaceField& massFlux,
    const ScalarField& diffusivity, const BoundaryConditionSet& boundaries, const ScalarField& Su,
    const ScalarField& Sp, Real alpha, Real floorValue,
    const cfd::algebra::LinearSolverSettings& solverSettings,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal,
    const std::vector<ImposedCell>& imposedCells) {
  const Index n = mesh.numberOfCells();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  // P14: one scalar-transport assembly for the whole codebase. The duplicated
  // turbulence diffusion and source loops that used to live in this file were
  // deleted once all six production equations had been migrated and their
  // output frozen -- see tests/unit/transport/test_transport_characterization.cpp.
  //
  // Convection was never duplicated: both layers always called the same
  // cfd::thermal assembler with a unit specific heat.
  cfd::transport::assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder,
                                                      rhs, nonOrthogonal);
  cfd::transport::assembleScalarConvectionContribution(mesh, massFlux, phi, boundaries, builder,
                                                       rhs);
  cfd::transport::applyImplicitScalarSource(mesh, builder, rhs, Su, Sp);

  const Vector phiOld = toVector(phi);
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, phiOld, alpha);

  // P16: imposed cells are applied LAST, after under-relaxation.
  //
  // Order matters and is load-bearing. fixRow discards every triplet in the row
  // whenever it was added, so the relaxation's diagonal contribution goes with
  // it -- but relaxation also writes to `rhs`, which fixRow does not own. Both
  // halves of the row therefore have to be settled here, at the end: the
  // right-hand side is OVERWRITTEN (not accumulated) so whatever assembly and
  // relaxation put there is discarded along with the matrix row.
  for (const ImposedCell& imposed : imposedCells) {
    if (imposed.cell >= n) {
      throw InvalidArgumentError(
          "solveRelaxedScalarTransport: imposed cell index is outside the mesh");
    }
    if (!std::isfinite(imposed.value)) {
      throw InvalidArgumentError("solveRelaxedScalarTransport: imposed value must be finite");
    }
    builder.fixRow(imposed.cell);
    rhs[imposed.cell] = imposed.value;
  }

  SparseMatrix matrix = builder.build();
  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError("solveRelaxedScalarTransport: assembled system is non-finite");
  }

  const cfd::algebra::BiCGSTAB solver(solverSettings);
  const auto result = solver.solve(cfd::algebra::LinearSystem(std::move(matrix), rhs), phiOld);
  if (!result.converged()) {
    throw NumericalError("solveRelaxedScalarTransport: linear solve did not converge");
  }

  ScalarField solved = toScalarField(result.solution);
  if (!allFinite(solved)) {
    throw NumericalError("solveRelaxedScalarTransport: solution is non-finite");
  }
  for (Index i = 0; i < n; ++i) {
    solved[i] = std::max(solved[i], floorValue);
  }
  // An imposed value is a boundary condition, not a solver output, so the floor
  // must not move it. Re-asserted rather than assumed: a wall epsilon below the
  // floor would otherwise be silently replaced by the floor, and the cell would
  // stop satisfying the wall function that put it there.
  for (const ImposedCell& imposed : imposedCells) {
    solved[imposed.cell] = imposed.value;
  }
  return solved;
}

}  // namespace cfd::turbulence
