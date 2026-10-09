// P18.5 -- INSTRUMENTATION, not production and not a test.
//
// Section 48 asks for measured values rather than PASS/FAIL. This prints every quantity those
// sections name, taken from the built library.
//
// IT CARRIES NO EXPECTED VALUES. Where a quantity needs the independent reference, the probe
// prints CFDApp's number at 17 significant figures in a machine-readable form and
// tools/compare_probe.py recomputes the reference in mpmath and reports the error in units of eps.
// Nothing is transcribed by hand in either direction, so a wrong digit in a literal -- the one
// failure mode a reference comparison cannot catch -- is unrepresentable here.
//
// The self-contained measurements (the A1 branch agreement, determinism, control margins) are
// INTERNAL comparisons: two routes through the same library, or a defective formula against the
// correct one. Those need no external reference and are reported directly.
//
// BUILD: see tools/run_probe.sh, which records the exact command in the log it writes.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using namespace cfd;
using namespace cfd::species;
using cfd::fields::ScalarField;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kA = 1.5e8;
constexpr Real kN = 0.5;
constexpr Real kEa = 1.25e5;
constexpr Real kMH2 = 0.002016;
constexpr Real kMO2 = 0.031998;
constexpr Real kMH2O = 0.018015;

SensibleEnthalpy cpOf(Real v, const std::string& n) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(v, n), 298.15, 0.0);
}

SpeciesRegistry registry() { return SpeciesRegistry({"H2", "O2", "H2O"}); }

MixtureThermodynamics mixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kMH2, cpOf(14307.0, "cp_H2")));
  species.push_back(SpeciesThermodynamics(kMO2, cpOf(918.0, "cp_O2")));
  species.push_back(SpeciesThermodynamics(kMH2O, cpOf(1996.0, "cp_H2O")));
  return MixtureThermodynamics(registry(), std::move(species));
}

ReactionMechanism mechanism() {
  std::vector<Reaction> reactions;
  reactions.push_back(
      Reaction("2H2+O2->2H2O", registry(), {{"H2", 2.0}, {"O2", 1.0}}, {{"H2O", 2.0}}));
  return ReactionMechanism(registry(), std::move(reactions), mixture());
}

ReactionKinetics kinetics(const ReactionMechanism& m) {
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back({"2H2+O2->2H2O", ArrheniusParameters(kA, kN, kEa), std::nullopt});
  return ReactionKinetics(m, std::move(parameters));
}

// 17 significant figures round-trips a double exactly.
void emit(const char* key, Real value) { std::printf("%s %.17g\n", key, value); }

}  // namespace

