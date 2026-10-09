// P18.2 -- INSTRUMENTATION, not production and not a test.
//
// Measures the SPECIES SUM of the assembled system directly, instead of
// inferring it from a drifted solution. The question A1 turns on is whether
//
//     sum_i [ L_i(Y_i) + K(Y_i) ]  =  0     per cell
//
// and this measures each piece separately so the residue can be attributed
// rather than guessed at.
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/species/SpeciesTransport.hpp"

using namespace cfd;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using namespace cfd::species;

int main() {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const Index ns = 2;

  ScalarField a(n);
  ScalarField b(n);
  for (Index c = 0; c < n; ++c) {
    const Real t = static_cast<Real>(c) / static_cast<Real>(n - 1);
    a[c] = 0.2 + (0.6 * t);
    b[c] = 1.0 - a[c];
  }
  const MassFractionState state(SpeciesRegistry({"A", "B"}), {a, b});
  const ScalarField rho(n, 1.2);
  const auto diff = makeSpeciesDiffusivities(rho, {1.0e-4, 4.0e-4});

  std::vector<std::unique_ptr<BoundaryConditionSet>> sets;
  for (Index i = 0; i < ns; ++i) {
    auto set = std::make_unique<BoundaryConditionSet>();
    for (const char* patch : {"left", "right", "bottom", "top"}) {
      set->set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    sets.push_back(std::move(set));
  }
  std::vector<const BoundaryConditionSet*> bcs;
  for (const auto& s : sets) {
    bcs.push_back(s.get());
  }

  const auto sources = zeroSpeciesSources(ns, n);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const auto fluxes = speciesDiffusiveFaceFluxes(mesh, state, diff, bcs);

  std::printf("=== Sigma_f, the assembled raw flux sum per face ===\n");
  Real maxSigma = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    maxSigma = std::max(maxSigma, std::abs(fluxes.rawSum[f]));
  }
  std::printf("  max_f |Sigma_f| = %.17g   (must be non-zero or K is the zero operator)\n",
              maxSigma);
  const auto residual = closureResidual(fluxes);
  std::printf("  max_f |sum_i J_i| = %.17g  against tolerance %.17g\n", residual.maxAbsoluteSum,
              closureTolerance(ns, residual.maxAbsoluteRaw));
  std::printf("\n");

  // ---- the decisive measurement: species sum of the assembled LHS on the
  // CURRENT state, with the transient OFF so only L + K remain ----
  SpeciesTransportOptions options;
  options.convection = false;  // no physical convection; only diffusion + the correction

  std::printf("=== species sum of ( A_i Y_i - b_i ) with transient OFF ===\n");
  std::printf("    for the CURRENT state, so L_i and K are evaluated on the same Y.\n");
  std::printf("    If A1's cancellation is exact this is 0 to round-off, per cell.\n");
  std::vector<Real> sumResidual(n, 0.0);
  for (Index i = 0; i < ns; ++i) {
    const auto assembly = speciesTransportAssembly(mesh, i, state, noFlow, diff, *bcs[i],
                                                   sources.perSpecies[i], SpeciesTimeState{},
                                                   &fluxes, options);
    algebra::Vector y(n, 0.0);
    for (Index c = 0; c < n; ++c) {
      y[c] = state.massFraction(i, c);
    }
    const algebra::Vector ay = assembly.system.matrix().multiply(y);
    for (Index c = 0; c < n; ++c) {
      sumResidual[c] += ay[c] - assembly.system.rhs()[c];
    }
  }
  Real worst = 0.0;
  Index worstCell = 0;
  for (Index c = 0; c < n; ++c) {
    if (std::abs(sumResidual[c]) > worst) {
      worst = std::abs(sumResidual[c]);
      worstCell = c;
    }
  }
  std::printf("  max_cell |sum_i (A_i Y_i - b_i)| = %.17g  at cell %zu\n", worst,
              static_cast<std::size_t>(worstCell));
  std::printf("  for scale: max_f |Sigma_f| = %.17g\n", maxSigma);
  std::printf("  first 6 cells:");
  for (Index c = 0; c < 6 && c < n; ++c) {
    std::printf(" %.3g", sumResidual[c]);
  }
  std::printf("\n\n");

  // ---- the same, with the correction DISABLED, so the residue attributable to
  // the uncorrected raw flux is visible for comparison ----
  SpeciesTransportOptions noCorrection = options;
  noCorrection.closureCorrection = false;
  std::vector<Real> sumNoCorr(n, 0.0);
  for (Index i = 0; i < ns; ++i) {
    const auto assembly = speciesTransportAssembly(mesh, i, state, noFlow, diff, *bcs[i],
                                                   sources.perSpecies[i], SpeciesTimeState{},
                                                   nullptr, noCorrection);
    algebra::Vector y(n, 0.0);
    for (Index c = 0; c < n; ++c) {
      y[c] = state.massFraction(i, c);
    }
    const algebra::Vector ay = assembly.system.matrix().multiply(y);
    for (Index c = 0; c < n; ++c) {
      sumNoCorr[c] += ay[c] - assembly.system.rhs()[c];
    }
  }
  Real worstNo = 0.0;
  for (Index c = 0; c < n; ++c) {
    worstNo = std::max(worstNo, std::abs(sumNoCorr[c]));
  }
  std::printf("=== the same with closureCorrection = false (control NV3) ===\n");
  std::printf("  max_cell |sum_i (A_i Y_i - b_i)| = %.17g\n", worstNo);
  std::printf("  ratio corrected/uncorrected = %.6g\n", worstNo == 0.0 ? 0.0 : worst / worstNo);

  // ---- one TRANSIENT step, with the inner iteration, reporting the solver
  // status and the sum(Y) drift that the conservation test measures ----
  std::printf("\n=== one transient step: solver status, inner convergence, sum(Y) drift ===\n");
  algebra::LinearSolverSettings settings;
  settings.absoluteTolerance = 1.0e-15;
  settings.relativeTolerance = 1.0e-15;
  settings.maxIterations = 3000;
  const algebra::BiCGSTAB solver(settings);

  const MassFractionState old = state;
  SpeciesTimeState time;
  time.transient = true;
  time.dt = 0.5;
  time.fieldsOld = &old.fields();
  time.rhoOld = &rho;
  time.rhoNew = &rho;

  std::vector<ScalarField> next(ns, ScalarField(n, 0.0));
  for (Index i = 0; i < ns; ++i) {
    next[i] = old.field(i);
  }
  for (Index inner = 0; inner < 60; ++inner) {
    // The iterate is RAW FIELDS: it legitimately violates sum(Y)=1 until it
    // converges, and MassFractionState correctly refuses such a composition.
    const auto innerFluxes =
        speciesDiffusiveFaceFluxes(mesh, old.registry(), next, diff, bcs);
    Real change = 0.0;
    Real worstSolverResidual = 0.0;
    Index worstIterations = 0;
    int allConverged = 1;
    for (Index i = 0; i < ns; ++i) {
      const auto assembly =
          speciesTransportAssembly(mesh, i, old.registry(), next, noFlow, diff, *bcs[i],
                                   sources.perSpecies[i], time, &innerFluxes, {});
      algebra::Vector guess(n, 0.0);
      for (Index c = 0; c < n; ++c) {
        guess[c] = next[i][c];
      }
      const auto result = solver.solve(assembly.system, guess);
      if (result.status != algebra::SolverStatus::Converged) {
        allConverged = 0;
      }
      worstSolverResidual = std::max(worstSolverResidual, result.finalResidual);
      worstIterations = std::max(worstIterations, result.iterations);
      for (Index c = 0; c < n; ++c) {
        change = std::max(change, std::abs(result.solution[c] - next[i][c]));
        next[i][c] = result.solution[c];
      }
    }
    Real maxSumDev = 0.0;
    for (Index c = 0; c < n; ++c) {
      Real sum = 0.0;
      for (Index i = 0; i < ns; ++i) {
        sum += next[i][c];
      }
      maxSumDev = std::max(maxSumDev, std::abs(sum - 1.0));
    }
    if (inner < 6) {
      std::printf(
          "  inner %2zu  change %.3e  solverRes %.3e  iters %2zu  converged %d  max|sumY-1| %.6e\n",
          static_cast<std::size_t>(inner), change, worstSolverResidual,
          static_cast<std::size_t>(worstIterations), allConverged, maxSumDev);
    }
    if (change <= 1.0e-13) {
      std::printf("  inner loop CONVERGED at iteration %zu, max|sumY-1| = %.6e\n",
                  static_cast<std::size_t>(inner), maxSumDev);
      break;
    }
    if (inner == 59) {
      std::printf("  inner loop DID NOT CONVERGE in 60: change %.3e, max|sumY-1| %.6e\n", change,
                  maxSumDev);
    }
  }
  return 0;
}
