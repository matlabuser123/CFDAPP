// Diagnose why P17.2 control N6 was vacuous, and derive a replacement from the bound
// rather than from the observed result.
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
  const double eps = std::numeric_limits<double>::epsilon();
  const double R = 287.05;
  const double cp = 1005.0;
  const double cvTrue = cp - R;

  std::printf("eps        = %.17g\n", eps);
  std::printf("R          = %.17g\n", R);
  std::printf("cp         = %.17g\n", cp);
  std::printf("cv = cp-R  = %.17g\n\n", cvTrue);

  // What N6 actually did.
  const double cvN6 = std::round(cvTrue * 1.0e10) / 1.0e10;
  std::printf("N6: round(cv*1e10)/1e10 = %.17g\n", cvN6);
  std::printf("    identical to cv?      %s   <- THIS is why N6 was vacuous\n",
              cvN6 == cvTrue ? "YES, bit for bit" : "no");
  std::printf("    cv*1e10               = %.17g\n", cvTrue * 1.0e10);
  std::printf("    round(...)            = %.17g\n", std::round(cvTrue * 1.0e10));
  std::printf("    /1e10                 = %.17g\n\n", std::round(cvTrue * 1.0e10) / 1.0e10);

  // The bound T1 is gated at.
  const double bound = 4.0 * eps * R;
  std::printf("T1 bound = 4*eps*R      = %.17g  (absolute, on (cp-cv)-R)\n", bound);
  std::printf("residual with true cv   = %.17g\n", (cp - cvTrue) - R);

  // Derive the minimum relative perturbation of cv that must exceed the bound.
  // Perturbing cv by relative delta shifts (cp-cv)-R by delta*cv.
  const double deltaMin = bound / cvTrue;
  std::printf("\nminimum relative perturbation of cv to exceed the bound:\n");
  std::printf("  delta_min = bound/cv  = %.17g  = %.2f eps\n", deltaMin, deltaMin / eps);

  // The replacement: a relative perturbation with a large, stated margin.
  const double delta = 1.0e-12;
  const double cvNew = cvTrue * (1.0 + delta);
  std::printf("\nreplacement N6b: cv *= (1 + %.1g)\n", delta);
  std::printf("  cv perturbed          = %.17g\n", cvNew);
  std::printf("  residual (cp-cv)-R    = %.17g\n", (cp - cvNew) - R);
  std::printf("  bound                 = %.17g\n", bound);
  std::printf("  exceeds bound by      = %.1fx\n", std::fabs((cp - cvNew) - R) / bound);
  std::printf("  margin over delta_min = %.1fx\n", delta / deltaMin);

  // And gamma must STILL be exact, because it is stored as cp/cv of the perturbed cv.
  const double gammaNew = cp / cvNew;
  std::printf("\n  gamma stored = cp/cv_perturbed = %.17g\n", gammaNew);
  std::printf("  gamma - cp/cv_perturbed        = %.17g  <- must stay 0 (T2 still passes)\n",
              gammaNew - cp / cvNew);
  std::printf("  gamma > 1?                     %s\n", gammaNew > 1.0 ? "yes" : "no");
  return 0;
}
