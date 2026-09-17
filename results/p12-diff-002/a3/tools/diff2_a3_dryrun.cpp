// P12-DIFF-002 A3 dry-run: the two replacement validation instruments, each with a POSITIVE and a
// NEGATIVE control, run before the A3 gate is frozen. Nothing here modifies production.
//
// ===========================================================================================
// A3-1  Direct activation test, replacing the obsolete "the N = 0 solution must be inaccurate"
//       proxy in StructuredQuadProductionCase.NonOrthogonalCorrectionIsActiveOnTheProductionPath.
//
// The property that test exists to establish is that solver.json's `non_orthogonal_corrections`
// actually reaches the assembly and switches the INTERNAL-face non-orthogonal correction on. It
// established that indirectly, by requiring the uncorrected solution to be measurably bad -- a
// premise A2 deliberately removed by making the Dirichlet wall flux second order at N = 0 too.
//
// Direct replacement, at operator level, on a genuinely non-orthogonal mesh:
//   D1  INTERIOR-ONLY rows (cells touching no boundary face, so the wall treatment contributes
//       nothing at all to them) must be BITWISE IDENTICAL between two assemblies that differ only
//       in the correction being off, and must DIFFER once it is on. That is exactly "not applied at
//       N = 0, applied at N >= 1", with the boundary treatment provably excluded.
//   D2  the explicit non-orthogonal contribution to the RHS must be exactly zero on interior rows
//       at N = 0 and non-zero at N >= 1.
//   D3  the Dirichlet wall coefficient must be invariant across N (A2's property), isolated from
//       the diagonal by subtracting the exactly-recomputed internal-face coefficients.
// The case-level half -- that the setting propagates from solver.json into SIMPLESettings -- is
// checked by reading the committed case and asserting the parsed value, then mapping it through the
// production nonOrthogonalOptions().
//
// ===========================================================================================
// A3-2  Corrected cross-section flux accounting, replacing the obsolete boundary-flux estimator in
//       MultiBlockProductionCase.SectorConductionInterfaceConservation.
//
// That test's own helper comment states its premise: "discrete heat flow through every radial face
// line from the solver's own two-point face coefficients (non_orthogonal_corrections 0: the flux is
// exactly coefficient * dT)". A2 made the Dirichlet wall flux the DIFF-002 three-point form, so on
// BOUNDARY faces the flux is no longer `coefficient * dT`; on interior faces it still is (interior
// correction is still gated, and this case runs N = 0). The instrument therefore compares a
// production higher-order boundary flux against a test-only two-point estimate.
//
// Replacement: evaluate every face with the SAME operator production uses, and assemble the flux
// independently from the documented convention (architecture.md section 4)
//     flux_into_owner = -(coefficient * phi_P - farCellCoefficient * phi_F)
//                       + boundaryValueCoefficient * phi_b + explicitFlux
// then sum it over each cross-section independently. Production never computes a cross-section heat
// flow, so this is not circular: the test still forms its own conservation statement and still
// compares against the analytic value.
//
// Threshold derivation (not a tuned number): the assembled steady system enforces zero net flux for
// every cell, so summing cell balances telescopes and the cross-section flows must agree to within
// the accumulated per-cell discrete imbalance. This probe measures that imbalance directly with the
// same operator, so the bound is derived from the quantity under test rather than chosen.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

using namespace cfd;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

discretization::NonOrthogonalCorrectionOptions optionsFor(Index n) {
  pressure_velocity::SIMPLESettings settings;
  settings.nonOrthogonalCorrections = n;
  return pressure_velocity::nonOrthogonalOptions(settings);
}

// ---------------------------------------------------------------------------------------------
// A3-1
// ---------------------------------------------------------------------------------------------

