#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"

namespace cfd::compressible {

// P12-COMP-002: the compressible pressure-correction equation --
// generalizes cfd::pressure_velocity::assemblePressureCorrection in two
// ways a genuinely variable-density flow needs (confirmed necessary by
// reading that function's full implementation, not assumed from its
// header -- see the P12-COMP-002 plan's own "Refined findings"):
//
//   1. The D_f face coefficient (rho_f*Af*d_f/dPN) uses the SAME
//      per-face density `evaluateCompressibleFaceDensity` computes for
//      the predictor mass flux (`faceDensity` below is that exact
//      already-computed field, passed in rather than recomputed here --
//      the "one canonical compressible face quantity" consistency
//      invariant, mirrored from the incompressible faceCoefficient/
//      correctFaceMassFlux precedent) -- NOT a single global constant,
//      unlike the incompressible equation this generalizes.
//   2. A new diagonal compressibility term,
//      V_P/pseudoTimeStep * dDensityDPressure(p_abs_P, T_P), using
//      IdealGasEOS's own analytical derivative (ThermodynamicProperties::
//      equationOfState().dDensityDPressure -- existing, unmodified,
//      previously unused anywhere in this codebase; see
//      EquationOfState.hpp's own header comment: "needed by the
//      pressure-density correction relation"). This is exactly the
//      transient compressible continuity equation's own pressure term
//      (d(rho)/dt = dDensityDPressure * dp/dt, discretized implicitly
//      against the pseudo-time-step), so this equation *is* the
//      discretized compressible continuity constraint solved for
//      pressure -- see this equation's own role in CompressibleSIMPLE's
//      header comment for why no separate "solve compressible
//      continuity" step exists or is needed.
//
// Reduces exactly to assemblePressureCorrection's own result when
// `faceDensity` is uniform and the compressibility term is negligible
// (tiny `dDensityDPressure`, e.g. a very large gas constant) -- the
// numerical target CompressibleSIMPLE's own reduction-to-incompressible
// regression checks.
//
// Boundary treatment (Dirichlet/Neumann pressure-patch distinction,
// reference-cell pinning) is otherwise identical to
// assemblePressureCorrection's own -- see PressureCorrectionEquation.hpp's
// own header comment for the full boundary-treatment rationale, which
// applies unchanged here.
//
// Throws InvalidArgumentError if predictorMassFlux/faceDensity size !=
// mesh.numberOfFaces(), uResponseCoefficient/vResponseCoefficient/
// pressureAbsolute/temperature size != mesh.numberOfCells(),
// pseudoTimeStep is not finite and > 0, or referenceCell >=
// mesh.numberOfCells(). NumericalError if the assembled system is
// non-finite (propagated from IdealGasEOS's own validation of
// pressureAbsolute/temperature).
[[nodiscard]] cfd::pressure_velocity::PressureCorrectionAssembly
assembleCompressiblePressureCorrection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& predictorMassFlux,
    const cfd::fields::SurfaceField& faceDensity,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient,
    const cfd::fields::ScalarField& pressureAbsolute, const cfd::fields::ScalarField& temperature,
    const ThermodynamicProperties& thermodynamics, Real pseudoTimeStep, Index referenceCell,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

}  // namespace cfd::compressible
