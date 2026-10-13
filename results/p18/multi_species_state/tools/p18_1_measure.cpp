// P18.1 -- INSTRUMENTATION, not production and not a test.
//
// Section 37 asks for measured values rather than PASS/FAIL. This walks a
// ladder of compositions past MassFractionState and reports what was
// actually accepted and rejected, so the summary quotes numbers that came
// out of the code rather than numbers a human believed.
//
// It implements no physics and is not linked into cfdcore.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/SpeciesRegistry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::species::MassFractionState;
using cfd::species::massFractionSumTolerance;
using cfd::species::normalizeMassFractions;
using cfd::species::SpeciesRegistry;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

int acceptedSum = 0;
int rejectedSum = 0;
int rejectedBounds = 0;
int rejectedNonFinite = 0;
Real maxAcceptedDeviation = 0.0;
Real minRejectedDeviation = std::numeric_limits<Real>::infinity();
Real largestAccepted = 0.0;
Real smallestAccepted = std::numeric_limits<Real>::infinity();

// Classifies by the message, so a rejection is attributed to the check that
// actually fired rather than to the one we assumed would.
void offer(const SpeciesRegistry& registry, const std::vector<Real>& composition) {
  Real sum = 0.0;
  for (Real v : composition) {
    sum += v;
  }
  const Real deviation = std::abs(sum - 1.0);
  try {
    const MassFractionState state(registry, 1, composition);
    ++acceptedSum;
    if (deviation > maxAcceptedDeviation) {
      maxAcceptedDeviation = deviation;
    }
    for (Real v : composition) {
      if (v > largestAccepted) {
        largestAccepted = v;
      }
      if (v < smallestAccepted) {
        smallestAccepted = v;
      }
    }
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    if (message.find("not finite") != std::string::npos) {
      ++rejectedNonFinite;
    } else if (message.find("must be in [0, 1]") != std::string::npos) {
      ++rejectedBounds;
    } else if (message.find("sum to") != std::string::npos) {
      ++rejectedSum;
      if (deviation < minRejectedDeviation) {
        minRejectedDeviation = deviation;
      }
    }
  }
}

}  // namespace

int main() {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();

  std::printf("eps                        %.17g\n", kEps);
  std::printf("tolerance N=1  (4*eps*1)   %.17g\n", massFractionSumTolerance(1));
  std::printf("tolerance N=2  (4*eps*2)   %.17g\n", massFractionSumTolerance(2));
  std::printf("tolerance N=3  (4*eps*3)   %.17g\n", massFractionSumTolerance(3));
  std::printf("tolerance N=64 (4*eps*64)  %.17g\n", massFractionSumTolerance(64));
  std::printf("\n");

  const SpeciesRegistry binary({"O2", "N2"});
  const SpeciesRegistry ternary({"CH4", "O2", "N2"});

  // --- a ULP ladder across the frozen bound, both directions ---
  std::printf("--- binary ULP ladder about the N=2 bound (8*eps = %.17g) ---\n",
              massFractionSumTolerance(2));
  for (int k = 0; k <= 20; ++k) {
    const Real perturbed = 0.5 + static_cast<Real>(k) * kEps;
    const std::vector<Real> composition{perturbed, 0.5};
    Real sum = perturbed + 0.5;
    const Real deviation = std::abs(sum - 1.0);
    bool accepted = true;
    try {
      const MassFractionState state(binary, 1, composition);
    } catch (const InvalidArgumentError&) {
      accepted = false;
    }
    if (k <= 6 || k >= 16) {
      std::printf("  %2d eps  deviation %.17g  %s\n", k, deviation,
                  accepted ? "ACCEPTED" : "REJECTED");
    }
    offer(binary, composition);
  }
  std::printf("\n");

  // --- valid compositions ---
  offer(SpeciesRegistry({"N2"}), {1.0});
  offer(binary, {0.0, 1.0});
  offer(binary, {1.0, 0.0});
  offer(binary, {0.5, 0.5});
  offer(binary, {0.25, 0.75});
  offer(binary, {1.0e-300, 1.0 - 1.0e-300});
  offer(ternary, {0.03, 0.282, 0.688});
  offer(ternary, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
  offer(ternary, {0.5, 0.25, 0.25});
  offer(ternary, {0.0, 0.3, 0.7});

  // --- invalid: bounds (each has an exact sum of 1, so only bounds can fire) ---
  offer(binary, {-0.25, 1.25});
  offer(binary, {-1.0e-12, 1.0 + 1.0e-12});
  offer(binary, {1.25, -0.25});
  offer(binary, {2.0, -1.0});

  // --- invalid: non-finite ---
  offer(binary, {nan, 1.0});
  offer(binary, {inf, 1.0});
  offer(binary, {-inf, 1.0});
  offer(ternary, {nan, 0.5, 0.5});

  // --- invalid: sum ---
  offer(binary, {0.5, 0.51});
  offer(binary, {0.5, 0.49});
  offer(binary, {0.8, 0.8});
  offer(binary, {0.0, 0.0});
  offer(ternary, {0.3, 0.3, 0.3});

  std::printf("--- tallies over the ladder and the explicit cases ---\n");
  std::printf("accepted                       %d\n", acceptedSum);
  std::printf("rejected: sum                  %d\n", rejectedSum);
  std::printf("rejected: bounds               %d\n", rejectedBounds);
  std::printf("rejected: non-finite           %d\n", rejectedNonFinite);
  std::printf("max deviation ACCEPTED         %.17g  (%.3f eps)\n", maxAcceptedDeviation,
              maxAcceptedDeviation / kEps);
  std::printf("min deviation REJECTED         %.17g  (%.3f eps)\n", minRejectedDeviation,
              minRejectedDeviation / kEps);
  std::printf("largest Y_i accepted           %.17g\n", largestAccepted);
  std::printf("smallest Y_i accepted          %.17g\n", smallestAccepted);
  std::printf("\n");

  // --- per-cell detection on a large field ---
  const Index cells = 100000;
  ScalarField a(cells, 0.5);
  ScalarField b(cells, 0.5);
  b[99999] = 0.5 + 1.0e-9;
  std::string where = "NOT DETECTED";
  try {
    const MassFractionState state(binary, {a, b});
  } catch (const InvalidArgumentError& error) {
    where = error.what();
  }
  std::printf("--- one bad cell in %zu, global min/max of each field unchanged ---\n",
              static_cast<std::size_t>(cells));
  std::printf("  %s\n", where.c_str());
  std::printf("\n");

  // --- normalisation is explicit ---
  const std::vector<Real> normalized = normalizeMassFractions({0.8, 0.8});
  std::printf("--- normalisation ---\n");
  std::printf("  normalizeMassFractions({0.8, 0.8}) = {%.17g, %.17g}\n", normalized[0],
              normalized[1]);
  bool constructionNormalised = false;
  try {
    const MassFractionState state(binary, 1, {0.8, 0.8});
    constructionNormalised = true;
  } catch (const InvalidArgumentError&) {
  }
  std::printf("  construction from {0.8, 0.8} silently normalised: %s\n",
              constructionNormalised ? "YES -- DEFECT" : "NO (rejected)");
  return 0;
}