Mesh shearedMesh(Index n, Real shear) {
  // Interior nodes only are displaced, so boundary patches stay exactly planar and prescribed
  // boundary values stay exact (the GRAD-002 instrument lesson).
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? shear * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? shear * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

struct MomentumAssembly {
  algebra::SparseMatrix matrix;
  algebra::Vector rhs;
};

// `forceDisabled` is the NEGATIVE CONTROL for D1/D2: it pretends the case asked for corrections
// while assembling with them off, which is what a broken "the setting never reaches the assembly"
// production path would do. The instrument must then fail.
MomentumAssembly assembleMomentum(const Mesh& mesh, const boundary::BoundaryConditionSet& bcs,
                                  const fields::VectorField& velocity, Index n,
                                  bool forceDisabled) {
  const auto options = optionsFor(n);
  algebra::SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  algebra::Vector rhs(mesh.numberOfCells());
  physics::assembleDiffusionContribution(mesh, 1.0e-3, velocity, bcs, physics::VelocityComponent::U,
                                         builder, rhs, forceDisabled ? false : options.enabled,
                                         options.gradientScheme);
  return {builder.build(), rhs};
}

Real rowSum(const algebra::SparseMatrix& m, Index row) {
  const Index* offsets = m.rowOffsetsData();
  Real s = 0.0;
  for (Index k = offsets[row]; k < offsets[row + 1]; ++k) s += std::abs(m.valuesData()[k]);
  return s;
}

bool interiorRowsDiffer(const Mesh& mesh, const MomentumAssembly& a, const MomentumAssembly& b,
                        const std::vector<bool>& touchesBoundary, std::size_t* differing,
                        Real* worst) {
  *differing = 0;
  *worst = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (touchesBoundary[i]) continue;
    const bool same =
        sameBits(rowSum(a.matrix, i), rowSum(b.matrix, i)) && sameBits(a.rhs[i], b.rhs[i]);
    if (!same) {
      ++(*differing);
      *worst = std::max(*worst, std::abs(a.rhs[i] - b.rhs[i]));
    }
  }
  return *differing > 0;
}

void a3_1(const std::string& label, const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::MovingWall>(Vector3{2.0, 0.0, 0.0}));
  }
  fields::VectorField velocity(mesh.numberOfCells(), Vector3{});
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector3{0.3 + cell.centroid().y, 0.1 * cell.centroid().x, 0.0};
  }
  std::vector<bool> touchesBoundary(mesh.numberOfCells(), false);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) touchesBoundary[face.owner()] = true;
  }
  std::size_t interior = 0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!touchesBoundary[i]) ++interior;
  }

  const MomentumAssembly n0 = assembleMomentum(mesh, bcs, velocity, 0, false);
  const MomentumAssembly n1 = assembleMomentum(mesh, bcs, velocity, 1, false);
  const MomentumAssembly n2 = assembleMomentum(mesh, bcs, velocity, 2, false);
  const MomentumAssembly n1_broken = assembleMomentum(mesh, bcs, velocity, 1, true);

  std::size_t d01 = 0, d12 = 0, dneg = 0;
  Real w01 = 0.0, w12 = 0.0, wneg = 0.0;
  const bool applied = interiorRowsDiffer(mesh, n0, n1, touchesBoundary, &d01, &w01);
  interiorRowsDiffer(mesh, n1, n2, touchesBoundary, &d12, &w12);
  const bool brokenApplied = interiorRowsDiffer(mesh, n0, n1_broken, touchesBoundary, &dneg, &wneg);

  std::printf(
      "A3-1 %-28s interior rows %4zu | D1 N0 vs N1 differ on %4zu rows (max|d rhs| %.3e)"
      " -> %s | N1 vs N2 differ on %4zu rows | NEGATIVE CONTROL (setting ignored):"
      " differ on %4zu rows -> %s\n",
      label.c_str(), interior, d01, w01, applied ? "APPLIED (pass)" : "NOT APPLIED (fail)", d12,
      dneg, brokenApplied ? "APPLIED (would wrongly pass)" : "NOT APPLIED (fails, as required)");
}

// ---------------------------------------------------------------------------------------------
// A3-2
// ---------------------------------------------------------------------------------------------

const Real kSweep = 1.5 * std::acos(-1.0);  // 270 degrees

Real angleOf(const Vector3& p) {
  const Real a = std::atan2(p.y, p.x);
  return (a < 0.0) ? (a + (2.0 * std::acos(-1.0))) : a;
}

Vector3 eTheta(Real t) { return Vector3{-std::sin(t), std::cos(t), 0.0}; }

// `legacyBoundary` is the NEGATIVE CONTROL: the pre-A2 estimator the current test still uses
// (boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * dT). With it the conservation
// statement must FAIL; with the corrected operator it must PASS.
struct SectorFlux {
  std::vector<Real> lineFlow;
  Real maxCellImbalance{0.0};
  Real totalCellImbalance{0.0};
  Real meanFlow{0.0};
  Real spread{0.0};
};

