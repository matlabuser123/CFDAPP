#pragma once

// P12-MESH-004 -- the mesh-quality / numerical-effect campaign machinery:
// a manufactured solution compatible with UNIFORM production boundary
// conditions, the controlled mesh families (as production mesh.json
// configurations), and one production-path run:
//
//   case directory (CaseWriter) -> CaseReader -> CaseBuilder (production
//   mesh, MeshQuality gate, boundary objects, solver settings) -> SIMPLE
//   (production settings from solver.json) + the manufactured momentum
//   source -> ThermalSolver (production settings) + the manufactured heat
//   source -> error norms against the exact solution, iterations,
//   conservation.
//
// The manufactured source is the only non-production input -- an MMS needs
// a forcing term by definition. Boundary conditions are the case's own
// ("wall", zero-gradient pressure, fixed_temperature 0) because the exact
// solution satisfies them exactly on the unit square:
//   streamfunction psi = sin^2(pi x) sin^2(pi y) / pi
//     u =  sin^2(pi x) sin(2 pi y),   v = -sin(2 pi x) sin^2(pi y)
//     (div u = 0; u = v = 0 on every wall -- no-slip)
//   p = cos(pi x) cos(2 pi y)          (dp/dn = 0 on every wall)
//   T = sin(pi x) sin(pi y)            (T = 0 on every wall)
//   rho = 1, mu = 0.05 (Re = 20), k = 0.2, cp = 1 (Pe = 5: the production energy
//   equation convects with first-order upwind, so diffusion is kept significant);
//   f = rho (u . grad) u + grad p - mu lap u,
//   q = rho cp u . grad T - k lap T,
// every derivative derived by hand from the closed forms (never a discrete
// operator of the code under test).

#include <filesystem>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/io/case/CaseDefinition.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"

namespace cfd::test::meshq {

inline constexpr Real kDensity = 1.0;
inline constexpr Real kViscosity = 0.05;
inline constexpr Real kConductivity = 0.2;
inline constexpr Real kSpecificHeat = 1.0;

[[nodiscard]] Vector2 exactVelocity(const Vector2& p);
[[nodiscard]] Real exactPressure(const Vector2& p);
[[nodiscard]] Real exactTemperature(const Vector2& p);
[[nodiscard]] Vector2 momentumForcing(const Vector2& p);
[[nodiscard]] Real heatSource(const Vector2& p);

// --- Mesh families on the unit square (production mesh.json content) -----
// Uniform Cartesian nx x ny (aspect ratio nx / ny).
[[nodiscard]] cfd::io::MeshConfig cartesianMesh(Index nx, Index ny);
// n x n with y geometric grading "both" of adjacent-cell ratio r (x
// uniform); r = 1 is the uniform mesh.
[[nodiscard]] cfd::io::MeshConfig gradedMesh(Index n, Real ratio);
// n x n structured_quad of the smooth boundary-preserving mapping
//   x = X + A sin(pi X) sin(2 pi Y),   y = Y + A sin(2 pi X) sin(pi Y):
// finite-amplitude non-orthogonality/skewness that stays the same under
// refinement (a smooth curvilinear mesh).
[[nodiscard]] cfd::io::MeshConfig smoothDistortedMesh(Index n, Real amplitude);
// n x n structured_quad whose interior vertices are displaced by
// fraction * h * (xi, eta), (xi, eta) in [-1, 1)^2 from a deterministic
// integer hash of (i, j): cell-scale ("rough") distortion, the same
// relative size at every resolution.
[[nodiscard]] cfd::io::MeshConfig roughDistortedMesh(Index n, Real fraction);

struct Discretization {
  Index nonOrthogonalCorrections{2};
  std::string gradientScheme{"least_squares"};
  std::string convectionScheme{"linear_upwind"};
  // SIMPLE velocity / pressure / continuity tolerance and relaxation
  // (results/p12-mesh-004/acceptance_gate.md, "Common configuration").
  Real tolerance{1e-7};
  Real velocityRelaxation{0.8};
  Real pressureRelaxation{0.4};
};

// The complete case (rectangle 1 x 1, the given mesh, walls, thermal) as a
// CaseDefinition, from the committed cases/heated_cavity template.
[[nodiscard]] cfd::io::CaseDefinition caseDefinition(const cfd::io::MeshConfig& mesh,
                                                     const Discretization& discretization);

struct Run {
  bool built{false};       // CaseBuilder accepted the case
  std::string buildError;  // its message otherwise
  cfd::mesh::MeshQualityReport quality;
  Index cells{0};
  Real h{0.0};         // sqrt(domain area / cells)
  std::string status;  // SIMPLE status name
  bool converged{false};
  bool finite{false};
  Index iterations{0};
  Real finalU{0.0}, finalV{0.0}, finalP{0.0}, finalContinuity{0.0};
  // Mean contraction of the continuity residual per SIMPLE iteration:
  // (final / first)^(1 / (iterations - 1)).
  Real continuityContraction{0.0};
  cfd::validation::SolveAcceptance acceptance;  // P12-NUM-005 gate (mass tolerance 1e-10)
  double seconds{0.0};
  cfd::validation::ErrorNorms u, v, pressure, temperature;
  Real massImbalance{0.0};      // |global net mass flux|
  Real maxCellContinuity{0.0};  // max |cell net mass flux| / cell area
  bool thermalConverged{false};
  std::string thermalStatus;
  Index thermalIterations{0};
  Real thermalLinearInitial{0.0}, thermalLinearFinal{0.0};  // last outer iteration's linear solve
  Real energyImbalance{0.0};  // |sum over cells of the energy residual| / sum |q| V
  Real kineticEnergy{0.0};    // 1/2 sum |u_c|^2 V_c   (exact 3/16)
  Real meanTemperature{0.0};  // sum T_c V_c           (exact 4/pi^2)
};

// Writes `definition` to `directory` (CaseWriter) and runs the production
// path described above.
[[nodiscard]] Run run(const std::filesystem::path& directory,
                      const cfd::io::CaseDefinition& definition);

inline constexpr Real kExactKineticEnergy = 3.0 / 16.0;
[[nodiscard]] Real exactMeanTemperature();  // 4 / pi^2
// |global mass imbalance| tolerance of the P12-NUM-005 solve gate and R7.
inline constexpr Real kMassImbalanceTolerance = 1e-10;

// One-line human-readable record of a run (for logs).
[[nodiscard]] std::string describe(const std::string& label, const Run& r);

// One CSV record of a run (every quality metric and measured quantity), and
// the matching header -- the campaign's machine-readable evidence.
[[nodiscard]] std::string csvHeader();
[[nodiscard]] std::string csvRow(const std::string& family, const std::string& level,
                                 Real parameter, const std::string& recipe, const Run& r);

}  // namespace cfd::test::meshq
