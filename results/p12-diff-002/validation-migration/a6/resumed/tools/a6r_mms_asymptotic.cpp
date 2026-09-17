// RESUMED A6 Steps 2-3: is the U-D upper-band failure a real order > 2, or a pre-asymptotic
// transient read by a three-level Richardson estimator?
//
// Investigation only. No authoritative test, threshold or manufactured field is modified. This probe
// calls the SAME production path the U-D tests call (cfd::test::mmscase::runMomentumLevel) and only
// adds refinement levels beyond the ones the tests use, then reports pairwise AND triplet orders for
// every quantity, including the interior / boundary-ring split.
//
// WHY. The failing criteria are all of the form "finest-triplet observed order in [lo, hi]". The
// triplet estimator is the three-level Richardson form
//
//     p = ln( (e1 - e2) / (e2 - e3) ) / ln(r)
//
// which is exact only when the sequence is already in its asymptotic range. It is a difference of
// differences, so a coarse level carrying an extra, faster-decaying error component inflates it far
// more than it inflates the pairwise estimate  p = ln(e1/e2)/ln(r).
//
// DIFF-002 makes the boundary ring converge at ~3rd order while the interior stays ~2nd. A total
// error of the form  E(h) = A h^2 + B h^3  has a pairwise order that decays from ~3 toward 2 as
// h -> 0, and a triplet estimate that OVERSHOOTS during that decay. The test framework itself
// labels these rows `monotonic_not_asymptotic` while still gating on the triplet value.
//
// The decisive question is therefore whether the orders settle to ~2 once the grid is fine enough.
// That is what this probe measures, by refining further than the tests do.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "MMSCases.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

using namespace cfd;
using cfd::test::mmscase::MomentumOptions;
using cfd::test::mmscase::runMomentumLevel;
using cfd::validation::ErrorNorms;
using cfd::validation::MMSLevel;

namespace {

struct Series {
  std::vector<Real> h;
  std::vector<Real> e;
};

Real pairwise(Real eCoarse, Real eFine, Real rh) {
  return std::log(eCoarse / eFine) / std::log(rh);
}

// The three-level Richardson estimator the U-D gates use.
Real triplet(Real e1, Real e2, Real e3, Real rh) {
  const Real num = e1 - e2;
  const Real den = e2 - e3;
  if (!(den > 0.0) || !(num > 0.0)) return std::nan("");
  return std::log(num / den) / std::log(rh);
}

const ErrorNorms* find(const MMSLevel& level, const std::string& quantity) {
  for (const auto& [name, norms] : level.errors) {
    if (name == quantity) return &norms;
  }
  return nullptr;
}

Real normOf(const ErrorNorms& n, int which) {
  return which == 0 ? n.l1 : (which == 1 ? n.l2 : n.linf);
}

void report(const std::string& quantity, int whichNorm, const std::vector<MMSLevel>& levels) {
  const char* normName = whichNorm == 0 ? "l1" : (whichNorm == 1 ? "l2" : "linf");
  Series s;
  for (const auto& level : levels) {
    const ErrorNorms* n = find(level, quantity);
    if (n == nullptr) return;
    s.h.push_back(level.h);
    s.e.push_back(normOf(*n, whichNorm));
  }
  std::printf("  %-18s %-5s |", quantity.c_str(), normName);
  for (const Real e : s.e) std::printf(" %.4e", e);
  std::printf("\n                           | pairwise p:");
  for (std::size_t k = 0; k + 1 < s.e.size(); ++k) {
    std::printf("        %.3f ", pairwise(s.e[k], s.e[k + 1], s.h[k] / s.h[k + 1]));
  }
  std::printf("\n                           | triplet  p:");
  for (std::size_t k = 0; k + 2 < s.e.size(); ++k) {
    const Real p = triplet(s.e[k], s.e[k + 1], s.e[k + 2], s.h[k] / s.h[k + 1]);
    std::printf("                    %.3f", p);
  }
  std::printf("\n");
}

}  // namespace

int main() {
  // The tests use 16/32/64/128. Add 256 to reach further into the asymptotic range.
  const std::vector<Index> sizes{16, 32, 64, 128, 256};

  for (const auto scheme :
       {discretization::ConvectionScheme::Central, discretization::ConvectionScheme::LinearUpwind}) {
    MomentumOptions options;
    options.scheme = scheme;
    const std::string schemeName = cfd::test::mmscase::schemeName(scheme);

    std::printf("\n================================================================================\n");
    std::printf("## momentum MMS, scheme = %s   (the study behind MomentumMMS.UConverges/VConverges)\n",
                schemeName.c_str());
    std::printf("## grids:");
    for (const Index n : sizes) std::printf(" %lldx%lld", static_cast<long long>(n),
                                            static_cast<long long>(n));
    std::printf("      formal order 2\n");
    std::printf("================================================================================\n");

    std::vector<MMSLevel> levels;
    for (const Index n : sizes) {
      const MMSLevel level = runMomentumLevel(n, options);
      std::printf("  level %-9s cells %6lld h %.6f  %s  iterations %lld\n", level.name.c_str(),
                  static_cast<long long>(level.cells), level.h, level.solverStatus.c_str(),
                  static_cast<long long>(level.iterations));
      if (!level.accepted) {
        std::printf("    *** level rejected: %s\n", level.rejectionReason.c_str());
      }
      levels.push_back(level);
    }
    std::printf("\n  errors and orders (leftmost = coarsest):\n");
    for (const char* q : {"u", "v", "u_interior", "v_interior", "u_boundary_ring",
                          "v_boundary_ring"}) {
      for (int norm = 0; norm < 3; ++norm) report(q, norm, levels);
    }
  }

  std::printf("\n## Reading of the table\n");
  std::printf("## If the pairwise order decays toward 2 as h falls while the coarse triplet sits\n");
  std::printf("## above the band, the failure is a pre-asymptotic transient, not order > 2.\n");
  std::printf("## The boundary ring's own order shows whether DIFF-002's ~3rd-order wall term is\n");
  std::printf("## the faster-decaying component responsible.\n");
  return 0;
}