SectorFlux sectorFlux(const Mesh& mesh, const fields::ScalarField& temperature,
                      const boundary::BoundaryConditionSet& tempBcs, Real k, Index nt,
                      bool legacyBoundary) {
  SectorFlux out;
  const Real dTheta = kSweep / static_cast<Real>(3 * nt);
  out.lineFlow.assign((3 * nt) + 1, 0.0);
  std::vector<Real> cellNet(mesh.numberOfCells(), 0.0);
  const fields::VectorField gradT = discretization::gradient(
      mesh, temperature, tempBcs, discretization::GradientScheme::GreenGauss);

  for (const auto& face : mesh.faces()) {
    Real fluxIntoOwner = 0.0;
    if (face.isBoundary()) {
      const Real distance =
          MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), tempBcs);
      const bool prescribed = discretization::prescribesBoundaryValue(bc.type());
      if (legacyBoundary) {
        const auto terms =
            discretization::boundaryFaceDiffusionTerms(mesh, face, k, distance, nullptr, false);
        const Real tb = dynamic_cast<const boundary::ScalarBoundaryCondition&>(bc).boundaryValue(
            temperature[face.owner()], distance);
        fluxIntoOwner = terms.coefficient * (tb - temperature[face.owner()]);
      } else {
        const auto terms =
            discretization::boundaryFaceDiffusionTerms(mesh, face, k, distance, &gradT, prescribed);
        const Real tb = dynamic_cast<const boundary::ScalarBoundaryCondition&>(bc).boundaryValue(
            temperature[face.owner()], distance);
        // architecture.md section 4: the assembled row holds -flux_into_owner.
        fluxIntoOwner = -((terms.coefficient * temperature[face.owner()]) -
                          (terms.farCellCoefficient * temperature[terms.farCell])) +
                        (terms.boundaryValueCoefficient * tb) + terms.explicitFlux;
      }
      cellNet[face.owner()] += fluxIntoOwner;
    } else {
      const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
      const Real c =
          discretization::internalFaceDiffusionTerms(mesh, face, k, dPN, nullptr).coefficient;
      fluxIntoOwner = c * (temperature[*face.neighbor()] - temperature[face.owner()]);
      cellNet[face.owner()] += fluxIntoOwner;
      cellNet[*face.neighbor()] -= fluxIntoOwner;
    }

    // Cross-section accounting: radial face lines, flux along +e_theta.
    const Real t = angleOf(face.centroid());
    const Real s = dot(face.areaVector(), eTheta(t)) / face.area();
    if (std::abs(s) < 0.99) continue;
    const auto j = static_cast<Index>(std::llround(t / dTheta));
    if (j > 3 * nt) continue;
    // fluxIntoOwner is along -n_face; the line flow wants it along +e_theta.
    out.lineFlow[j] += (s > 0.0 ? -1.0 : 1.0) * fluxIntoOwner;
  }

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    out.maxCellImbalance = std::max(out.maxCellImbalance, std::abs(cellNet[i]));
    out.totalCellImbalance += std::abs(cellNet[i]);
  }
  const std::array<Index, 4> key = {0, nt, 2 * nt, 3 * nt};
  Real sum = 0.0;
  for (const Index j : key) sum += out.lineFlow[j];
  out.meanFlow = sum / 4.0;
  for (const Index j : key) {
    out.spread = std::max(out.spread, std::abs(out.lineFlow[j] - out.meanFlow));
  }
  return out;
}

