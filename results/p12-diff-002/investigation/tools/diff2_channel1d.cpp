// P12-DIFF-002-INV-001: does a second-order wall flux actually improve the PRODUCTION quantities,
// or only an isolated manufactured flux test?
//
// The fully developed region MESH-001 gates on reduces exactly to a 1D discrete diffusion problem:
//     mu u'' = G,  u(0) = u(H) = 0,  G = dp/dx
// Finite volume on ny uniform (or graded) cells, unit face area, cell volume dy_j:
//     interior cell j:  mu (u_{j+1}-u_j)/d_{j+1/2} - mu (u_j-u_{j-1})/d_{j-1/2} = G dy_j
//     wall cell:        mu (u_1-u_0)/d_{1/2} + F_wall = G dy_0
// with F_wall the wall diffusive flux INTO the cell, computed either
//   (A) two-point, as production does:      F = mu (u_b - u_0) / h1
//   (B) three-point one-sided (DIFF-002):   F = -mu a,  a = (phi~_P - phi_b)/h1 - b h1,
//                                           b = [(phi~_F - phi_b)/h2 - (phi~_P - phi_b)/h1]/(h2-h1)
// (In 1D there is no tangential offset, so phi~ = phi.)
//
// The real case fixes the flow rate and lets dp/dx adjust, so the reported dp/dx error is
// |1/Q - 1| where Q is the discrete flow rate obtained with the exact G applied. That is directly
// comparable with MESH-001's measured dp/dx error, and with the test's own closed form for the
// two-point scheme, G_discrete/G_exact = ny^2/(ny^2+2)  =>  error 2/(ny^2+2).
//
// Standalone: no CFDApp dependency, so the discretisation under test is unambiguous.
#include <algorithm>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Real = double;

constexpr Real kHeight = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;
const Real kExactG = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

Real uExact(Real y) {
  return 6.0 * kMeanVelocity * (y / kHeight) * (1.0 - (y / kHeight));
}

// Thomas algorithm for the tridiagonal system.
std::vector<Real> solveTridiagonal(std::vector<Real> a, std::vector<Real> b, std::vector<Real> c,
                                   std::vector<Real> d) {
  const std::size_t n = b.size();
  for (std::size_t i = 1; i < n; ++i) {
    const Real w = a[i] / b[i - 1];
    b[i] -= w * c[i - 1];
    d[i] -= w * d[i - 1];
  }
  std::vector<Real> x(n, 0.0);
  x[n - 1] = d[n - 1] / b[n - 1];
  for (std::size_t i = n - 1; i-- > 0;) x[i] = (d[i] - (c[i] * x[i + 1])) / b[i];
  return x;
}

struct Grid {
  std::vector<Real> centre;  // cell centres
  std::vector<Real> width;   // cell widths
};

// Uniform (beta = 0), or graded by a FIXED smooth mapping y(xi) = H (xi - beta sin(2 pi xi)/(2 pi)),
// which clusters cells at both walls. Refining ny is then a proper refinement of one fixed grading
// -- unlike a fixed geometric ratio, whose total stretch grows without bound under refinement and
// makes the convergence study meaningless (the first version of this probe did that; its output is
// preserved in logs/01 and discussed in summary.md).
Grid makeGrid(std::size_t ny, Real beta) {
  Grid g;
  std::vector<Real> node(ny + 1, 0.0);
  for (std::size_t j = 0; j <= ny; ++j) {
    const Real xi = static_cast<Real>(j) / static_cast<Real>(ny);
    node[j] = kHeight * (xi - (beta * std::sin(2.0 * M_PI * xi) / (2.0 * M_PI)));
  }
  for (std::size_t j = 0; j < ny; ++j) {
    g.centre.push_back(0.5 * (node[j] + node[j + 1]));
    g.width.push_back(node[j + 1] - node[j]);
  }
  return g;
}

// Solves the discrete problem with the chosen wall treatment and the exact G applied.
// threePoint = false -> production's two-point wall flux; true -> the DIFF-002 reconstruction.
std::vector<Real> solveChannel(const Grid& g, bool threePoint) {
  const std::size_t n = g.centre.size();
  std::vector<Real> a(n, 0.0), b(n, 0.0), c(n, 0.0), d(n, 0.0);
  for (std::size_t j = 0; j < n; ++j) d[j] = kExactG * g.width[j];

  for (std::size_t j = 0; j < n; ++j) {
    // Internal faces.
    if (j + 1 < n) {
      const Real dist = g.centre[j + 1] - g.centre[j];
      const Real k = kViscosity / dist;
      b[j] -= k;
      c[j] += k;
    }
    if (j > 0) {
      const Real dist = g.centre[j] - g.centre[j - 1];
      const Real k = kViscosity / dist;
      b[j] -= k;
      a[j] += k;
    }
  }

  // Wall faces: j = 0 (y = 0) and j = n-1 (y = H); the prescribed value is 0 on both.
  for (const bool bottom : {true, false}) {
    const std::size_t p = bottom ? 0 : (n - 1);
    const std::size_t f = bottom ? 1 : (n - 2);
    const Real h1 = bottom ? g.centre[p] : (kHeight - g.centre[p]);
    const Real h2 = bottom ? g.centre[f] : (kHeight - g.centre[f]);
    if (!threePoint || n < 2) {
      // F = mu (0 - u_p) / h1
      b[p] -= kViscosity / h1;
    } else {
      // a = (u_p - 0)/h1 - b h1,  b = [ (u_f - 0)/h2 - (u_p - 0)/h1 ] / (h2 - h1)
      // F = -mu a = -mu [ u_p (1/h1 + h1/(h1(h2-h1))) - u_f h1/(h2(h2-h1)) ]
      //           = -mu [ u_p * cP - u_f * cF ]
      const Real cP = (1.0 / h1) + (h1 / (h1 * (h2 - h1)));
      const Real cF = h1 / (h2 * (h2 - h1));
      b[p] -= kViscosity * cP;
      if (bottom) {
        c[p] += kViscosity * cF;
      } else {
        a[p] += kViscosity * cF;
      }
    }
  }
  return solveTridiagonal(a, b, c, d);
}

