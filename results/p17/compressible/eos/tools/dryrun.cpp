// P17.2 dry-run: the UNCHANGED tree's EOS reference values, recorded before the
// interface change so P4/P5 (density and derivatives bit-for-bit unchanged) can be
// checked against measurements rather than against memory.
//
// Printed with %.17g -- enough digits to round-trip a double exactly, so "bit-for-bit
// unchanged" is a string comparison afterwards rather than a tolerance judgement.
#include <cstdio>

#include "cfd/compressible/IdealGasEOS.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"

int main() {
  // Representative non-default states. Air-like R, a non-air R, and pressures and
  // temperatures away from any plausible default so a hard-coded return cannot
  // reproduce them.
  struct State {
    double R, p, T;
    const char* label;
  };
  const State states[] = {
      {287.05, 101325.0, 288.15, "air, sea-level standard"},
      {287.05, 250000.0, 450.00, "air, compressed and hot"},
      {2077.10, 50000.0, 120.00, "helium, low p, cryogenic"},
      {461.50, 611.657, 273.16, "water vapour, triple point"},
      {287.05, 1.0e-3, 1.0e-3, "extreme small p and T"},
  };

  std::printf("P17.2 dry-run -- EOS reference values on the UNCHANGED tree\n\n");
  std::printf("%-34s %-14s %-14s %-14s %-24s %-24s %-24s\n", "state", "R", "p", "T",
              "density(p,T)", "dRho/dp", "dRho/dT");
  for (const auto& s : states) {
    const cfd::compressible::IdealGasEOS eos(s.R);
    std::printf("%-34s %-14.6g %-14.6g %-14.6g %-24.17g %-24.17g %-24.17g\n", s.label, s.R, s.p,
                s.T, eos.density(s.p, s.T), eos.dDensityDPressure(s.p, s.T),
                eos.dDensityDTemperature(s.p, s.T));
  }

  std::printf("\nThermodynamicProperties invariants (derived, not stored):\n");
  const struct {
    double R, cp;
    const char* label;
  } props[] = {{287.05, 1004.675, "air"}, {2077.10, 5193.0, "helium"}};
  for (const auto& p : props) {
    const cfd::compressible::ThermodynamicProperties tp(p.R, p.cp);
    std::printf("  %-10s R=%-12.6g cp=%-12.6g cv=%-14.17g gamma=%-20.17g cp-cv-R=%.17g\n", p.label,
                tp.gasConstant(), tp.specificHeatPressure(), tp.specificHeatVolume(),
                tp.specificHeatRatio(),
                tp.specificHeatPressure() - tp.specificHeatVolume() - tp.gasConstant());
    std::printf("  %-10s speedOfSound(288.15)=%.17g  speedOfSound(450)=%.17g\n", "",
                tp.speedOfSound(288.15), tp.speedOfSound(450.0));
  }

  std::printf("\npressure(rho,T) on EquationOfState: ABSENT on this tree "
              "(this file would not compile if it called it)\n");
  return 0;
}
