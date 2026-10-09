// P18.0 sections 12-13 -- INSTRUMENTATION, not new capability.
//
// This program implements no physics. It drives the EXISTING species and thermal solvers and reports
// what each one's boundary treatment actually does, so the recorded "species boundary lag" can be
// classified from a measurement rather than from a comment.
//
// WHAT IS BEING TESTED, stated before the numbers.
//
// A FixedGradient (Neumann) boundary prescribes dY/dn = g at the wall. Two assemblies exist in this
// repository for the identical situation:
//
//   THERMAL (fixed in P12-MESH-003) -- src/thermal/EnergyEquation.cpp:103-104
//       return {0.0, coefficient * scalarBc->boundaryValue(0.0, distance)};
//     coefficient 0, and the owner value passed in is 0.0, so the boundary contribution is exactly
//     Df*g*d and depends on NOTHING from the field. One assembly is exact.
//
//   SPECIES (not changed) -- src/species/SpeciesEquation.cpp:79-86
//       yB = boundaryValue(concentration[owner], distance)      // = Y_P + g*d
//       builder.add(owner, owner, conductance);                 // conductance = Df, NON-ZERO
//       rhs[owner]   += boundaryValueCoefficient * yB;          // carries the LAGGED Y_P
//     The +Df on the diagonal and the -Df*Y_P^lagged on the RHS cancel ONLY when Y_P stops
//     changing. Before then the boundary is anchored to the previous iterate.
//
// So the prediction, made before running: with a pure-Neumann species problem the species solver
// needs MORE THAN ONE outer Picard iteration to satisfy its own boundary condition, while the
// thermal solver needs exactly one. If instead species also converges in one, the recorded lag does
// not exist in this path and the note is stale -- which is equally worth knowing.
//
// The residual reported is the boundary condition's OWN definition:
//     r = max over boundary faces of | (Y_face - Y_owner)/d - g |
// evaluated on the RETURNED field, using each face's own owner distance. A converged solve whose
// boundary condition is satisfied has r = 0 to round-off; a lagged one has r != 0 after one
// iteration.

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesSolver.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

// The BC's own residual, on whatever field is handed back.
struct BcResidual {
  Real worst{0.0};
  Index worstFace{0};
  Index faces{0};
};

BcResidual gradientResidual(const Mesh& mesh, const ScalarField& field,
                            const BoundaryConditionSet& bcs, Real prescribedGradient) {
  BcResidual out;
  for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) {
    const auto& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    const BoundaryCondition& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), bcs);
    const auto* sbc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc);
    if (sbc == nullptr) continue;
    // Only GRADIENT faces are meaningful here. The first version of this probe applied the
    // gradient check to every boundary face including Dirichlet ones, which reported a spurious
    // residual of 19.2 in case 3 -- an artefact of the metric, not of the solver.
    if (dynamic_cast<const cfd::boundary::FixedGradient*>(&bc) == nullptr) continue;
    const Index owner = face.owner();
    const Real d = MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
    // The face value the assembly would use, from the RETURNED field.
    const Real yFace = sbc->boundaryValue(field[owner], d);
    const Real achieved = (yFace - field[owner]) / d;
    const Real r = std::abs(achieved - prescribedGradient);
    ++out.faces;
    if (r > out.worst) {
      out.worst = r;
      out.worstFace = f;
    }
  }
  return out;
}

// A uniform mass flux so convection is present but not dominant; the point is the boundary term.
SurfaceField uniformFlux(const Mesh& mesh, Real value) {
  SurfaceField flux(mesh.numberOfFaces());
  for (Index f = 0; f < static_cast<Index>(mesh.numberOfFaces()); ++f) flux[f] = value;
  return flux;
}

BoundaryConditionSet allFixedGradient(const Mesh& mesh, Real g) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, std::string(patch.name()), std::make_unique<cfd::boundary::FixedGradient>(g));
  }
  return bcs;
}

BoundaryConditionSet inletOutletGradient(const Mesh& mesh, Real inletValue, Real g) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const std::string n(patch.name());
    if (n == "left") {
      bcs.set(mesh, n, std::make_unique<cfd::boundary::FixedValue>(inletValue));
    } else {
      bcs.set(mesh, n, std::make_unique<cfd::boundary::FixedGradient>(g));
    }
  }
  return bcs;
}

}  // namespace