int main() {
  std::printf("=== P18.5 quantitative evidence (section 48) ===\n");
  emit("EPS", kEps);
  std::printf("MECHANISM 2H2+O2->2H2O\n");
  emit("A", kA);
  emit("NEXP", kN);
  emit("EA", kEa);
  emit("M_H2", kMH2);
  emit("M_O2", kMO2);
  emit("M_H2O", kMH2O);
  emit("R_U", kUniversalGasConstant);
  std::printf("\n");

  const ArrheniusParameters parameters(kA, kN, kEa);

  // ---------------------------------------------------------------- k(T) and x(T)
  std::printf("--- k(T) and the exponential argument, for mpmath comparison ---\n");
  for (const Real t : {300.0, 500.0, 800.0, 1200.0, 1500.0, 1800.0, 2500.0, 3000.0}) {
    std::printf("KT %.17g %.17g %.17g\n", t, parameters.rateCoefficient(t),
                parameters.exponentialArgument(t));
  }
  std::printf("\n");

  // ---------------------------------------------------------------- dk/dT
  std::printf("--- dk/dT: closed form vs central difference ---\n");
  std::printf("the step is h = T * 1e-6, DERIVED: a central difference's total error is minimised\n");
  std::printf("near h ~ T * eps^(1/3), where the truncation and round-off terms balance, and the\n");
  std::printf("floor on the relative agreement is then eps^(2/3) = %.17g\n", std::cbrt(kEps * kEps));
  Real worstDerivative = 0.0;
  for (const Real t : {400.0, 600.0, 1000.0, 1500.0, 2000.0, 2400.0, 3000.0}) {
    const Real closed = parameters.rateCoefficientDerivative(t);
    const Real h = t * 1.0e-6;
    const Real fd =
        (parameters.rateCoefficient(t + h) - parameters.rateCoefficient(t - h)) / (2.0 * h);
    const Real relative = std::abs(fd - closed) / std::abs(closed);
    worstDerivative = std::max(worstDerivative, relative);
    std::printf("DKDT %.17g closed %.17g fd %.17g rel %.6e (%.2f x eps^(2/3))\n", t, closed, fd,
                relative, relative / std::cbrt(kEps * kEps));
  }
  std::printf("worst relative derivative disagreement %.6e = %.2f x eps^(2/3), bound 8\n",
              worstDerivative, worstDerivative / std::cbrt(kEps * kEps));
  std::printf("\n");

  // ---------------------------------------------------------------- amendment A1
  std::printf("--- AMENDMENT A1: the direct form vs log space ---\n");
  const ArrheniusParameters reduced(kA, 0.0, 0.0);
  const Real reducedK = reduced.rateCoefficient(1500.0);
  std::printf("A1_REDUCED k %.17g  A %.17g  difference %.17g  bitwise %s\n", reducedK, kA,
              reducedK - kA, (reducedK == kA) ? "YES" : "NO");
  std::printf("  (log-space-always gave 150000000.00000015 here -- 4.5 eps,\n");
  std::printf("   preserved at logs/01_first_failures_PRESERVED.log)\n");

  // the two forms compared directly, where both are safe
  Real worstBranch = 0.0;
  Real worstBranchT = 0.0;
  for (Real t = 300.0; t <= 3000.0; t += 25.0) {
    const Real direct = parameters.rateCoefficient(t);
    const Real viaLog = std::exp(parameters.logRateCoefficient(t));
    const Real relative = std::abs(direct - viaLog) / direct;
    if (relative > worstBranch) {
      worstBranch = relative;
      worstBranchT = t;
    }
  }
  std::printf("A1_BRANCH_AGREEMENT worst relative %.6e at T = %.17g = %.2f eps (G6 allows 32)\n",
              worstBranch, worstBranchT, worstBranch / kEps);

  // the fallback must still be LIVE: A T^n overflows while k itself is representable
  const ArrheniusParameters overflowing(1.0e300, 40.0, 3.0e6);
  const Real powerLaw = 1.0e300 * std::pow(1000.0, 40.0);
  const Real fallbackK = overflowing.rateCoefficient(1000.0);
  std::printf("A1_FALLBACK_LIVE A T^n = %.6e (finite %s), k = %.17g (finite %s)\n", powerLaw,
              std::isfinite(powerLaw) ? "YES" : "NO -- so the log-space branch IS taken", fallbackK,
              std::isfinite(fallbackK) ? "YES" : "NO");
  std::printf("\n");

  // ---------------------------------------------------------------- the G6 matrix
  std::printf("--- THE G6 MATRIX: CFDApp's values, for mpmath comparison ---\n");
  std::printf("    MATRIX yH2 yO2 p T rho q omega_H2 omega_O2 omega_H2O sumAbs srcAbs\n");
  const auto mech = mechanism();
  const auto kin = kinetics(mech);
  const auto mix = mixture();
  const auto mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const Index cells = mesh.numberOfCells();

  struct Composition {
    Real yH2;
    Real yO2;
  };
  const std::vector<Composition> compositions{{0.10, 0.80}, {0.60, 0.30}, {0.05, 0.90}, {0.0, 0.50}};
  Index emitted = 0;
  for (const Composition& comp : compositions) {
    const MassFractionState state(registry(), cells, {comp.yH2, comp.yO2, 1.0 - comp.yH2 - comp.yO2});
    for (const Real p : {50000.0, 101325.0, 1000000.0}) {
      for (const Real t : {800.0, 1500.0, 2200.0}) {
        const Real rho = mix.density(state.cellComposition(0), p, t);
        const auto rates =
            arrheniusProgressRates(mech, kin, mix, state, ScalarField(cells, rho),
                                   ScalarField(cells, t));
        const auto sources = reactionSpeciesSources(mech, mix, rates, cells);
        const auto conservation = sourceConservation(sources, cells);
        std::printf("MATRIX %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                    comp.yH2, comp.yO2, p, t, rho, rates.perReaction[0][0],
                    sources.perSpecies[0].Su[0], sources.perSpecies[1].Su[0],
                    sources.perSpecies[2].Su[0], conservation.maxAbsoluteSum,
                    conservation.maxAbsoluteSource);
        ++emitted;
      }
    }
  }
  std::printf("MATRIX_ROWS %lld\n", static_cast<long long>(emitted));
  std::printf("\n");

  // ---------------------------------------------------------------- control margins
  std::printf("=== control margins, quantified (sections 39, 48) ===\n");
  const Real t0 = 1500.0;
  const Real correct = parameters.rateCoefficient(t0);
  const Real x = parameters.exponentialArgument(t0);
  std::printf("  measured at T = %.17g,  k = %.17g,  x = -Ea/(R_u T) = %.17g\n", t0, correct, x);
  std::printf("  N1 exponent sign flipped        exp(-x)/exp(x)    ratio %.6e\n",
              std::exp(-x) / std::exp(x));
  std::printf("  N2 activation energy omitted    k(Ea=0)/k         ratio %.6e\n",
              ArrheniusParameters(kA, kN, 0.0).rateCoefficient(t0) / correct);
  std::printf("  N3 T^n omitted                  k/k(n=0)          ratio %.6e\n",
              correct / ArrheniusParameters(kA, 0.0, kEa).rateCoefficient(t0));
  std::printf("  N4 R in J/(kmol K)              exp ratio         ratio %.6e\n",
              std::exp(-kEa / (kUniversalGasConstant * 1000.0 * 1000.0)) /
                  std::exp(-kEa / (kUniversalGasConstant * 1000.0)));
  std::printf("  N5 A ignored                    k/k(A=1)          ratio %.6e\n",
              correct / ArrheniusParameters(1.0, kN, kEa).rateCoefficient(t0));

  // N6: a global rate coefficient against the per-cell one
  {
    const Index n = 8;
    const MassFractionState state(registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
    ScalarField temperature(n);
    for (Index c = 0; c < n; ++c) {
      temperature[c] = 800.0 + (250.0 * static_cast<Real>(c));
    }
    const auto rates =
        arrheniusProgressRates(mech, kin, mix, state, ScalarField(n, 0.5), temperature);
    std::printf("  N6 global T instead of cell T   q[last]/q[first]  ratio %.6e\n",
                rates.perReaction[0][n - 1] / rates.perReaction[0][0]);
    std::printf("     (a cached global coefficient would make this ratio exactly 1)\n");
  }

  // N7: the concentration dependence, order 3 in total
  {
    const MassFractionState state(registry(), cells, {0.10, 0.80, 1.0 - 0.10 - 0.80});
    const auto lo = arrheniusProgressRates(mech, kin, mix, state, ScalarField(cells, 0.5),
                                           ScalarField(cells, 1500.0));
    const auto hi = arrheniusProgressRates(mech, kin, mix, state, ScalarField(cells, 1.0),
                                           ScalarField(cells, 1500.0));
    const Real ratio = hi.perReaction[0][0] / lo.perReaction[0][0];
    std::printf("  N7 concentration omitted        q(2 rho)/q(rho)   ratio %.17g (2^3 = 8 exactly"
                " for total order 3: %s)\n",
                ratio, (ratio == 8.0) ? "YES" : "NO");
  }
  std::printf("\n");

  // ---------------------------------------------------------------- determinism
  std::printf("=== determinism ===\n");
  {
    const Index n = 256;
    const MassFractionState state(registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
    ScalarField temperature(n);
    for (Index c = 0; c < n; ++c) {
      temperature[c] = 700.0 + (8.0 * static_cast<Real>(c));
    }
    const ScalarField density(n, 1.2);
    const auto first = arrheniusProgressRates(mech, kin, mix, state, density, temperature);
    const auto firstSources = reactionSpeciesSources(mech, mix, first, n);
    long long diffs = 0;
    const int repeats = 200;
    for (int r = 0; r < repeats; ++r) {
      const auto m2 = mechanism();
      const auto again = arrheniusProgressRates(m2, kinetics(m2), mix, state, density, temperature);
      const auto sources = reactionSpeciesSources(m2, mix, again, n);
      for (Index c = 0; c < n; ++c) {
        if (again.perReaction[0][c] != first.perReaction[0][c]) {
          ++diffs;
        }
        for (Index i = 0; i < 3; ++i) {
          if (sources.perSpecies[i].Su[c] != firstSources.perSpecies[i].Su[c]) {
            ++diffs;
          }
        }
      }
    }
    std::printf("  %d repeats x %lld cells x (1 rate + 3 sources), RECONSTRUCTING the mechanism\n",
                repeats, static_cast<long long>(n));
    std::printf("  and the kinetics each time: bitwise differences = %lld\n", diffs);
  }
  std::printf("\n");

  // ---------------------------------------------------------------- the Arrhenius plot
  std::printf("=== the Arrhenius plot, n = 0 (section 35) ===\n");
  {
    const ArrheniusParameters flat(kA, 0.0, kEa);
    const Real t1 = 800.0;
    const Real t2 = 2000.0;
    const Real slope = (std::log(flat.rateCoefficient(t2)) - std::log(flat.rateCoefficient(t1))) /
                       ((1.0 / t2) - (1.0 / t1));
    const Real expectedSlope = -kEa / kUniversalGasConstant;
    const Real intercept = std::log(flat.rateCoefficient(t1)) + (kEa / kUniversalGasConstant / t1);
    std::printf("  slope recovered %.17g  expected -Ea/R_u %.17g  rel %.2f eps\n", slope,
                expectedSlope, std::abs(slope - expectedSlope) / std::abs(expectedSlope) / kEps);
    std::printf("  intercept recovered %.17g  expected ln A %.17g  rel %.2f eps\n", intercept,
                std::log(kA), std::abs(intercept - std::log(kA)) / std::abs(std::log(kA)) / kEps);
  }
  return 0;
}
