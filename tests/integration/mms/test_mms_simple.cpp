// P12-NUM-006: full SIMPLE method-of-manufactured-solutions -- the core
// system-level gate. The production cfd::pressure_velocity::SIMPLE solver
// (momentum predictor, pressure correction, velocity/flux correction, outer
// convergence monitor) runs from rest (U = 0, p = 0) with:
//   - the analytical momentum forcing f = rho (U.grad)U + grad p - mu lap U
//     through the generic momentum source (SIMPLE::setMomentumSource),
//   - exact Dirichlet velocity on every boundary face (closed box: the
//     manufactured normal wall velocity is zero, tangential non-zero),
//   - exact Neumann pressure data (FixedGradient grad p . n) -- the
//     pressure-correction sees a closed Neumann domain; the gauge is fixed
//     by the reference cell,
// and must iteratively recover the manufactured velocity AND pressure.
// Pressure is compared modulo its gauge: both fields shifted to zero
// volume-weighted mean (cfd::validation::computeGaugeInvariantErrorNorms).
// No forcing is ever built from a discrete operator.
//
// Grids: 8/16/32 in the default suite (Debug ~1.5-4 min per study), the
// 16/32/64 study as DISABLED_FineGridStudy (explicit run, ~25 min).
// Outer tolerance 1e-8 (absolute, every residual): the errors agree with a
// 1e-10 run to 5 significant digits -- iterative error << discretization
// error (results/p12-num-006/summary.md).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "MMSCases.hpp"

using cfd::Index;
using cfd::Real;
using cfd::discretization::ConvectionScheme;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;
namespace mc = cfd::test::mmscase;