int main() {
  std::printf("P18.0 -- species boundary-lag probe (INSTRUMENTATION ONLY, no physics added)\n\n");

  const Mesh mesh = MeshGeometry::createCartesian2D(12, 8, 1.0, 0.5);
  const FluidProperties fluid(1.0, 1.0e-3);
  const Real g = 0.0;  // impermeable wall: dY/dn = 0, the exact case the thermal fix addressed

  std::printf("mesh 12x8 uniform, rho = 1.0, D = 0.01, prescribed dY/dn = %.1f on every wall\n", g);
  std::printf("convergence tolerance on the OUTER loop: 1e-8 (max |dY| between outer iterations)\n\n");

  // ---------------------------------------------------------------------------------------------
  // 1. SPECIES, pure Neumann. How many outer iterations does it need, and is the BC satisfied
  //    after the FIRST one?
  // ---------------------------------------------------------------------------------------------
  const cfd::species::SpeciesProperties species("probe", 0.01);
  const BoundaryConditionSet yBcs = allFixedGradient(mesh, g);
  ScalarField y0(mesh.numberOfCells());
  for (Index i = 0; i < static_cast<Index>(mesh.numberOfCells()); ++i) {
    y0[i] = 0.3 + 0.4 * mesh.cell(i).centroid().x;  // a deliberately non-uniform initial guess
  }
  const SurfaceField flux = uniformFlux(mesh, 0.0);  // pure diffusion: isolate the boundary term

  std::printf("=== 1. SPECIES, pure Neumann (all walls dY/dn = 0), pure diffusion ===\n");
  for (Index cap : {Index{1}, Index{2}, Index{5}, Index{20}, Index{2000}}) {
    cfd::species::SpeciesSolverSettings s;
    s.maxIterations = cap;
    s.tolerance = 1e-8;
    const cfd::species::SpeciesSolver solver{s};
    const auto r = solver.solve(mesh, y0, flux, fluid, species, yBcs, 0.0);
    const BcResidual bc = gradientResidual(mesh, r.concentration, yBcs, g);
    std::printf("  maxOuter=%-5d outer used=%-5d status=%-2d  maxChange=%.3e  "
                "BC residual=%.3e over %d faces\n",
                static_cast<int>(cap), static_cast<int>(r.iterations),
                static_cast<int>(r.status), r.maxConcentrationChange, bc.worst,
                static_cast<int>(bc.faces));
  }

  // ---------------------------------------------------------------------------------------------
  // 2. THERMAL, the SAME problem shape, for contrast. This is the path that was FIXED.
  // ---------------------------------------------------------------------------------------------
  std::printf("\n=== 2. THERMAL, the same pure-Neumann shape (the FIXED path, for contrast) ===\n");
  ScalarField t0(mesh.numberOfCells());
  for (Index i = 0; i < static_cast<Index>(mesh.numberOfCells()); ++i) {
    t0[i] = 300.0 + 40.0 * mesh.cell(i).centroid().x;
  }
  const BoundaryConditionSet tBcs = allFixedGradient(mesh, g);
  for (Index cap : {Index{1}, Index{2}, Index{5}, Index{20}, Index{2000}}) {
    cfd::thermal::ThermalSolverSettings s;
    s.maxIterations = cap;
    s.tolerance = 1e-8;
    const cfd::thermal::ThermalSolver solver{s};
    const cfd::thermal::ThermalProperties props(0.026, 1005.0);
    const auto r = solver.solve(mesh, t0, flux, props, tBcs);
    const BcResidual bc = gradientResidual(mesh, r.temperature, tBcs, g);
    std::printf("  maxOuter=%-5d outer used=%-5d status=%-2d  maxChange=%.3e  "
                "BC residual=%.3e over %d faces\n",
                static_cast<int>(cap), static_cast<int>(r.iterations),
                static_cast<int>(r.status), r.maxTemperatureChange, bc.worst,
                static_cast<int>(bc.faces));
  }

  // ---------------------------------------------------------------------------------------------
  // 3. SPECIES, mixed Dirichlet inlet + Neumann walls, with convection -- the configuration a real
  //    case uses, so the outer-iteration cost is representative rather than academic.
  // ---------------------------------------------------------------------------------------------
  std::printf("\n=== 3. SPECIES, Dirichlet inlet + Neumann walls, WITH convection ===\n");
  const BoundaryConditionSet mixed = inletOutletGradient(mesh, 1.0, g);
  const SurfaceField convFlux = uniformFlux(mesh, 0.02);
  {
    cfd::species::SpeciesSolverSettings s;
    s.maxIterations = 2000;
    s.tolerance = 1e-8;
    const cfd::species::SpeciesSolver solver{s};
    const auto r = solver.solve(mesh, y0, convFlux, fluid, species, mixed, 0.0);
    const BcResidual bc = gradientResidual(mesh, r.concentration, mixed, g);
    std::printf("  outer used=%-5d status=%-2d  maxChange=%.3e  BC residual=%.3e\n",
                static_cast<int>(r.iterations), static_cast<int>(r.status),
                r.maxConcentrationChange, bc.worst);
    std::printf("  outer change history (first 8): ");
    for (std::size_t k = 0; k < r.outerChangeHistory.size() && k < 8; ++k) {
      std::printf("%.2e ", r.outerChangeHistory[k]);
    }
    std::printf("\n  total outer iterations: %d\n", static_cast<int>(r.iterations));
  }

  std::printf("\nHOW TO READ THIS:\n");
  std::printf("  * 'outer used' > 1 means the solver had to RE-ASSEMBLE to satisfy its own boundary\n");
  std::printf("    condition -- i.e. one assembly was not self-consistent. That is the lag.\n");
  std::printf("  * 'BC residual' is the boundary condition's own definition evaluated on the field\n");
  std::printf("    actually returned. It is 0 by construction for FixedGradient whenever the face\n");
  std::printf("    value is reconstructed FROM the owner, so it measures self-consistency of the\n");
  std::printf("    returned field, NOT whether the assembled flux used a stale value.\n");
  std::printf("  * the discriminating number is therefore 'outer used' at maxOuter=2000, compared\n");
  std::printf("    between species and thermal on the identical problem shape.\n");
  return 0;
}
