#pragma once

// Test-only helpers for discretization operator verification: analytical
// (manufactured) fields with known exact gradient/Laplacian, and a way to
// give every boundary face its own exact Dirichlet value (needed because
// production FixedValue is a single constant -- see makeExactBoundaries
// below for why and how).

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/validation/ErrorNorms.hpp"

namespace cfd::test {

// --- Manufactured fields (exact, polynomial up to degree 2) --------------
// These are the exactness-test fields from the spec: linear fields should
// be reproduced by these operators to near machine precision, and the
// quadratic field's flux at any face midpoint is *exactly* its analytical
// derivative there (a property of central differencing on a uniform
// Cartesian grid), so the operators reproduce it exactly too. That makes
// them excellent correctness probes, but useless for a grid-refinement
// *order* study (see phiSmooth below): the error is already at floating-
// point noise on every grid, so log(E_h/E_h2) is meaningless.

inline Real phiX(const Vector2& p) { return p.x; }
inline Real phiY(const Vector2& p) { return p.y; }
inline Real phiQuadratic(const Vector2& p) { return (p.x * p.x) + (p.y * p.y); }
inline Vector2 gradQuadratic(const Vector2& p) { return Vector2{2.0 * p.x, 2.0 * p.y}; }

// --- A genuinely smooth (non-polynomial) field, for observed-order study -
// phi = sin(pi x) cos(pi y) has real (h^2-scaling) truncation error under
// this scheme, so refining the grid produces a real, meaningful observed
// convergence order -- unlike the polynomial fields above.
inline Real phiSmooth(const Vector2& p) {
  return std::sin(constants::pi * p.x) * std::cos(constants::pi * p.y);
}
inline Vector2 gradSmooth(const Vector2& p) {
  const Real pi = constants::pi;
  return Vector2{pi * std::cos(pi * p.x) * std::cos(pi * p.y),
                 -pi * std::sin(pi * p.x) * std::sin(pi * p.y)};
}
inline Real laplacianSmooth(const Vector2& p) {
  return -2.0 * constants::pi * constants::pi * phiSmooth(p);
}

// --- Per-face exact boundary values ---------------------------------------
// FixedValue is a single uniform value, but a manufactured field's exact
// boundary value varies along a patch (e.g. phi=x^2+y^2 differs at every
// point of the "top" patch). Rather than adding a spatially-varying BC
// type to production code (explicitly out of scope -- see TODO.md), we
// rebuild the mesh's boundary patches as one singleton patch per face,
// then assign each its own exact FixedValue. This uses only existing,
// already-verified production APIs.
// P12-NUM-002: generalized to accept ANY already-built mesh (not just a
// fresh Cartesian one) -- e.g. a distorted mesh (DistortedMesh.hpp) --
// via simple overloading; the nx/ny/lengthX/lengthY overload below is
// kept byte-identical to before (just forwards here) so every existing
// P12-NUM-001 caller is unaffected.
inline cfd::mesh::Mesh perFaceBoundaryMesh(const cfd::mesh::Mesh& base) {
  std::vector<cfd::mesh::Cell> cells = base.cells();
  std::vector<cfd::mesh::Face> faces = base.faces();

  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }

  return cfd::mesh::Mesh(std::move(cells), std::move(faces), std::move(patches));
}

inline cfd::mesh::Mesh perFaceBoundaryMesh(Index nx, Index ny, Real lengthX, Real lengthY) {
  return perFaceBoundaryMesh(cfd::mesh::MeshGeometry::createCartesian2D(nx, ny, lengthX, lengthY));
}

template <typename PhiFunction>
cfd::boundary::BoundaryConditionSet makeExactBoundaries(const cfd::mesh::Mesh& mesh,
                                                        PhiFunction phi) {
  cfd::boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    const Real value = phi(mesh.face(faceId).centroid());
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(value));
  }
  return bcs;
}

// --- Error metrics ---------------------------------------------------------