namespace {

std::string failures(const MMSStudy& study, const std::string& category) {
  std::string text;
  mc::categoryPassed(study, category, &text);
  return text;
}

MMSStudy buildSimpleStudy(ConvectionScheme scheme, const std::vector<Index>& grids, bool distorted,
                          const std::string& name) {
  MMSStudy study;
  study.name = name;
  study.description = std::string("Full SIMPLE MMS from rest (U = 0, p = 0), ") +
                      (distorted ? "distorted (0.25 h) meshes, 2 non-orthogonal corrections, "
                                   "least-squares gradient"
                                 : "Cartesian meshes");
  study.manufacturedSolution =
      "mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; "
      "mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)";
  study.coefficients = {{"density", mc::kDensity}, {"viscosity", mc::kViscosity}};
  study.configuration = {
      {"solver", "cfd::pressure_velocity::SIMPLE + setMomentumSource"},
      {"convection", mc::schemeName(scheme)},
      {"gradient", distorted ? "least_squares" : "green_gauss"},
      {"relaxation", "velocity 0.8, pressure 0.4"},
      {"outer_tolerance", "1e-8 (velocity, pressure, continuity; absolute)"},
      {"inner_solvers", "BiCGSTAB rel 1e-3 / abs 1e-12 (pressure: Jacobi), P12-NUM-004 fallback"},
      {"velocity_bc", "exact Dirichlet (Inlet) on every boundary face"},
      {"pressure_bc", "exact Neumann FixedGradient(grad p . n) per face; reference cell 0"},
      {"pressure_gauge", "zero volume-weighted mean for both fields"},
      {"initial_condition", "U = 0, p = 0"},
      {"forcing", "analytical at cell centroids; never discrete_operator(exact_solution)"},
      {"solve_gate", "assessSimpleSolve: Converged, finite, global mass imbalance <= 1e-10"}};
  mc::SimpleOptions options;
  options.scheme = scheme;
  options.distorted = distorted;
  for (const Index n : grids) study.levels.push_back(mc::runSimpleLevel(n, options));
  if (!mc::allLevelsAccepted(study)) return study;

  const bool upwind = scheme == ConvectionScheme::Upwind;
  // Velocity: upwind is first-order class; on 8/16/32 it is still
  // pre-asymptotic (measured three-grid p 0.63 -> 0.79 on 16/32/64, pairwise
  // 0.76, 0.86, 0.93 -> 1): band [0.55, 1.3] with a reduction-factor floor
  // below. Central: second-order class [1.6, 2.4] (measured 1.8-1.9).
  const Real vLo = upwind ? 0.55 : 1.6;
  const Real vHi = upwind ? 1.3 : 2.4;
  // P12-DIFF-002 A6-3 (validation-migration/acceptance_gate_A6.md section 2.3): gated on the finest
  // PAIRWISE observed order rather than the three-level Richardson triplet, with an upper limit of
  // formal + 1 -- the order of DIFF-002's faster-converging boundary ring, which makes the total
  // error a two-rate A h^formal + B h^(formal+1) whose pairwise order decays to formal while the
  // triplet estimator overshoots above formal + 1. See MMSCases.hpp addPairwiseOrderGate. The lower
  // limit vLo is unchanged: it is what detects a loss of formal accuracy.
  const Real vHiPairwise = (upwind ? 1.0 : 2.0) + 1.0;
  static_cast<void>(vHi);
  for (const std::string c : {"u", "v"}) {
    mc::addDecreaseGate(study, "velocity", c);
    mc::addPairwiseOrderGate(study, "velocity", c, NormKind::L2, upwind ? 1.0 : 2.0, vLo,
                             vHiPairwise);
    mc::addPairwiseOrderGate(study, "velocity", c, NormKind::L1, upwind ? 1.0 : 2.0, vLo,
                             vHiPairwise);
  }
  mc::addOrder(study, "velocity", NormKind::L2, upwind ? 1.0 : 2.0);
  mc::addOrder(study, "u", NormKind::Linf, upwind ? 1.0 : 2.0);
  {
    const auto& factors = study.orders.front().reductionFactors;  // u L2
    const Real finest = factors.back().value_or(0.0);
    mc::addGate(
        study, std::string("velocity: u L2 finest reduction factor >= ") + (upwind ? "1.6" : "3.0"),
        finest >= (upwind ? 1.6 : 3.0), mc::format("E_coarse/E_fine = %.3f", finest));
  }
  // Pressure (modulo gauge): follows the momentum accuracy -- first order
  // with upwind (measured 3-grid p 1.34 on 8/16/32, pairwise 1.21, 1.05,
  // 1.0 -> 1), second-order class with central (measured 1.69-1.9).
  mc::addDecreaseGate(study, "pressure", "p");
  mc::addOrderGate(study, "pressure", "p", NormKind::L2, upwind ? 1.0 : 2.0, upwind ? 0.8 : 1.5,
                   upwind ? 1.6 : 2.4);
  mc::addOrderGate(study, "pressure", "p", NormKind::L1, upwind ? 1.0 : 2.0, upwind ? 0.8 : 1.5,
                   upwind ? 1.6 : 2.4);
  // Linf is set by the boundary ring (one-sided Neumann reconstruction) --
  // recorded, gated only on decreasing (addDecreaseGate above).
  mc::addOrder(study, "p", NormKind::Linf, upwind ? 1.0 : 2.0);
  mc::addOrder(study, "p_interior", NormKind::L2, upwind ? 1.0 : 2.0);
  mc::addOrder(study, "p_boundary_ring", NormKind::L2, upwind ? 1.0 : 2.0);
  // Continuity: SIMPLE satisfies its discrete continuity equation up to the
  // iterative tolerance on every grid; the converged face flux converges to
  // the exact face flux at the velocity order.
  for (std::size_t k = 0; k < study.levels.size(); ++k) {
    const auto& c = study.error(k, "continuity");
    mc::addGate(study,
                "continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at " +
                    study.levels[k].name,
                c.linf <= 1e-5, mc::format("Linf %.2e", c.linf) + mc::format(", L2 %.2e", c.l2));
  }
  mc::addDecreaseGate(study, "continuity", "face_flux");
  // The converged face flux must converge AT LEAST at the velocity order.
  // Measured on 8/16/32 with central convection it converges faster
  // (reduction factors 9.4, 5.1 -> p 3.4, pre-asymptotic from above,
  // heading for 4); a faster-decaying secondary quantity is not a
  // correctness failure, so the upper bound is only a sanity cap (4).
  mc::addOrderGate(study, "continuity", "face_flux", NormKind::L2, upwind ? 1.0 : 2.0, vLo, 4.0);
  // Global mass: every boundary face carries the prescribed (zero-normal)
  // velocity, so the net boundary flux is zero to round-off.
  for (const auto& level : study.levels) {
    mc::addGate(study, "mass: global mass imbalance <= 1e-14 at " + level.name,
                level.massImbalance.has_value() && *level.massImbalance <= 1e-14,
                mc::format("%.2e", level.massImbalance.value_or(1.0)));
  }
  return study;
}

const MMSStudy& simpleStudy(ConvectionScheme scheme) {
  static std::map<ConvectionScheme, MMSStudy> cache;
  if (auto it = cache.find(scheme); it != cache.end()) return it->second;
  MMSStudy study =
      buildSimpleStudy(scheme, {8, 16, 32}, false,
                       scheme == ConvectionScheme::Upwind ? "simple_mms_upwind" : "simple_mms");
  return cache.emplace(scheme, std::move(study)).first->second;
}

void expectCategory(ConvectionScheme scheme, const std::string& category) {
  const MMSStudy& study = simpleStudy(scheme);
  for (const auto& level : study.levels) {
    ASSERT_TRUE(level.accepted) << mc::schemeName(scheme) << " " << level.name << ": "
                                << level.rejectionReason;
  }
  EXPECT_TRUE(mc::categoryPassed(study, category, nullptr)) << mc::schemeName(scheme) << ":\n"
                                                            << failures(study, category);
}

Real maxAbsDifference(const std::vector<Real>& a, const std::vector<Real>& b) {
  Real m = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(a[i] - b[i]));
  return m;
}

std::vector<Real> gaugeShifted(std::vector<Real> p) {
  Real mean = 0.0;
  for (const Real value : p) mean += value;
  mean /= static_cast<Real>(p.size());  // uniform Cartesian cells: volume weights are equal
  for (Real& value : p) value -= mean;
  return p;
}

}  // namespace

