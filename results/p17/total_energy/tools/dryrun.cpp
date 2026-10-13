// P17.5 DRY-RUN against the UNCHANGED tree. No total-energy code exists yet; this measures the
// thermodynamic facts the gate's thresholds must be derived FROM, before the gate is frozen.
//
// Written because of the P17.4 lesson: seven frozen criteria turned out to be measured by nothing
// or by the wrong instrument, largely because thresholds were written before the quantities they
// bound had been measured. Every number this prints is a candidate gate threshold's basis.
//
// Jobs:
//   A. P15's SensibleEnthalpy under the ZERO reference (T_ref = 0, h_ref = 0): is h == cp*T?
//   B. The same under a NONZERO reference (T_ref = 298.15): how far is h from cp*T?
//   C. The proposed internal energy e(T) = h(T) - R*T. Is h - e - R*T identically zero, and
//      does de/dT equal cv? Is e == cv*T, and under which convention?
//   D. T(h) round-trip error against the class's documented 1e-12 K.
//   E. With cp(T) LINEAR, quantify the cp*T-vs-h(T) defect the production thermal path carries.
//   F. p = (gamma-1)(rhoE - 0.5 rho |U|^2) vs p = rho R T, under both conventions.
//   G. Catastrophic cancellation in e = rhoE/rho - 0.5|U|^2 at low Mach (authorization 25).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Real;
using cfd::compressible::ThermodynamicProperties;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;

TemperatureDependentProperty constantCp(Real cp) {
  return TemperatureDependentProperty::constant(cp, "cp");
}