// Volume-weighted L2 error over all cells. P12-NUM-006: delegates to the
// one authoritative norm implementation (cfd/validation/ErrorNorms.hpp).
template <typename NumericField, typename ExactFunction>
Real l2CellError(const cfd::mesh::Mesh& mesh, const NumericField& numeric, ExactFunction exact) {
  cfd::fields::ScalarField numericValues(mesh.numberOfCells());
  cfd::fields::ScalarField exactValues(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    numericValues[cell.id()] = numeric[cell.id()];
    exactValues[cell.id()] = exact(cell.centroid());
  }
  return cfd::validation::computeErrorNorms(mesh, numericValues, exactValues).l2;
}

// Volume-weighted L2 error for a vector field (magnitude of the
// difference vector) -- also via cfd/validation/ErrorNorms.hpp.
template <typename ExactVectorFunction>
Real l2CellErrorVector(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& numeric,
                       ExactVectorFunction exact) {
  cfd::fields::VectorField exactValues(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) exactValues[cell.id()] = exact(cell.centroid());
  return cfd::validation::computeVectorErrorNorms(mesh, numeric, exactValues).magnitude.l2;
}

// Observed order p from two refinement levels (grid spacing halved):
// p = log(E_h / E_h2) / log(2).
inline Real observedOrder(Real errorCoarse, Real errorFine) {
  return std::log(errorCoarse / errorFine) / std::log(2.0);
}

