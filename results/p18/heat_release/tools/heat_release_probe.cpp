// P18.6 -- INSTRUMENTATION, not production and not a test.
//
// Section 60 asks for measured values rather than PASS/FAIL. This reports every quantity that
// section names, from the built library.
//
// EXPECTED VALUES COME FROM tools/heat_release_reference.py (mpmath, dps=60, no CFDApp code).
// They are round numbers here because the mechanism was chosen with DYADIC molecular weights and
// formation enthalpies so the reference values are exactly representable -- which is what makes
// several of these comparisons bitwise rather than approximate.
//
// BUILD: see tools/run_probe.sh, which records the exact command in the log it writes.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using namespace cfd;
using namespace cfd::species;
using cfd::fields::ScalarField;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kM = 0.0625;
constexpr Real kCp = 1000.0;
constexpr Real kHfB = -2000000.0;
constexpr Real kHfC = 3000000.0;

SensibleEnthalpy cpOf(Real v, const std::string& n) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(v, n), 298.15, 0.0);
}

SpeciesRegistry registry() { return SpeciesRegistry({"A", "B", "C"}); }

MixtureThermodynamics mixture() {
  std::vector<SpeciesThermodynamics> s;
  s.push_back(SpeciesThermodynamics(kM, cpOf(kCp, "cp_A")));
  s.push_back(SpeciesThermodynamics(kM, cpOf(kCp, "cp_B")));
  s.push_back(SpeciesThermodynamics(kM, cpOf(kCp, "cp_C")));
  return MixtureThermodynamics(registry(), std::move(s));
}

MixtureThermodynamics linearMixture() {
  std::vector<SpeciesThermodynamics> s;
  s.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({900.0, 0.25}, 200.0, 3000.0, "cp_A"),
                           298.15, 0.0)));
  s.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({1100.0, -0.15}, 200.0, 3000.0, "cp_B"),
                           298.15, 0.0)));
  s.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({1000.0, 0.05}, 200.0, 3000.0, "cp_C"),
                           298.15, 0.0)));
  return MixtureThermodynamics(registry(), std::move(s));
}

FormationEnthalpies formation() {
  return FormationEnthalpies(registry(), {0.0, kHfB, kHfC});
}

Reaction ab() { return Reaction("A->B", registry(), {{"A", 1.0}}, {{"B", 1.0}}); }
Reaction ac() { return Reaction("A->C", registry(), {{"A", 1.0}}, {{"C", 1.0}}); }

ReactionMechanism mech(const MixtureThermodynamics& m) {
  std::vector<Reaction> r;
  r.push_back(ab());
  return ReactionMechanism(registry(), std::move(r), m);
}

ReactionProgressRates rates(const std::vector<Real>& q, Index cells) {
  ReactionProgressRates out;
  for (const Real value : q) {
    out.perReaction.push_back(ScalarField(cells, value));
  }
  return out;
}

Real relative(Real measured, Real expected) {
  if (expected == 0.0) {
    return std::abs(measured);
  }
  return std::abs(measured - expected) / std::abs(expected);
}

}  // namespace