// cp(T) = b0 + b1 T, valid over [200, 2000].
TemperatureDependentProperty linearCp(Real b0, Real b1) {
  return TemperatureDependentProperty(std::vector<Real>{b0, b1}, 200.0, 2000.0, "cp_linear");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const ThermodynamicProperties thermo(kR, kCp);
  const Real cv = thermo.specificHeatVolume();
  const Real gamma = thermo.specificHeatRatio();

  std::printf("P17.5 dry-run -- the UNCHANGED tree\n");
  std::printf("R = %.17g   cp = %.17g   cv = cp - R = %.17g   gamma = %.17g\n", kR, kCp, cv, gamma);
  std::printf("eps = %.17g\n\n", kEps);

  // ---------------------------------------------------------------- A
  std::printf("### A. SensibleEnthalpy, ZERO reference (T_ref = 0, h_ref = 0): is h == cp*T?\n");
  {
    // The property range must admit T_ref = 0, so use a range starting at 0.
    const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
    const SensibleEnthalpy h0(cp, 0.0, 0.0);
    Real worstAbs = 0.0;
    Real worstRel = 0.0;
    for (const Real T : {200.0, 300.0, 500.0, 1000.0, 2000.0}) {
      const Real h = h0.enthalpy(T);
      const Real naive = kCp * T;
      const Real d = std::abs(h - naive);
      worstAbs = std::max(worstAbs, d);
      worstRel = std::max(worstRel, d / std::abs(naive));
      std::printf("  T %-7.1f  h = %-22.17g  cp*T = %-22.17g  diff %.17g\n", T, h, naive, h - naive);
    }
    std::printf("  worst |h - cp*T|              %.17g\n", worstAbs);
    std::printf("  worst relative                %.17g   (%.3g eps)\n", worstRel, worstRel / kEps);
    std::printf("  => under the ZERO reference, h == cp*T is EXACT. This is the calorically\n");
    std::printf("     perfect convention in which e = cv*T and the gamma relation are exact.\n");
  }

  // ---------------------------------------------------------------- B
  std::printf("\n### B. NONZERO reference (T_ref = 298.15): how far is h from cp*T?\n");
  {
    const TemperatureDependentProperty cp = constantCp(kCp);
    const SensibleEnthalpy h298(cp, 298.15, 0.0);
    for (const Real T : {300.0, 500.0, 1000.0}) {
      const Real h = h298.enthalpy(T);
      std::printf("  T %-7.1f  h = %-22.17g  cp*T = %-22.17g  diff %.17g\n", T, h, kCp * T,
                  h - kCp * T);
    }
    std::printf("  predicted offset -cp*T_ref =  %.17g\n", -kCp * 298.15);
    std::printf("  => h != cp*T by exactly the reference offset. Any formula assuming e = cv*T\n");
    std::printf("     or p = (gamma-1)(rhoE - ke) is WRONG under a nonzero reference.\n");
  }

  // ---------------------------------------------------------------- C
  std::printf("\n### C. the proposed e(T) = h(T) - R*T\n");
  {
    for (const Real Tref : {0.0, 298.15}) {
      const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
      const SensibleEnthalpy hModel(cp, Tref, 0.0);
      Real worstIdentity = 0.0;
      Real worstCvT = 0.0;
      for (const Real T : {300.0, 500.0, 1000.0, 2000.0}) {
        const Real h = hModel.enthalpy(T);
        const Real e = h - kR * T;
        worstIdentity = std::max(worstIdentity, std::abs((h - e) - kR * T));
        worstCvT = std::max(worstCvT, std::abs(e - cv * T));
      }
      // de/dT by a central difference, to confirm it is cv and not cp.
      const Real T0 = 800.0;
      const Real dT = 1e-4;
      const Real dedT = ((hModel.enthalpy(T0 + dT) - kR * (T0 + dT)) -
                         (hModel.enthalpy(T0 - dT) - kR * (T0 - dT))) /
                        (2.0 * dT);
      std::printf("  T_ref = %-8.2f  worst |(h-e) - R T| = %-12.17g  worst |e - cv T| = %.17g\n",
                  Tref, worstIdentity, worstCvT);
      std::printf("                    de/dT at 800 K = %-22.17g  cv = %.17g   diff %.3g\n", dedT,
                  cv, std::abs(dedT - cv));
    }
    std::printf("  => h - e = R T holds BY CONSTRUCTION for any reference, and de/dT = cv always.\n");
    std::printf("     e = cv*T holds ONLY under the zero reference. That is the whole contract.\n");
  }

  // ---------------------------------------------------------------- D
  std::printf("\n### D. T(h) round trip, against the documented %.0e K\n",
              SensibleEnthalpy::kInversionToleranceKelvin);
  {
    const TemperatureDependentProperty cp = constantCp(kCp);
    const SensibleEnthalpy hModel(cp, 298.15, 0.0);
    Real worst = 0.0;
    for (const Real T : {250.0, 300.0, 700.0, 1500.0}) {
      const Real back = hModel.temperature(hModel.enthalpy(T));
      worst = std::max(worst, std::abs(back - T));
      std::printf("  T %-7.1f -> h -> T = %-22.17g  err %.17g\n", T, back, back - T);
    }
    std::printf("  worst |T(h(T)) - T|           %.17g   documented bound %.0e\n", worst,
                SensibleEnthalpy::kInversionToleranceKelvin);
  }

  // ---------------------------------------------------------------- E
  std::printf("\n### E. cp(T) LINEAR: the magnitude of the cp*T defect the thermal path carries\n");
  {
    const Real b0 = 900.0;
    const Real b1 = 0.15;
    const TemperatureDependentProperty cp = linearCp(b0, b1);
    const SensibleEnthalpy hModel(cp, 200.0, 0.0);
    for (const Real T : {400.0, 800.0, 1500.0}) {
      const Real h = hModel.enthalpy(T);
      const Real naive = cp.evaluate(T) * T;
      // exact: h = b0 (T - Tref) + b1/2 (T^2 - Tref^2)
      const Real exact = b0 * (T - 200.0) + 0.5 * b1 * (T * T - 200.0 * 200.0);
      std::printf("  T %-7.1f  h = %-20.17g  closed form = %-20.17g  cp(T)*T = %-20.17g\n", T, h,
                  exact, naive);
      std::printf("            |h - closed form| = %-12.3g   cp(T)*T is off by %.6g (%.1f%%)\n",
                  std::abs(h - exact), naive - h, 100.0 * (naive - h) / h);
    }
    std::printf("  => the closed form matches to round-off; cp(T)*T is off by tens of percent.\n");
  }

  // ---------------------------------------------------------------- F
  std::printf("\n### F. p = (gamma-1)(rhoE - 0.5 rho |U|^2)  vs  p = rho R T\n");
  {
    const Real T = 500.0;
    const Real rho = 1.2;
    const Real u = 80.0;
    const Real v = 30.0;
    const Real ke = 0.5 * (u * u + v * v);
    for (const Real Tref : {0.0, 298.15}) {
      const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
      const SensibleEnthalpy hModel(cp, Tref, 0.0);
      const Real e = hModel.enthalpy(T) - kR * T;
      const Real rhoE = rho * (e + ke);
      const Real pFromGamma = (gamma - 1.0) * (rhoE - 0.5 * rho * (u * u + v * v));
      const Real pFromEos = rho * kR * T;
      std::printf("  T_ref = %-8.2f  p(gamma) = %-20.17g  rho R T = %-20.17g  diff %.6g\n", Tref,
                  pFromGamma, pFromEos, pFromGamma - pFromEos);
    }
    std::printf("  => the gamma form is EXACT only under the zero reference. Under a nonzero\n");
    std::printf("     reference it is wrong by (gamma-1) rho (h_ref - cp T_ref), not by round-off.\n");
  }

  // ---------------------------------------------------------------- G
  std::printf("\n### G. cancellation in e = rhoE/rho - 0.5|U|^2 at low Mach (section 25)\n");
  {
    const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
    const SensibleEnthalpy hModel(cp, 0.0, 0.0);
    const Real T = 300.0;
    const Real rho = 1.2;
    const Real e = hModel.enthalpy(T) - kR * T;
    std::printf("  e at 300 K = %.17g   (this is the quantity being recovered)\n", e);
    std::printf("  %-10s %-14s %-22s %-22s %s\n", "|U|", "Mach", "ke", "ke/e", "recovered e error");
    for (const Real speed : {0.1, 1.0, 10.0, 100.0, 300.0}) {
      const Real ke = 0.5 * speed * speed;
      const Real rhoE = rho * (e + ke);
      const Real eBack = rhoE / rho - ke;
      const Real a = thermo.speedOfSound(T);
      std::printf("  %-10.1f %-14.6g %-22.17g %-22.6g %.17g\n", speed, speed / a, ke, ke / e,
                  std::abs(eBack - e));
    }
    std::printf("  => at low Mach ke/e is TINY, so rhoE/rho is dominated by e and the\n");
    std::printf("     subtraction is benign. Cancellation would bite at ke >> e, i.e. HYPERsonic,\n");
    std::printf("     which is the opposite end from this solver's low-Mach regime.\n");
  }


  // ---------------------------------------------------------------- H
  std::printf("\n### H. floating-point FLOORS of the composite identities (threshold basis)\n");
  {
    const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
    const SensibleEnthalpy hModel(cp, 0.0, 0.0);
    struct State { Real rho, T, u, v; };
    const State states[] = {{1.2, 300.0, 10.0, 5.0},
                            {0.35, 1200.0, 250.0, -80.0},
                            {8.0, 250.0, 0.5, 0.0},
                            {1.0, 500.0, 0.0, 0.0}};
    Real worstHtotal = 0.0, worstHtotalRel = 0.0;
    Real worstRoundPC = 0.0, worstRoundCP = 0.0;
    for (const State& s : states) {
      const Real h = hModel.enthalpy(s.T);
      const Real e = h - kR * s.T;
      const Real ke = 0.5 * (s.u * s.u + s.v * s.v);
      const Real rhoE = s.rho * (e + ke);
      const Real p = s.rho * kR * s.T;
      const Real hTotal = h + ke;
      const Real lhs = s.rho * hTotal;
      const Real rhs = rhoE + p;
      const Real d = std::abs(lhs - rhs);
      worstHtotal = std::max(worstHtotal, d);
      worstHtotalRel = std::max(worstHtotalRel, d / std::abs(lhs));
      const Real eBack = rhoE / s.rho - ke;
      const Real Tback = hModel.temperature(eBack + kR * s.T);
      worstRoundPC = std::max(worstRoundPC, std::abs(Tback - s.T));
      const Real rhoEBack = s.rho * ((hModel.enthalpy(Tback) - kR * Tback) + ke);
      worstRoundCP = std::max(worstRoundCP, std::abs(rhoEBack - rhoE) / std::abs(rhoE));
      std::printf("  rho %-6.2f T %-8.1f |U| %-8.2f  rho*hTotal-(rhoE+p) = %-12.4g  %.2f eps rel\n",
                  s.rho, s.T, std::sqrt(s.u * s.u + s.v * s.v), lhs - rhs,
                  (d / std::abs(lhs)) / kEps);
    }
    std::printf("  worst |rho hTotal - (rhoE+p)|         %.17g\n", worstHtotal);
    std::printf("  worst RELATIVE                       %.17g = %.2f eps\n", worstHtotalRel,
                worstHtotalRel / kEps);
    std::printf("  worst T round trip prim-cons-prim    %.17g K\n", worstRoundPC);
    std::printf("  worst rhoE round trip relative       %.17g = %.2f eps\n", worstRoundCP,
                worstRoundCP / kEps);
    std::printf("  => these are the FLOORS. A threshold below them fails a correct\n");
    std::printf("     implementation; one far above them does not discriminate.\n");
  }
  std::printf("\ndone\n");
  return 0;
}