// annularBlocks, transcribed from tests/integration/case/test_multiblock_production_case.cpp:103
// so the dry-run covers the SAME three resolutions the amended test will run.
std::vector<io::MeshBlockConfig> annularBlocks(Index nr, Index nt) {
  const char* names[3] = {"sector_a", "sector_b", "sector_c"};
  std::vector<io::MeshBlockConfig> blocks;
  const Index total = 3 * nt;
  for (Index b = 0; b < 3; ++b) {
    io::MeshBlockConfig block{names[b], nr, nt, {}};
    for (Index j = 0; j <= nt; ++j) {
      const Real t = kSweep * static_cast<Real>((b * nt) + j) / static_cast<Real>(total);
      for (Index i = 0; i <= nr; ++i) {
        const Real r = 1.0 + ((2.0 - 1.0) * static_cast<Real>(i) / static_cast<Real>(nr));
        block.vertices.push_back(Vector3{r * std::cos(t), r * std::sin(t), 0.0});
      }
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

void a3_2(Index nr, Index nt) {
  io::CaseDefinition definition =
      io::CaseReader{}.read("cases/annular_sector_conduction_multiblock");
  definition.mesh.blocks = annularBlocks(nr, nt);
  const std::string scratch = "/tmp/diff2_a3_sector_" + std::to_string(nt);
  std::filesystem::create_directories(scratch);
  io::CaseWriter::write(scratch, definition);
  const app::ProjectRunResult run = app::ProjectRunner::run(scratch);
  if (!run.thermalResult.has_value() || !run.mesh.has_value()) {
    std::printf("A3-2 %zux%zu NO THERMAL RESULT: %s\n", static_cast<std::size_t>(nr),
                static_cast<std::size_t>(nt), run.errorMessage.c_str());
    return;
  }
  const io::SimulationSetup setup = io::CaseBuilder{}.build(*run.caseDefinition);
  const Real k = run.caseDefinition->physics.thermal->conductivity;
  const Real exactFlow = std::log(2.0) / kSweep;
  const auto& T = run.thermalResult->temperature;

  for (const bool legacy : {true, false}) {
    const SectorFlux f = sectorFlux(*run.mesh, T, *setup.temperatureBoundaries, k, nt, legacy);
    // Derived acceptance, independent of which estimator is used: the assembled steady system makes
    // every cell's net flux zero (there is no volumetric source here), satisfied to the linear
    // solver's own tolerance. So ANY correct flux evaluation must reproduce a per-cell balance at
    // that level, and cross-section agreement then follows by telescoping. This case solves to
    // absolute 1e-10 / relative 1e-8, so the per-cell residual target is max(1e-10, 1e-8 |b|); with
    // the flux scale Q = log(2)/sweep ~ 0.147 the bound is the solver's own relative tolerance
    // times that scale, 1e-8 * Q ~ 1.47e-9. Derived from the case configuration, not from the
    // measurement: it is 100x TIGHTER than the assertion it replaces (1e-6 * Q) and the legacy
    // estimator misses it by three to four orders.
    const Real bound = 1.0e-8 * exactFlow;
    const bool balanceOk = f.maxCellImbalance <= bound;
    const bool spreadOk = f.spread <= bound;
    std::printf(
        "A3-2 %-9s nt %3zu | lines: hot %.10f if1 %.10f if2 %.10f cold %.10f | spread"
        " %.3e | max cell imbalance %.3e | derived bound %.3e | mean vs exact %.3e |"
        " balance %s spread %s -> %s\n",
        legacy ? "LEGACY" : "CORRECTED", static_cast<std::size_t>(nt), f.lineFlow[0],
        f.lineFlow[nt], f.lineFlow[2 * nt], f.lineFlow[3 * nt], f.spread, f.maxCellImbalance, bound,
        std::abs(f.meanFlow - exactFlow) / exactFlow, balanceOk ? "OK" : "FAIL",
        spreadOk ? "OK" : "FAIL", (balanceOk && spreadOk) ? "PASS" : "FAIL");
  }
}

}  // namespace

int main() {
  std::printf(
      "# P12-DIFF-002 A3 dry-run: replacement instruments, with controls, BEFORE freezing.\n\n");

  std::printf("## A3-1 direct activation (operator level, interior-only rows)\n");
  a3_1("sheared 16x16 s=0.45", shearedMesh(16, 0.45));
  a3_1("sheared 32x32 s=0.45", shearedMesh(32, 0.45));
  a3_1("orthogonal 16x16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));

  std::printf("\n## A3-1 case-level propagation of solver.json non_orthogonal_corrections\n");
  for (const char* c : {"cases/poiseuille_distorted", "cases/poiseuille_flow",
                        "cases/annular_sector_conduction_multiblock"}) {
    const io::CaseDefinition d = io::CaseReader{}.read(c);
    const io::SimulationSetup s = io::CaseBuilder{}.build(d);
    std::printf("  %-46s case %zu -> settings %zu -> options.enabled %d\n", c,
                static_cast<std::size_t>(d.solver.nonOrthogonalCorrections),
                static_cast<std::size_t>(s.solverSettings.nonOrthogonalCorrections),
                int(pressure_velocity::nonOrthogonalOptions(s.solverSettings).enabled));
  }

  std::printf("\n## A3-2 sector cross-section flux accounting (committed case, 12x30 x3)\n");
  a3_2(8, 20);
  a3_2(12, 30);
  a3_2(18, 45);
  return 0;
}