int main() {
  std::printf("=== P18.6 quantitative evidence (section 60) ===\n");
  std::printf("eps = %.17g\n", kEps);
  std::printf("expected values from tools/heat_release_reference.py (mpmath dps=60)\n\n");

  const auto mix = mixture();
  const auto form = formation();
  const auto mechanism = mech(mix);
  const Index cells = 1;

  // ---------------------------------------------------------------- species enthalpy
  std::printf("--- max species-enthalpy reference error ---\n");
  Real worstHf = 0.0;
  const Real expectedMolar[3] = {0.0, -125000.0, 187500.0};
  for (Index i = 0; i < 3; ++i) {
    const Real r = relative(form.molar(mix, i), expectedMolar[i]);
    worstHf = std::max(worstHf, r);
    std::printf("  hbar_f,%lld = %.17g   expected %.17g   rel %.3e\n", static_cast<long long>(i),
                form.molar(mix, i), expectedMolar[i], r);
  }
  std::printf("  MAX species-enthalpy error : %.3e  (%.2f eps)\n\n", worstHf, worstHf / kEps);

  // ---------------------------------------------------------------- reaction enthalpy
  std::printf("--- max reaction-enthalpy error ---\n");
  Real worstDh = 0.0;
  struct DhCase {
    const char* name;
    Real expected;
  };
  const Real dhAb = standardReactionEnthalpy(ab(), mix, form);
  const Real dhAc = standardReactionEnthalpy(ac(), mix, form);
  std::printf("  Delta H(A->B) = %.17g   expected -125000   rel %.3e\n", dhAb,
              relative(dhAb, -125000.0));
  std::printf("  Delta H(A->C) = %.17g   expected  187500   rel %.3e\n", dhAc,
              relative(dhAc, 187500.0));
  worstDh = std::max(relative(dhAb, -125000.0), relative(dhAc, 187500.0));

  // the temperature-dependent form, against the reference
  const auto linear = linearMixture();
  struct TCase {
    Real t;
    Real expected;
  };
  const std::vector<TCase> tCases{{400.0, -124615.70721875000},
                                  {800.0, -125615.70721875000},
                                  {1500.0, -136990.70721875000},
                                  {2500.0, -174490.70721875000}};
  Real worstDhT = 0.0;
  for (const TCase& c : tCases) {
    const Real measured = reactionEnthalpy(ab(), linear, form, c.t);
    const Real r = relative(measured, c.expected);
    worstDhT = std::max(worstDhT, r);
    std::printf("  Delta H(A->B, T=%6.0f) = %.17g   expected %.17g   rel %.3e (%.2f eps)\n", c.t,
                measured, c.expected, r, r / kEps);
  }
  std::printf("  MAX reaction-enthalpy error : %.3e  (%.2f eps)\n\n", std::max(worstDh, worstDhT),
              std::max(worstDh, worstDhT) / kEps);

  // ---------------------------------------------------------------- Kirchhoff
  std::printf("--- Kirchhoff: d(Delta H_r)/dT vs Delta Cp_r ---\n");
  const Real floorValue = std::cbrt(kEps * kEps);
  std::printf("  eps^(2/3) = %.17g, bound 8 x that\n", floorValue);
  Real worstKirchhoff = 0.0;
  for (const Real t : {400.0, 700.0, 1200.0, 1800.0, 2500.0}) {
    const Real closed = reactionHeatCapacity(ab(), linear, t);
    const Real h = t * 1.0e-6;
    const Real fd = (reactionEnthalpy(ab(), linear, form, t + h) -
                     reactionEnthalpy(ab(), linear, form, t - h)) /
                    (2.0 * h);
    const Real scale = std::abs(reactionEnthalpy(ab(), linear, form, t));
    const Real r = std::abs(fd - closed) / scale;
    worstKirchhoff = std::max(worstKirchhoff, r);
    std::printf("  T=%6.0f  Delta Cp = %12.6f  fd = %12.6f  rel %.3e (%.2f x eps^(2/3))\n", t,
                closed, fd, r, r / floorValue);
  }
  std::printf("  MAX Kirchhoff disagreement : %.3e  (%.2f x eps^(2/3))\n\n", worstKirchhoff,
              worstKirchhoff / floorValue);

  // ---------------------------------------------------------------- the energy source
  std::printf("--- max reaction-energy-source error, and the two routes ---\n");
  Real worstSource = 0.0;
  Real worstRoutes = 0.0;
  const std::vector<std::pair<Real, Real>> qCases{
      {0.0, 0.0}, {1.0, 125000.0}, {2.0, 250000.0}, {7.5, 937500.0}, {1000.0, 125000000.0}};
  for (const auto& c : qCases) {
    const Real molar = reactionHeatRelease(mechanism, mix, form, rates({c.first}, cells), cells)[0];
    const auto sources = reactionSpeciesSources(mechanism, mix, rates({c.first}, cells), cells);
    const Real mass = reactionHeatReleaseFromSpeciesSources(form, sources, cells)[0];
    const Real r = relative(molar, c.second);
    const Real routes = (molar == 0.0) ? std::abs(mass) : std::abs(molar - mass) / std::abs(molar);
    worstSource = std::max(worstSource, r);
    worstRoutes = std::max(worstRoutes, routes);
    std::printf("  q=%9.2f  S_E(molar)=%18.8g  expected %18.8g  rel %.3e   routes agree %.3e\n",
                c.first, molar, c.second, r, routes);
  }
  std::printf("  MAX source error  : %.3e  (%.2f eps)\n", worstSource, worstSource / kEps);
  std::printf("  MAX route disagreement : %.3e  (%.2f eps)\n\n", worstRoutes, worstRoutes / kEps);

  // ---------------------------------------------------------------- linearity
  std::printf("--- max source linearity error: S_E(c q) = c S_E(q) ---\n");
  const Real q1 = 3.25;
  const Real base = reactionHeatRelease(mechanism, mix, form, rates({q1}, cells), cells)[0];
  Real worstLinear = 0.0;
  for (const Real c : {2.0, 0.5, 1.0e6, -1.0, 0.0}) {
    const Real scaled =
        reactionHeatRelease(mechanism, mix, form, rates({c * q1}, cells), cells)[0];
    const Real r = (c * base == 0.0) ? std::abs(scaled) : std::abs(scaled - (c * base)) /
                                                              std::abs(c * base);
    worstLinear = std::max(worstLinear, r);
    std::printf("  c=%10.1f  S_E=%18.8g   c*S_E=%18.8g   rel %.3e%s\n", c, scaled, c * base, r,
                (r == 0.0) ? "   BITWISE" : "");
  }
  std::printf("  MAX linearity error : %.3e  (%.2f eps)\n\n", worstLinear, worstLinear / kEps);

  // ---------------------------------------------------------------- sum omega
  std::printf("--- max |sum_i omega_i| (P18.4's invariant, which P18.6 depends on) ---\n");
  Real worstOmegaSum = 0.0;
  Real omegaScale = 0.0;
  for (const Real q : {1.0, 7.5, 1000.0}) {
    const auto sources = reactionSpeciesSources(mechanism, mix, rates({q}, cells), cells);
    const auto conservation = sourceConservation(sources, cells);
    worstOmegaSum = std::max(worstOmegaSum, conservation.maxAbsoluteSum);
    omegaScale = std::max(omegaScale, conservation.maxAbsoluteSource);
    std::printf("  q=%9.2f  max|sum omega| = %.3e   scale %.6g\n", q, conservation.maxAbsoluteSum,
                conservation.maxAbsoluteSource);
  }
  std::printf("  MAX |sum omega| : %.3e   relative to scale %.3e\n\n", worstOmegaSum,
              (omegaScale > 0.0) ? worstOmegaSum / omegaScale : 0.0);

  // ---------------------------------------------------------------- one-step balance
  std::printf("--- max one-step energy-balance error: Delta u_sens = dt S_E ---\n");
  Real worstStep = 0.0;
  const std::vector<std::pair<Real, Real>> stepCases{{1.0, 1.0e-3}, {100.0, 1.0e-5}, {0.5, 2.0}};
  for (const auto& c : stepCases) {
    const ReactorState state{MassFractionState(registry(), cells, {1.0, 0.0, 0.0}),
                             ScalarField(cells, 1000.0), ScalarField(cells, 1.0)};
    const auto r = rates({c.first}, cells);
    const Real source = reactionHeatRelease(mechanism, mix, form, r, cells)[0];
    const auto next = advanceClosedReactor(mechanism, mix, form, r, state, c.second);
    const Real before =
        energyDensity(mix, form, state.composition.cellComposition(0), 1.0, 1000.0).sensible;
    const Real after = energyDensity(mix, form, next.composition.cellComposition(0), 1.0,
                                     next.temperature[0])
                           .sensible;
    const Real expected = c.second * source;
    const Real err = relative(after - before, expected);
    worstStep = std::max(worstStep, err);
    std::printf("  q=%8.2f dt=%9.1e  measured %.17g  expected %.17g  rel %.3e\n", c.first,
                c.second, after - before, expected, err);
  }
  std::printf("  MAX one-step balance error : %.3e  (%.2f eps)\n\n", worstStep, worstStep / kEps);

  // ---------------------------------------------------------------- temperature recovery
  std::printf("--- max temperature-recovery error ---\n");
  Real worstRecovery = 0.0;
  for (const Real t : {300.0, 1000.0, 2500.0}) {
    for (const Real rho : {0.5, 1.0, 7.25}) {
      const std::vector<Real> comp{0.25, 0.5, 0.25};
      const Real u = rho * mix.specificHeatPressure(comp, t) * t;
      const Real back = temperatureFromSensibleEnergy(mix, comp, rho, u);
      worstRecovery = std::max(worstRecovery, relative(back, t));
    }
  }
  std::printf("  constant cp : MAX %.3e  (%.2f eps)\n", worstRecovery, worstRecovery / kEps);
  Real worstRecoveryT = 0.0;
  for (const Real t : {400.0, 900.0, 1600.0, 2400.0}) {
    const std::vector<Real> comp{0.25, 0.5, 0.25};
    const Real u = 1.3 * linear.specificHeatPressure(comp, t) * t;
    const Real back = temperatureFromSensibleEnergy(linear, comp, 1.3, u);
    worstRecoveryT = std::max(worstRecoveryT, relative(back, t));
  }
  std::printf("  cp(T), a root find : MAX %.3e  (%.2f eps)\n\n", worstRecoveryT,
              worstRecoveryT / kEps);

  // ---------------------------------------------------------------- closed reactor
  std::printf("--- closed-reactor total-energy drift (THE PRIMARY G7 MEASUREMENT) ---\n");
  {
    const Index n = 16;
    const ScalarField volume(n, 2.0);
    ReactorState state{MassFractionState(registry(), n, {1.0, 0.0, 0.0}),
                       ScalarField(n, 1000.0), ScalarField(n, 1.0)};
    const auto m = mech(mix);
    const auto initial =
        totalEnergy(mix, form, state.composition, state.density, state.temperature, volume);
    Real worst = 0.0;
    for (int step = 0; step < 50; ++step) {
      state = advanceClosedReactor(m, mix, form, rates({128.0}, n), state, 1.0e-3);
      const auto now =
          totalEnergy(mix, form, state.composition, state.density, state.temperature, volume);
      worst = std::max(worst, std::abs(now.total - initial.total) / std::abs(initial.total));
    }
    const auto finalEnergy =
        totalEnergy(mix, form, state.composition, state.density, state.temperature, volume);
    std::printf("  cells %lld, 50 steps, q = 128, dt = 1e-3\n", static_cast<long long>(n));
    std::printf("  E_initial  = %.17g J   (sensible %.17g, chemical %.17g)\n", initial.total,
                initial.sensible, initial.chemical);
    std::printf("  E_final    = %.17g J   (sensible %.17g, chemical %.17g)\n", finalEnergy.total,
                finalEnergy.sensible, finalEnergy.chemical);
    std::printf("  abs drift  = %.6e J\n", std::abs(finalEnergy.total - initial.total));
    std::printf("  rel drift  = %.6e  (%.2f eps);  WORST over the march %.6e (%.2f eps)\n",
                std::abs(finalEnergy.total - initial.total) / std::abs(initial.total),
                std::abs(finalEnergy.total - initial.total) / std::abs(initial.total) / kEps,
                worst, worst / kEps);
    std::printf("  G7 allows 8 eps N_cells = %.6e  (%.1f eps)\n",
                8.0 * kEps * static_cast<Real>(n), 8.0 * static_cast<Real>(n));
    std::printf("  T: 1000 -> %.6f K   Y_A: 1 -> %.6f\n", state.temperature[0],
                state.composition.massFraction(0, 0));
    std::printf("  the two pools moved oppositely: d(sensible) = %+.6e, d(chemical) = %+.6e\n",
                finalEnergy.sensible - initial.sensible, finalEnergy.chemical - initial.chemical);
  }
  std::printf("\n");

  // ---------------------------------------------------------------- adiabatic balance
  std::printf("--- final temperature vs the INDEPENDENT adiabatic balance ---\n");
  {
    ReactorState state{MassFractionState(registry(), cells, {1.0, 0.0, 0.0}),
                       ScalarField(cells, 1000.0), ScalarField(cells, 1.0)};
    const auto m = mech(mix);
    state = advanceClosedReactor(m, mix, form, rates({8.0}, cells), state, 1.0);
    std::printf("  50%% conversion : T = %.17g   independent balance 2000   rel %.3e (%.2f eps)\n",
                state.temperature[0], relative(state.temperature[0], 2000.0),
                relative(state.temperature[0], 2000.0) / kEps);
    state = advanceClosedReactor(m, mix, form, rates({8.0}, cells), state, 1.0);
    std::printf("  complete       : T = %.17g   independent balance 3000   rel %.3e (%.2f eps)\n",
                state.temperature[0], relative(state.temperature[0], 3000.0),
                relative(state.temperature[0], 3000.0) / kEps);
  }
  std::printf("\n");

  // ---------------------------------------------------------------- non-reacting limit
  std::printf("--- max non-reacting thermal difference (section 34) ---\n");
  {
    const Index n = 32;
    ScalarField t(n);
    ScalarField rho(n);
    ScalarField a(n);
    ScalarField b(n);
    ScalarField c(n);
    for (Index cell = 0; cell < n; ++cell) {
      const Real s = static_cast<Real>(cell) / static_cast<Real>(n);
      a[cell] = 0.25 + (0.5 * s);
      b[cell] = 0.5 - (0.25 * s);
      c[cell] = 1.0 - a[cell] - b[cell];
      t[cell] = 800.0 + (20.0 * static_cast<Real>(cell));
      rho[cell] = 0.75 + (0.01 * static_cast<Real>(cell));
    }
    std::vector<ScalarField> fields{a, b, c};
    ReactorState state{MassFractionState(registry(), std::move(fields)), t, rho};
    const auto m = mech(mix);
    ReactorState next = state;
    for (int step = 0; step < 10; ++step) {
      next = advanceClosedReactor(m, mix, form, rates({0.0}, n), next, 1.0);
    }
    Real worstT = 0.0;
    Real worstY = 0.0;
    long long nonBitwise = 0;
    for (Index cell = 0; cell < n; ++cell) {
      if (next.temperature[cell] != state.temperature[cell]) {
        ++nonBitwise;
      }
      worstT = std::max(worstT, std::abs(next.temperature[cell] - state.temperature[cell]));
      for (Index i = 0; i < 3; ++i) {
        const Real d = std::abs(next.composition.massFraction(i, cell) -
                                state.composition.massFraction(i, cell));
        if (d != 0.0) {
          ++nonBitwise;
        }
        worstY = std::max(worstY, d);
      }
    }
    std::printf("  10 zero-rate steps, %lld cells, non-uniform state\n",
                static_cast<long long>(n));
    std::printf("  max |dT|   = %.17g K\n", worstT);
    std::printf("  max |dY|   = %.17g\n", worstY);
    std::printf("  values differing bitwise : %lld   (MUST be 0)\n", nonBitwise);
  }
  std::printf("\n");

  // ---------------------------------------------------------------- determinism
  std::printf("--- max determinism difference ---\n");
  {
    const Index n = 64;
    const ScalarField volume(n, 1.25);
    ScalarField t(n);
    ScalarField rho(n);
    for (Index cell = 0; cell < n; ++cell) {
      t[cell] = 900.0 + (10.0 * static_cast<Real>(cell));
      rho[cell] = 0.8 + (0.005 * static_cast<Real>(cell));
    }
    const ReactorState start{MassFractionState(registry(), n, {0.6, 0.3, 0.1}), t, rho};
    const auto r = rates({0.4}, n);
    const auto m0 = mech(mix);
    const auto firstStep = advanceClosedReactor(m0, mix, form, r, start, 1.0e-3);
    const auto firstHeat = reactionHeatRelease(m0, mix, form, r, n);
    const auto firstEnergy = totalEnergy(mix, form, firstStep.composition, firstStep.density,
                                         firstStep.temperature, volume);
    long long diffs = 0;
    const int repeats = 200;
    for (int repeat = 0; repeat < repeats; ++repeat) {
      const auto mix2 = mixture();
      const auto form2 = formation();
      const auto m = mech(mix2);
      const auto step = advanceClosedReactor(m, mix2, form2, r, start, 1.0e-3);
      const auto heat = reactionHeatRelease(m, mix2, form2, r, n);
      const auto energy =
          totalEnergy(mix2, form2, step.composition, step.density, step.temperature, volume);
      for (Index cell = 0; cell < n; ++cell) {
        if (heat[cell] != firstHeat[cell]) {
          ++diffs;
        }
        if (step.temperature[cell] != firstStep.temperature[cell]) {
          ++diffs;
        }
        for (Index i = 0; i < 3; ++i) {
          if (step.composition.massFraction(i, cell) !=
              firstStep.composition.massFraction(i, cell)) {
            ++diffs;
          }
        }
      }
      if (energy.total != firstEnergy.total) {
        ++diffs;
      }
    }
    std::printf("  %d repeats x %lld cells x (S_E + T + 3 Y) + total energy, REBUILDING the\n",
                repeats, static_cast<long long>(n));
    std::printf("  mixture, formation enthalpies and mechanism each time\n");
    std::printf("  bitwise differences : %lld\n", diffs);
  }
  std::printf("\n");

  // ---------------------------------------------------------------- chemistry feedback
  std::printf("--- max chemistry-feedback rate error (section 21) ---\n");
  std::printf("  measured in the test suite against exp(-(Ea/R)(1/T1 - 1/T0));\n");
  std::printf("  see TemperatureFeedbackTest.N3_HeatReleaseRaisesTheRateAtTheNextEvaluation\n");
  return 0;
}