// =====================================================================
// P12-NUM-006 -- system-level manufactured solutions on the unit square.
//
// The operator-level fields above verify one discrete operator at a time.
// The fields below are a COHERENT set for verifying whole solves (scalar
// transport, momentum, pressure-velocity coupling, SIMPLE): a scalar phi,
// a divergence-free velocity from a streamfunction, a pressure p, and the
// forcing terms that make them exact solutions of the continuous PDEs.
//
// Verification integrity: every derivative and every forcing term here is
// derived BY HAND from the closed-form expressions (product/chain rule on
// the continuous functions) -- never evaluated with a discrete operator of
// the code under test. (source = discrete_operator(exact_solution) would
// only prove algebraic self-consistency.) The unit tests re-derive each
// one independently with high-order finite differences of the continuous
// functions (ManufacturedFieldsTest.*).
namespace mms {

// Separable factors E(s) = exp(c s) sin(pi s) and their derivatives:
//   E'   = e^{cs} ( c S + pi C )
//   E''  = e^{cs} ( (c^2 - pi^2) S + 2 c pi C )
//   E''' = e^{cs} ( (c^3 - 3 c pi^2) S + (3 c^2 pi - pi^3) C )
// with S = sin(pi s), C = cos(pi s).
struct Factor {
  Real value;
  Real d1;
  Real d2;
  Real d3;
};
inline Factor expSinFactor(Real c, Real s) {
  const Real pi = constants::pi;
  const Real e = std::exp(c * s);
  const Real sn = std::sin(pi * s);
  const Real cs = std::cos(pi * s);
  return Factor{e * sn, e * ((c * sn) + (pi * cs)),
                e * ((((c * c) - (pi * pi)) * sn) + (2.0 * c * pi * cs)),
                e * ((((c * c * c) - (3.0 * c * pi * pi)) * sn) +
                     (((3.0 * c * c * pi) - (pi * pi * pi)) * cs))};
}

// Streamfunction psi = X(x) Y(y) / pi, X = e^{a x} sin(pi x), Y = e^{b y}
// sin(pi y). a != -b and a, b != 0 break every symmetry of the unit square
// (no Taylor-Green-style cancellation). psi = 0 on the whole boundary, so
// the normal velocity vanishes there exactly (closed box: the Neumann
// pressure-correction problem is exactly compatible) while the tangential
// wall velocity is non-zero (non-trivial Dirichlet data on every side).
constexpr Real kStreamA = 0.5;
constexpr Real kStreamB = -0.4;

inline Real streamfunction(const Vector2& p) {
  return expSinFactor(kStreamA, p.x).value * expSinFactor(kStreamB, p.y).value / constants::pi;
}

// u = d(psi)/dy, v = -d(psi)/dx, so div(u) = X'Y'/pi - X'Y'/pi = 0
// identically. Derivatives (all /pi):
//   u = X Y',   u_x = X' Y',  u_y = X Y'',   u_xx = X'' Y',  u_yy = X Y'''
//   v = -X' Y,  v_x = -X'' Y, v_y = -X' Y',  v_xx = -X''' Y, v_yy = -X' Y''
struct VelocityDerivatives {
  Vector2 value;
  Vector2 dx;   // (u_x, v_x)
  Vector2 dy;   // (u_y, v_y)
  Vector2 dxx;  // (u_xx, v_xx)
  Vector2 dyy;  // (u_yy, v_yy)
};
inline VelocityDerivatives vortexVelocity(const Vector2& p) {
  const Factor X = expSinFactor(kStreamA, p.x);
  const Factor Y = expSinFactor(kStreamB, p.y);
  const Real s = 1.0 / constants::pi;
  return VelocityDerivatives{
      Vector2{s * X.value * Y.d1, -s * X.d1 * Y.value},
      Vector2{s * X.d1 * Y.d1, -s * X.d2 * Y.value}, Vector2{s * X.value * Y.d2, -s * X.d1 * Y.d1},
      Vector2{s * X.d2 * Y.d1, -s * X.d3 * Y.value}, Vector2{s * X.value * Y.d3, -s * X.d1 * Y.d2}};
}
inline Vector2 velocity(const Vector2& p) { return vortexVelocity(p).value; }

// Pressure p = cos(pi x) cos(pi y) + x y / 2: gradients in both
// directions, non-zero normal derivative on every wall (x y / 2), and
// unrelated to the velocity field.
inline Real pressure(const Vector2& p) {
  const Real pi = constants::pi;
  return (std::cos(pi * p.x) * std::cos(pi * p.y)) + (0.5 * p.x * p.y);
}
inline Vector2 pressureGradient(const Vector2& p) {
  const Real pi = constants::pi;
  return Vector2{(-pi * std::sin(pi * p.x) * std::cos(pi * p.y)) + (0.5 * p.y),
                 (-pi * std::cos(pi * p.x) * std::sin(pi * p.y)) + (0.5 * p.x)};
}

// Steady incompressible momentum, the continuous form SIMPLE discretizes
// (MomentumEquation.hpp: rho (U.grad)U - mu Laplacian(U) = -grad p + f;
// the production convection term is div(rho U U), equal to rho (U.grad)U
// because div U = 0; constant mu, so mu Laplacian(U) = div(mu grad U)).
// Solving for f:
//   f_x = rho (u u_x + v u_y) + p_x - mu (u_xx + u_yy)
//   f_y = rho (u v_x + v v_y) + p_y - mu (v_xx + v_yy)
inline Vector2 momentumForcing(const Vector2& p, Real density, Real viscosity) {
  const VelocityDerivatives d = vortexVelocity(p);
  const Vector2 gradP = pressureGradient(p);
  const Real u = d.value.x;
  const Real v = d.value.y;
  return Vector2{
      (density * ((u * d.dx.x) + (v * d.dy.x))) + gradP.x - (viscosity * (d.dxx.x + d.dyy.x)),
      (density * ((u * d.dx.y) + (v * d.dy.y))) + gradP.y - (viscosity * (d.dxx.y + d.dyy.y))};
}

// --- Compressible (isothermal ideal gas) ----------------------------------
// Gauge pressure p = mms::pressure, absolute pressure P_ref + p, density
// from the ideal-gas EOS at the uniform temperature T0:
//   rho = (P_ref + p) / (R T0)
// The MASS flux is the vortex field, m = rho U = (psi_y, -psi_x), so the
// compressible continuity div(rho U) = div(m) = 0 holds identically, and
// U = m / rho. With m = rho U (quotient rule on the closed forms):
//   U_x  = (m_x - U rho_x) / rho,             U_y  = (m_y - U rho_y) / rho
//   U_xx = (m_xx - 2 rho_x U_x - U rho_xx) / rho,  U_yy likewise
//   rho_x = p_x/(R T0), rho_xx = p_xx/(R T0), p_xx = p_yy = -pi^2 cos(pi x) cos(pi y)
inline Real compressibleDensity(const Vector2& p, Real referencePressure,
                                Real gasConstantTemperature) {
  return (referencePressure + pressure(p)) / gasConstantTemperature;
}
inline VelocityDerivatives compressibleVelocity(const Vector2& p, Real referencePressure,
                                                Real gasConstantTemperature) {
  const Real pi = constants::pi;
  const VelocityDerivatives m = vortexVelocity(p);
  const Real rho = compressibleDensity(p, referencePressure, gasConstantTemperature);
  const Vector2 gradP = pressureGradient(p);
  const Real rhoX = gradP.x / gasConstantTemperature;
  const Real rhoY = gradP.y / gasConstantTemperature;
  const Real pSecond = -pi * pi * std::cos(pi * p.x) * std::cos(pi * p.y);  // p_xx = p_yy
  const Real rhoXX = pSecond / gasConstantTemperature;
  const Real rhoYY = pSecond / gasConstantTemperature;
  const Real inv = 1.0 / rho;
  VelocityDerivatives u;
  u.value = m.value * inv;
  u.dx = (m.dx - (u.value * rhoX)) * inv;
  u.dy = (m.dy - (u.value * rhoY)) * inv;
  u.dxx = (m.dxx - (u.dx * (2.0 * rhoX)) - (u.value * rhoXX)) * inv;
  u.dyy = (m.dyy - (u.dy * (2.0 * rhoY)) - (u.value * rhoYY)) * inv;
  return u;
}

// The steady compressible momentum equation CompressibleSIMPLE discretizes
// (CompressibleMomentum.hpp: the incompressible contribution assemblers
// with the compressible mass flux -- div(rho U U) - mu lap(U) = -grad p + f;
// NOTE the implemented viscous operator is mu lap(U), without the
// mu/3 grad(div U) term of the full compressible Newtonian stress, so the
// forcing below manufactures exactly the implemented equation). With
// div(m) = 0, div(rho U U) = div(m U) = (m . grad) U:
//   f = (m . grad) U + grad p - mu (U_xx + U_yy)
inline Vector2 compressibleMomentumForcing(const Vector2& p, Real referencePressure,
                                           Real gasConstantTemperature, Real viscosity) {
  const VelocityDerivatives u = compressibleVelocity(p, referencePressure, gasConstantTemperature);
  const Vector2 m = velocity(p);
  return (u.dx * m.x) + (u.dy * m.y) + pressureGradient(p) - ((u.dxx + u.dyy) * viscosity);
}

// --- Scalar advection-diffusion ------------------------------------------
// Advecting velocity: a uniform through-flow plus the vortex above
// (divergence-free: the uniform part trivially, the vortex by
// construction), so the scalar test has real inflow (left, bottom) and
// outflow (right, top) boundaries.
constexpr Real kThroughFlowU = 1.0;
constexpr Real kThroughFlowV = 0.5;
inline Vector2 advectingVelocity(const Vector2& p) {
  return Vector2{kThroughFlowU, kThroughFlowV} + velocity(p);
}

// phi = sin(pi x) sin(pi y) + x^2 y / 2 + 1/4 (non-zero, non-uniform
// Dirichlet data on two sides; mixed derivatives present).
inline Real scalar(const Vector2& p) {
  const Real pi = constants::pi;
  return (std::sin(pi * p.x) * std::sin(pi * p.y)) + (0.5 * p.x * p.x * p.y) + 0.25;
}
inline Vector2 scalarGradient(const Vector2& p) {
  const Real pi = constants::pi;
  return Vector2{(pi * std::cos(pi * p.x) * std::sin(pi * p.y)) + (p.x * p.y),
                 (pi * std::sin(pi * p.x) * std::cos(pi * p.y)) + (0.5 * p.x * p.x)};
}
inline Real scalarLaplacian(const Vector2& p) {
  const Real pi = constants::pi;
  return (-2.0 * pi * pi * std::sin(pi * p.x) * std::sin(pi * p.y)) + p.y;
}

// The equation ThermalSolver discretizes (EnergyEquation.hpp):
//   cp div(rho U phi) - k Laplacian(phi) = Q
// With div U = 0 and constant rho, cp, k:
//   Q = rho cp (U . grad phi) - k Laplacian(phi)
inline Real scalarForcing(const Vector2& p, Real density, Real specificHeat, Real conductivity) {
  return (density * specificHeat * dot(advectingVelocity(p), scalarGradient(p))) -
         (conductivity * scalarLaplacian(p));
}

// --- Exact face fluxes ----------------------------------------------------
// A 2D face is a straight segment; its area vector Sf is the segment
// rotated clockwise, so its endpoints are A = c - R/2, B = c + R/2 with
// R = (-Sf.y, Sf.x). For U = (psi_y, -psi_x), U . n dl = d(psi) along the
// segment, so the EXACT volume flux through the face (in the Sf
// direction) is psi(B) - psi(A) -- no quadrature error, and exactly zero
// net flux out of every cell (the vertex values telescope).
inline Real exactVortexVolumeFlux(const cfd::mesh::Face& face) {
  const Vector2 half{-0.5 * face.areaVector().y, 0.5 * face.areaVector().x};
  return streamfunction(face.centroid() + half) - streamfunction(face.centroid() - half);
}

// rho * (exact volume flux of advectingVelocity) per face (the uniform
// part is exact too: U0 . Sf).
inline cfd::fields::SurfaceField exactAdvectingMassFlux(const cfd::mesh::Mesh& mesh, Real density) {
  cfd::fields::SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = density * (dot(Vector2{kThroughFlowU, kThroughFlowV}, face.areaVector()) +
                                 exactVortexVolumeFlux(face));
  }
  return flux;
}

