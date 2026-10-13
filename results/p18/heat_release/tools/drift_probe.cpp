// P18.6 -- INSTRUMENTATION, not production and not a test.
//
// WHY THIS EXISTS. The closed-reactor conservation test refused its own state after ~200 explicit
// steps:
//     "mass fractions at cell 0 sum to 0.99999999999999722; must be within 2.6645352591003757e-15
//      of 1 (deviation 2.7755575615628914e-15)"
// P18.1 enforces |sum Y - 1| <= 4 eps N as a hard PER-STATE invariant. An explicit source step
// rounds each Y_i + increment_i separately, so the sum-to-one error executes a random walk and
// eventually crosses any FIXED tolerance.
//
// THE QUESTION THIS PROBE ANSWERS, before anything is changed: is the drift
//   (a) LINEAR in the step count -- a systematic bias, which would be a defect to fix, or
//   (b) ~SQRT(steps)  -- an irreducible random walk, which is a limitation to state?
//
// It marches on RAW vectors, applying exactly the production arithmetic in exactly the production
// order, so it can continue PAST the point where MassFractionState refuses the state -- which is
// the whole range of interest. Nothing here is production and nothing here validates.
//
// It also measures the ENERGY drift over the same march, because that is what G7 actually gates.
// The two are separate questions and conflating them would hide whichever is healthier.
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

constexpr double kEps = std::numeric_limits<double>::epsilon();
constexpr double kM = 0.0625;        // kg/mol, every species
constexpr double kCp = 1000.0;       // J/(kg K), every species
constexpr double kHfA = 0.0;         // J/kg
constexpr double kHfB = -2000000.0;  // J/kg
constexpr double kDh = -125000.0;    // J/mol, A->B

}  // namespace

int main() {
  std::printf("=== P18.6 drift probe: sum(Y) and energy over an explicit march ===\n");
  std::printf("eps = %.17g\n", kEps);
  std::printf("P18.1 tolerance for 3 species: 4 eps N = %.17g  (= %.1f eps)\n\n",
              4.0 * kEps * 3.0, 4.0 * 3.0);

  const double rho = 1.0;
  const double q = 0.5;
  const double dt = 1.0e-3;
  const double source = -q * kDh;  // S_E, W/m^3

  // the production increments, same expressions, same order
  const double omegaA = kM * (-1.0) * q;
  const double omegaB = kM * (1.0) * q;
  const double incA = (dt * omegaA) / rho;
  const double incB = (dt * omegaB) / rho;

  std::printf("omega_A = %.17g   omega_B = %.17g   sum = %.17g\n", omegaA, omegaB,
              omegaA + omegaB);
  std::printf("increment_A = %.17g   increment_B = %.17g   sum = %.17g\n", incA, incB,
              incA + incB);
  std::printf("S_E = %.17g W/m^3   dt*S_E = %.17g J/m^3\n\n", source, dt * source);

  double ya = 1.0;
  double yb = 0.0;
  double yc = 0.0;
  double t = 1000.0;
  // the conserved total, from the frozen convention
  const double e0 = (rho * kCp * t) + (rho * ((ya * kHfA) + (yb * kHfB)));

  std::printf("%8s  %24s  %12s  %24s  %12s\n", "step", "sum(Y) - 1", "in eps", "energy drift rel",
              "in eps");
  long long firstViolation = -1;
  for (long long step = 1; step <= 100000; ++step) {
    // EXACTLY the production arithmetic: Y_i + dt omega_i / rho, ascending species index
    ya = ya + incA;
    yb = yb + incB;
    yc = yc + 0.0;
    // the sensible pool, then T recovered from it (constant cp, so the inverse is exact here)
    const double sensibleOld = rho * kCp * t;
    const double sensibleNew = sensibleOld + (dt * source);
    t = sensibleNew / (rho * kCp);

    if (ya < 0.0) {
      std::printf("\nspecies A exhausted at step %lld\n", step);
      break;
    }

    const double sumError = (ya + yb + yc) - 1.0;
    const double energy = (rho * kCp * t) + (rho * ((ya * kHfA) + (yb * kHfB)));
    const double energyDrift = std::abs(energy - e0) / std::abs(e0);

    if (firstViolation < 0 && std::abs(sumError) > 4.0 * kEps * 3.0) {
      firstViolation = step;
    }
    const bool report = (step <= 5) || (step % 10000 == 0) || step == 200 || step == 1000 ||
                        step == firstViolation;
    if (report) {
      std::printf("%8lld  %24.17g  %12.2f  %24.6e  %12.2f%s\n", step, sumError,
                  sumError / kEps, energyDrift, energyDrift / kEps,
                  (step == firstViolation) ? "   <-- FIRST sum(Y) VIOLATION" : "");
    }
  }

  std::printf("\nfirst step at which |sum(Y)-1| exceeds 4 eps N : %lld\n", firstViolation);
  std::printf("\n--- is the drift linear or a random walk? ---\n");
  std::printf("re-marching and sampling the drift at decade step counts:\n");
  for (const long long target : {100LL, 400LL, 1600LL, 6400LL, 25600LL}) {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    for (long long step = 0; step < target; ++step) {
      a = a + incA;
      b = b + incB;
      c = c + 0.0;
      if (a < 0.0) {
        break;
      }
    }
    const double err = std::abs((a + b + c) - 1.0) / kEps;
    std::printf("  %8lld steps  |sum(Y)-1| = %8.2f eps   linear would predict %8.2f"
                "   sqrt would predict %8.2f\n",
                target, err, err * static_cast<double>(target) / 100.0,
                std::sqrt(static_cast<double>(target) / 100.0));
  }
  return 0;
}