struct Result {
  Real dpdxError{0.0};
  Real velocityL2{0.0};
  Real wallFluxError{0.0};
};

Result evaluate(const Grid& g, bool threePoint) {
  const std::vector<Real> u = solveChannel(g, threePoint);
  Result r;
  Real q = 0.0;
  Real e2 = 0.0;
  Real volume = 0.0;
  for (std::size_t j = 0; j < u.size(); ++j) {
    q += u[j] * g.width[j];
    // The case measures the error against the analytic profile, so scale the solution the way the
    // real case does: the flow rate is prescribed, so u is scaled to carry the target rate.
    volume += g.width[j];
  }
  const Real scale = (kMeanVelocity * kHeight) / q;
  for (std::size_t j = 0; j < u.size(); ++j) {
    const Real err = (u[j] * scale) - uExact(g.centre[j]);
    e2 += err * err * g.width[j];
  }
  r.velocityL2 = std::sqrt(e2 / volume);
  r.dpdxError = std::abs((1.0 / q) - 1.0);  // G_measured/G_exact = 1/q with q in units of U H

  // Wall flux of the EXACT field through each treatment, for reference.
  const Real h1 = g.centre[0];
  const Real h2 = g.centre[1];
  const Real exactFlux = -kViscosity * 6.0 * kMeanVelocity / kHeight;
  Real computed = 0.0;
  if (!threePoint) {
    computed = kViscosity * (0.0 - uExact(h1)) / h1;
  } else {
    const Real bb = (((uExact(h2) - 0.0) / h2) - ((uExact(h1) - 0.0) / h1)) / (h2 - h1);
    const Real aa = ((uExact(h1) - 0.0) / h1) - (bb * h1);
    computed = -kViscosity * aa;
  }
  r.wallFluxError = std::abs((computed - exactFlux) / exactFlux);
  return r;
}

void study(Real beta, const char* label) {
  std::printf("# %s\n", label);
  std::vector<Real> twoDp, threeDp, twoL2, threeL2, twoFlux, threeFlux;
  const std::vector<std::size_t> levels{8, 12, 18, 27, 36, 54, 72};
  for (const std::size_t ny : levels) {
    const Grid g = makeGrid(ny, beta);
    const Result two = evaluate(g, false);
    const Result three = evaluate(g, true);
    twoDp.push_back(two.dpdxError);
    threeDp.push_back(three.dpdxError);
    twoL2.push_back(two.velocityL2);
    threeL2.push_back(three.velocityL2);
    twoFlux.push_back(two.wallFluxError);
    threeFlux.push_back(three.wallFluxError);
    std::printf("C   ny %3zu | wall flux: 2pt %.4e 3pt %.4e | dp/dx err: 2pt %.4e (2/(ny^2+2) = "
                "%.4e) 3pt %.4e | velocity L2: 2pt %.4e 3pt %.4e\n",
                ny, two.wallFluxError, three.wallFluxError, two.dpdxError,
                2.0 / ((static_cast<Real>(ny) * static_cast<Real>(ny)) + 2.0), three.dpdxError,
                two.velocityL2, three.velocityL2);
  }
  const auto order = [&](const std::vector<Real>& e, std::size_t k) {
    const Real r = static_cast<Real>(levels[k + 1]) / static_cast<Real>(levels[k]);
    return (e[k] > 0.0 && e[k + 1] > 0.0) ? std::log(e[k] / e[k + 1]) / std::log(r) : 0.0;
  };
  for (std::size_t k = 0; k + 1 < levels.size(); ++k) {
    std::printf("O   %3zu->%3zu | wall flux order: 2pt %6.3f 3pt %6.3f | dp/dx order: 2pt %6.3f "
                "3pt %6.3f | velocity order: 2pt %6.3f 3pt %6.3f\n",
                levels[k], levels[k + 1], order(twoFlux, k), order(threeFlux, k), order(twoDp, k),
                order(threeDp, k), order(twoL2, k), order(threeL2, k));
  }
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002-INV-001: discrete fully developed channel, exact G = %.6f\n",
              kExactG);
  study(0.0, "uniform spacing");
  study(0.5, "graded by a fixed smooth mapping, beta = 0.5 (clustered at both walls)");
  return 0;
}