// --- Cell-centroid sampling and per-face boundary conditions --------------
template <typename Function>
cfd::fields::ScalarField sampleScalar(const cfd::mesh::Mesh& mesh, Function f) {
  cfd::fields::ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}
template <typename Function>
cfd::fields::VectorField sampleVector(const cfd::mesh::Mesh& mesh, Function f) {
  cfd::fields::VectorField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}

// Exact Dirichlet velocity on every boundary face (the mesh must come from
// perFaceBoundaryMesh: one patch per face). Inlet is the production
// prescribed-velocity condition (Dirichlet in momentum, prescribed flux in
// continuity).
inline cfd::boundary::BoundaryConditionSet makeExactVelocityBoundaries(
    const cfd::mesh::Mesh& mesh) {
  cfd::boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    bcs.set(mesh, patch.name(),
            std::make_unique<cfd::boundary::Inlet>(velocity(mesh.face(faceId).centroid())));
  }
  return bcs;
}

// Exact Neumann pressure data on every boundary face: FixedGradient(grad p
// . n_out) at the face centroid -- the pressure-compatible treatment of a
// closed domain (the pressure correction sees a Neumann boundary, so the
// gauge is fixed by SIMPLE's reference cell). FixedGradient reconstructs
// p_face = p_owner + g * d with d the owner->face distance, so g is the
// OUTWARD normal derivative.
inline cfd::boundary::BoundaryConditionSet makeExactPressureBoundaries(
    const cfd::mesh::Mesh& mesh) {
  cfd::boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& face = mesh.face(patch.faceIds().front());
    const Vector2 outwardNormal = face.areaVector() * (1.0 / face.area());
    bcs.set(mesh, patch.name(),
            std::make_unique<cfd::boundary::FixedGradient>(
                dot(pressureGradient(face.centroid()), outwardNormal)));
  }
  return bcs;
}

}  // namespace mms

}  // namespace cfd::test
