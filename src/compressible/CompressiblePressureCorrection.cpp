#include "cfd/compressible/CompressiblePressureCorrection.hpp"

#include <cmath>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

namespace cfd::compressible {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::PressureCorrectionAssembly;

PressureCorrectionAssembly assembleCompressiblePressureCorrection(
    const Mesh& mesh, const SurfaceField& predictorMassFlux, const SurfaceField& faceDensity,
    const ScalarField& uResponseCoefficient, const ScalarField& vResponseCoefficient,
    const ScalarField& pressureAbsolute, const ScalarField& temperature,
    const ThermodynamicProperties& thermodynamics, Real pseudoTimeStep, Index referenceCell,
    const BoundaryConditionSet& pressureBoundaries,
    const cfd::pressure_velocity::PressureCorrectionOptions& options) {
  const Index n = mesh.numberOfCells();
  if (predictorMassFlux.size() != mesh.numberOfFaces() ||
      faceDensity.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: predictorMassFlux/faceDensity size does not "
        "match mesh face count");
  }
  if (uResponseCoefficient.size() != n || vResponseCoefficient.size() != n ||
      pressureAbsolute.size() != n || temperature.size() != n) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: field size does not match mesh cell count");
  }
  if (!std::isfinite(pseudoTimeStep) || !(pseudoTimeStep > 0.0)) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: pseudoTimeStep must be finite and > 0");
  }
  if (referenceCell >= n) {
    throw InvalidArgumentError(
        "assembleCompressiblePressureCorrection: referenceCell out of range");
  }

  // P12-COMP-002: the compressibility diagonal term -- this equation
  // discretizes d(rho)/dt (via dDensityDPressure * dp/dt, implicit in the
  // unknown pressure correction) against the pseudo-time-step. With the
  // new term the system is no longer singular even on a fully-closed
  // (all-Neumann) domain, but `referenceCell` pinning (and so skipping the
  // pinned row's diagonal) is retained for exact behavioral continuity
  // with the incompressible equation's own convention, and because the
  // compressibility term can be numerically tiny (large pseudoTimeStep or
  // near-incompressible gas) where the pin still matters for conditioning.
  ScalarField compressibilityDiagonal(n);
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    const Real dRhoDp =
        thermodynamics.equationOfState().dDensityDPressure(pressureAbsolute[id], temperature[id]);
    compressibilityDiagonal[id] = cell.volume() / pseudoTimeStep * dRhoDp;
  }

  // P12-NUM-003: the face coupling, boundary treatment, reference-cell
  // pinning and the non-orthogonal correction are the incompressible
  // equation's own (cfd::pressure_velocity::assembleGeometricPressureCorrection
  // -- one implementation), fed with this equation's per-face density
  // (P12-COMP-002: faceDensity[faceId] is already the correct per-face
  // value -- evaluateCompressibleFaceDensity's internal-face interpolation
  // or P12-COMP-001's boundary EOS treatment) and its compressibility
  // diagonal. On a Cartesian mesh this reproduces the pre-P12-NUM-003
  // assembly bit-for-bit (same coefficient expressions, same add order:
  // face terms, then the diagonal, then the pin).
  return cfd::pressure_velocity::assembleGeometricPressureCorrection(
      mesh, predictorMassFlux, faceDensity, uResponseCoefficient, vResponseCoefficient,
      referenceCell, pressureBoundaries, options, &compressibilityDiagonal);
}

}  // namespace cfd::compressible
