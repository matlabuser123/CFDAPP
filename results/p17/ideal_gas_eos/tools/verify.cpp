// P17.2 verification: the identity residuals AFTER the change, at %.17g, including the
// new third direction. The gate and the final report require numerical error values, not
// PASS/FAIL, so this probe measures them and prints the worst case per identity.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "cfd/compressible/IdealGasEOS.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"

using cfd::Real;
using cfd::compressible::IdealGasEOS;
using cfd::compressible::ThermodynamicProperties;

int main() {
  const Real eps = std::numeric_limits<Real>::epsilon();
  std::printf("P17.2 verification -- identity residuals AFTER the change\n");
  std::printf("eps = %.17g\n\n", eps);

  struct Gas {
    Real R, cp;
    const char* label;
  };
  const Gas gases[] = {{287.05, 1005.0, "air"},
                       {2077.10, 5193.0, "helium"},
                       {461.50, 1996.0, "water vapour"}};
  const Real pressures[] = {1.0e3, 101325.0, 5.0e6};
  const Real temperatures[] = {200.0, 288.15, 1500.0};

  Real worstStateRel = 0.0, worstCvAbs = 0.0, worstCvEps = 0.0, worstGamma = 0.0;
  Real worstSoundRel = 0.0, worstTRel = 0.0, worstRtA = 0.0, worstRtB = 0.0, worstRtC = 0.0;
  int states = 0;

  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    const IdealGasEOS& eos = static_cast<const IdealGasEOS&>(t.equationOfState());
    const Real cv = t.specificHeatVolume();
    const Real gamma = t.specificHeatRatio();

    const Real cvAbs = std::abs((t.specificHeatPressure() - cv) - t.gasConstant());
    worstCvAbs = std::max(worstCvAbs, cvAbs);
    worstCvEps = std::max(worstCvEps, cvAbs / (eps * t.gasConstant()));
    worstGamma = std::max(worstGamma, std::abs(gamma - t.specificHeatPressure() / cv));

    for (const Real p : pressures) {
      for (const Real T : temperatures) {
        const Real rho = eos.density(p, T);

        // p = rho R T
        worstStateRel = std::max(worstStateRel, std::abs(p - rho * g.R * T) / p);
        // T = p/(rho R)   -- the new direction
        worstTRel = std::max(worstTRel, std::abs(eos.temperature(p, rho) - T) / T);
        // a = sqrt(gamma R T) vs sqrt(gamma p/rho)
        const Real a1 = t.speedOfSound(T);
        const Real a2 = std::sqrt(gamma * p / rho);
        worstSoundRel = std::max(worstSoundRel, std::abs(a2 - a1) / a1);
        // round trips
        worstRtA = std::max(worstRtA, std::abs(eos.pressure(rho, T) - p) / p);
        worstRtB = std::max(worstRtB, std::abs(eos.density(eos.pressure(rho, T), T) - rho) / rho);
        worstRtC =
            std::max(worstRtC, std::abs(eos.pressure(rho, eos.temperature(p, rho)) - p) / p);
        ++states;
      }
    }
  }

  std::printf("states exercised: %d  (3 gases x 3 pressures x 3 temperatures)\n\n", states);
  std::printf("%-44s %-24s %s\n", "identity", "worst error", "in eps / bound");
  std::printf("%-44s %-24.17g %.2f eps   bound 1e-12\n", "p - rho*R*T            (relative)",
              worstStateRel, worstStateRel / eps);
  std::printf("%-44s %-24.17g %.2f eps   bound 1e-12\n", "temperature(p,rho) - T (relative)",
              worstTRel, worstTRel / eps);
  std::printf("%-44s %-24.17g %.2f eps   bound 4 eps\n", "(cp - cv) - R          (absolute)",
              worstCvAbs, worstCvEps);
  std::printf("%-44s %-24.17g EXACT required\n", "gamma - cp/cv          (absolute)", worstGamma);
  std::printf("%-44s %-24.17g %.2f eps   bound 8 eps\n",
              "sqrt(gamma R T) vs sqrt(gamma p/rho)", worstSoundRel, worstSoundRel / eps);
  std::printf("%-44s %-24.17g %.2f eps   bound 1e-12\n", "round trip (p,T)->rho->p", worstRtA,
              worstRtA / eps);
  std::printf("%-44s %-24.17g %.2f eps   bound 1e-12\n", "round trip (rho,T)->p->rho", worstRtB,
              worstRtB / eps);
  std::printf("%-44s %-24.17g %.2f eps   bound 1e-12\n", "round trip (p,rho)->T->p", worstRtC,
              worstRtC / eps);

  std::printf("\nverdicts\n");
  std::printf("  p = rho R T            %s\n", worstStateRel <= 1e-12 ? "WITHIN 1e-12" : "*** OUT ***");
  std::printf("  T = p/(rho R)          %s\n", worstTRel <= 1e-12 ? "WITHIN 1e-12" : "*** OUT ***");
  std::printf("  cp - cv = R            %s\n",
              worstCvEps <= 4.0 ? "WITHIN 4 eps" : "*** OUT ***");
  std::printf("  gamma = cp/cv          %s\n", worstGamma == 0.0 ? "EXACT" : "*** NOT EXACT ***");
  std::printf("  sound-speed forms      %s\n",
              worstSoundRel <= 8.0 * eps ? "WITHIN 8 eps" : "*** OUT ***");
  std::printf("  all three round trips  %s\n",
              (std::max({worstRtA, worstRtB, worstRtC}) <= 1e-12) ? "WITHIN 1e-12" : "*** OUT ***");
  std::printf("\ndone\n");
  return 0;
}
