// P17.2 dry-run: the UNCHANGED tree's thermodynamic closure, recorded at %.17g before
// any edit so every gate criterion is checked against a measurement rather than memory.
//
// Two jobs:
//   1. Reference values for the identities, so "unchanged" is a string comparison later.
//   2. Show that the three-way closure is INCOMPLETE: (p,T)->rho and (rho,T)->p exist,
//      (p,rho)->T does not. If temperature() already existed the criterion would be
//      vacuous and the gate would need redesigning rather than freezing.
#include <cmath>
#include <cstdio>
#include <limits>

#include "cfd/compressible/IdealGasEOS.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::IdealGasEOS;
using cfd::compressible::ThermodynamicProperties;

namespace {

struct Gas {
  Real R, cp;
  const char* label;
};

struct State {
  Real p, T;
  const char* label;
};

}  // namespace

int main() {
  std::printf("P17.2 dry-run -- thermodynamic closure on the UNCHANGED tree\n\n");

  // Deliberately spread over low / ambient / high p and T, and three different gases,
  // so a hard-coded return cannot reproduce the table.
  const Gas gases[] = {{287.05, 1005.0, "air"},
                       {2077.10, 5193.0, "helium"},
                       {461.50, 1996.0, "water vapour"}};
  const State states[] = {{1.0e3, 200.0, "low p, low T"},
                          {101325.0, 288.15, "sea-level standard"},
                          {5.0e6, 1500.0, "high p, high T"}};

  std::printf("### A. property identities, per gas -- cp-cv=R and gamma=cp/cv\n");
  std::printf("%-14s %-22s %-22s %-22s %-24s %-24s\n", "gas", "R", "cp", "cv", "(cp-cv)-R",
              "gamma-cp/cv");
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    const Real cv = t.specificHeatVolume();
    const Real gamma = t.specificHeatRatio();
    std::printf("%-14s %-22.17g %-22.17g %-22.17g %-24.17g %-24.17g\n", g.label, t.gasConstant(),
                t.specificHeatPressure(), cv, (t.specificHeatPressure() - cv) - t.gasConstant(),
                gamma - t.specificHeatPressure() / cv);
  }

  std::printf("\n### B. gamma > 1 must hold for every admissible (R, cp)\n");
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    std::printf("  %-14s gamma = %-22.17g  gamma > 1 ? %s\n", g.label, t.specificHeatRatio(),
                t.specificHeatRatio() > 1.0 ? "yes" : "*** NO ***");
  }

  std::printf("\n### C. p = rho R T, and the two directions that EXIST\n");
  std::printf("%-14s %-16s %-16s %-22s %-24s %-24s\n", "gas", "p", "T", "rho=density(p,T)",
              "p-rho*R*T (abs)", "pressure(rho,T)-p (rel)");
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    for (const auto& s : states) {
      const Real rho = t.density(s.p, s.T);
      const Real residual = s.p - rho * t.gasConstant() * s.T;
      const Real pBack = t.equationOfState().pressure(rho, s.T);
      std::printf("%-14s %-16.6g %-16.6g %-22.17g %-24.17g %-24.17g\n", g.label, s.p, s.T, rho,
                  residual, (pBack - s.p) / s.p);
    }
  }

  std::printf("\n### D. THE GAP: the third direction (p,rho) -> T\n");
  std::printf("  EquationOfState pure virtuals present on the UNCHANGED tree:\n");
  std::printf("    density(p,T)            YES\n");
  std::printf("    dDensityDPressure(p,T)  YES\n");
  std::printf("    dDensityDTemperature    YES\n");
  std::printf("    pressure(rho,T)         YES   (earlier P17.2)\n");
  std::printf("    temperature(p,rho)      ABSENT  <- the gate criterion is NON-VACUOUS\n");
  std::printf("  Recovering T today requires the CALLER to divide by R itself, i.e. to\n");
  std::printf("  duplicate the EOS relation outside the EOS. Demonstrated:\n");
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    const Real rho = t.density(101325.0, 288.15);
    const Real tHand = 101325.0 / (rho * t.gasConstant());  // what a caller must write today
    std::printf("    %-14s hand-computed T = %-22.17g  (target 288.14999999999998)\n", g.label,
                tHand);
  }

  std::printf("\n### E. speed of sound: sqrt(gamma R T) vs sqrt(gamma p / rho)\n");
  std::printf("%-14s %-16s %-22s %-22s %-24s\n", "gas", "T", "a=sqrt(gamma R T)",
              "sqrt(gamma p/rho)", "relative difference");
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    for (const auto& s : states) {
      const Real rho = t.density(s.p, s.T);
      const Real a1 = t.speedOfSound(s.T);
      const Real a2 = std::sqrt(t.specificHeatRatio() * s.p / rho);
      std::printf("%-14s %-16.6g %-22.17g %-22.17g %-24.17g\n", g.label, s.T, a1, a2,
                  (a2 - a1) / a1);
    }
  }

  std::printf("\n### F. a increases with T, at fixed gamma and R\n");
  {
    const ThermodynamicProperties t(287.05, 1005.0);
    Real prev = -1.0;
    bool monotone = true;
    for (const Real T : {100.0, 200.0, 300.0, 600.0, 1200.0, 2400.0}) {
      const Real a = t.speedOfSound(T);
      if (prev > 0.0 && !(a > prev)) monotone = false;
      std::printf("    T = %-10.6g  a = %-22.17g\n", T, a);
      prev = a;
    }
    std::printf("    strictly increasing: %s\n", monotone ? "yes" : "*** NO ***");
  }

  std::printf("\n### G. determinism of the EXISTING entry points, 64 repeats, bitwise\n");
  {
    const ThermodynamicProperties t(287.05, 1005.0);
    const Real rho0 = t.density(101325.0, 288.15);
    const Real p0 = t.equationOfState().pressure(rho0, 288.15);
    const Real a0 = t.speedOfSound(288.15);
    bool stable = true;
    for (int i = 0; i < 64; ++i) {
      if (t.density(101325.0, 288.15) != rho0) stable = false;
      if (t.equationOfState().pressure(rho0, 288.15) != p0) stable = false;
      if (t.speedOfSound(288.15) != a0) stable = false;
    }
    std::printf("    density / pressure / speedOfSound bitwise stable: %s\n",
                stable ? "yes" : "*** NO ***");
    std::printf("    rho = %.17g   p = %.17g   a = %.17g\n", rho0, p0, a0);
  }

  std::printf("\n### H. existing invalid-state rejection\n");
  {
    const ThermodynamicProperties t(287.05, 1005.0);
    const Real qnan = std::numeric_limits<Real>::quiet_NaN();
    const Real pinf = std::numeric_limits<Real>::infinity();
    struct Case {
      Real p, T;
      const char* label;
    };
    const Case cases[] = {{0.0, 300.0, "p = 0"},        {-1.0, 300.0, "p < 0"},
                          {qnan, 300.0, "p = NaN"},     {pinf, 300.0, "p = +Inf"},
                          {1.0e5, 0.0, "T = 0"},        {1.0e5, -1.0, "T < 0"},
                          {1.0e5, qnan, "T = NaN"},     {1.0e5, pinf, "T = +Inf"}};
    for (const auto& c : cases) {
      std::printf("    density(%-10s) ", c.label);
      try {
        const Real r = t.density(c.p, c.T);
        std::printf("RETURNED %.17g   *** not rejected ***\n", r);
      } catch (const InvalidArgumentError& e) {
        std::printf("THREW: %s\n", e.what());
      }
    }
    for (const Real badR : {0.0, -1.0, qnan, pinf}) {
      std::printf("    IdealGasEOS(R = %-8.4g) ", badR);
      try {
        const IdealGasEOS bad(badR);
        std::printf("CONSTRUCTED   *** not rejected ***\n");
      } catch (const InvalidArgumentError& e) {
        std::printf("THREW: %s\n", e.what());
      }
    }
    for (const Real badCp : {287.05, 100.0, 0.0, -1.0}) {
      std::printf("    ThermodynamicProperties(R=287.05, cp = %-8.4g) ", badCp);
      try {
        const ThermodynamicProperties bad(287.05, badCp);
        std::printf("CONSTRUCTED gamma=%.17g   *** not rejected ***\n", bad.specificHeatRatio());
      } catch (const InvalidArgumentError& e) {
        std::printf("THREW: %s\n", e.what());
      }
    }
  }

  std::printf("\ndone\n");
  return 0;
}