// The solver starts away from the manufactured solution -- at rest, and
// (second run) from a reversed, rescaled velocity with a large pressure
// offset -- and iterates to the same discrete solution in both cases.
TEST(SIMPLEMMS, ConvergesFromNonExactInitialCondition) {
  mc::SimpleOptions options;
  const auto fromRest = mc::runSimpleFields(16, options, 0.0, 0.0);
  const auto fromReversed = mc::runSimpleFields(16, options, -0.5, 10.0);
  ASSERT_EQ(fromRest.status, "Converged");
  ASSERT_EQ(fromReversed.status, "Converged");
  EXPECT_GT(fromRest.iterations, 100u);  // genuinely iterated, not started at the answer
  // Same discrete solution (velocity; pressure modulo gauge) to within the
  // iterative tolerance -- measured ~1e-9, bound 1e-6 (discretization error
  // at 16x16 is ~2e-2).
  const Real du = maxAbsDifference(fromRest.u, fromReversed.u);
  const Real dv = maxAbsDifference(fromRest.v, fromReversed.v);
  const Real dp = maxAbsDifference(gaugeShifted(fromRest.p), gaugeShifted(fromReversed.p));
  std::printf(
      "\nSIMPLE MMS 16x16 upwind: from rest %llu outer iterations, from (-0.5 U_exact, p = 10) "
      "%llu; max |difference| u %.3e, v %.3e, p (modulo gauge) %.3e\n",
      static_cast<unsigned long long>(fromRest.iterations),
      static_cast<unsigned long long>(fromReversed.iterations), du, dv, dp);
  EXPECT_LT(du, 1e-6);
  EXPECT_LT(dv, 1e-6);
  EXPECT_LT(dp, 1e-6);
  // And that solution is the manufactured one to within the discretization
  // error (upwind, 16x16: velocity L2 ~2e-2).
  const auto level = mc::runSimpleLevel(16, options);
  ASSERT_TRUE(level.accepted) << level.rejectionReason;
  EXPECT_LT(level.errors.front().second.l2, 0.05);  // "u"
}

TEST(SIMPLEMMS, VelocityConvergesAtExpectedOrder) {
  expectCategory(ConvectionScheme::Upwind, "velocity");
  expectCategory(ConvectionScheme::Central, "velocity");
  // Reports written by this test only (no concurrent writers).
  mc::writeStudy(simpleStudy(ConvectionScheme::Central), "simple_mms");
  mc::writeStudy(simpleStudy(ConvectionScheme::Upwind), "simple_mms_upwind");
}

TEST(SIMPLEMMS, PressureConvergesAtExpectedOrder) {
  expectCategory(ConvectionScheme::Upwind, "pressure");
  expectCategory(ConvectionScheme::Central, "pressure");
}

TEST(SIMPLEMMS, ContinuityConverges) { expectCategory(ConvectionScheme::Central, "continuity"); }

TEST(SIMPLEMMS, GlobalMassBalance) { expectCategory(ConvectionScheme::Upwind, "mass"); }

TEST(SIMPLEMMS, Deterministic) {
  mc::SimpleOptions options;
  options.scheme = ConvectionScheme::Central;
  const auto a = mc::runSimpleFields(8, options, 0.0, 0.0);
  const auto b = mc::runSimpleFields(8, options, 0.0, 0.0);
  ASSERT_EQ(a.status, "Converged");
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.u, b.u);  // bitwise
  EXPECT_EQ(a.v, b.v);
  EXPECT_EQ(a.p, b.p);
}

// Full SIMPLE on distorted meshes with the P12-NUM-003 non-orthogonal
// correction (2 passes, least-squares gradient), central convection:
// converges from rest and keeps the Cartesian second-order class.
TEST(SIMPLEMMS, DistortedMesh) {
  MMSStudy study =
      buildSimpleStudy(ConvectionScheme::Central, {8, 16, 32}, true, "distorted_mesh_simple_mms");
  for (const auto& level : study.levels)
    ASSERT_TRUE(level.accepted) << level.name << ": " << level.rejectionReason;
  mc::writeStudy(study, "distorted_mesh_simple_mms");
  for (const std::string category : {"velocity", "pressure", "continuity", "mass"}) {
    EXPECT_TRUE(mc::categoryPassed(study, category, nullptr)) << failures(study, category);
  }
}

// The 16/32/64 study (both schemes) -- the asymptotic-range evidence of
// results/p12-num-006. ~25 min in Debug: run explicitly with
// --gtest_also_run_disabled_tests.
TEST(SIMPLEMMS, DISABLED_FineGridStudy) {
  for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central}) {
    const MMSStudy study = buildSimpleStudy(
        scheme, {16, 32, 64}, false,
        scheme == ConvectionScheme::Upwind ? "simple_mms_fine_upwind" : "simple_mms_fine");
    for (const auto& level : study.levels) ASSERT_TRUE(level.accepted) << level.name;
    mc::writeStudy(study, study.name);
    for (const std::string category : {"velocity", "pressure", "continuity", "mass"}) {
      EXPECT_TRUE(mc::categoryPassed(study, category, nullptr)) << mc::schemeName(scheme) << ":\n"
                                                                << failures(study, category);
    }
  }
}
