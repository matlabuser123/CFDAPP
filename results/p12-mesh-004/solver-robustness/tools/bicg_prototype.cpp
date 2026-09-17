// Prototype of candidate BiCGSTAB breakdown criteria on a dumped system (bicg_capture format).
// Variants: production (library), relative criteria (factor * eps * |x||y|), relative + restart.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"

using namespace cfd;
using algebra::Vector;

struct Loaded {
  algebra::LinearSystem system;
  Vector x0;
};

Loaded load(const std::string& path) {
  std::ifstream in(path);
  std::string line;
  std::stringstream body;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] == '#') continue;
    body << line << "\n";
  }
  std::size_t rows = 0, nnz = 0;
  body >> rows >> nnz;
  std::vector<Index> offsets(rows + 1), cols(nnz);
  std::vector<Real> values(nnz);
  for (auto& o : offsets) body >> o;
  for (auto& c : cols) body >> c;
  for (auto& v : values) body >> v;
  Vector b(rows), x0(rows);
  for (std::size_t i = 0; i < rows; ++i) body >> b[i];
  for (std::size_t i = 0; i < rows; ++i) body >> x0[i];
  return {algebra::LinearSystem(algebra::SparseMatrix(rows, rows, values, cols, offsets), b), x0};
}

struct Outcome {
  std::string status;
  Index iterations{0}, restarts{0};
  Real finalResidual{0.0}, trueResidual{0.0};
};

Outcome candidate(const algebra::LinearSystem& sys, const Vector& x0, Real absTol, Real relTol,
                  Index maxIt, Real factor, bool restart, bool print) {
  const auto& A = sys.matrix();
  const Vector& b = sys.rhs();
  const Index n = sys.size();
  const Real zero = factor * std::numeric_limits<Real>::epsilon();
  Outcome o;
  Vector x = x0;
  Vector r = b - A.multiply(x);
  Vector rHat = r;
  const Real b0 = algebra::l2Norm(r);
  Real rHatNorm = b0;
  Real restartResidual = b0;
  Index sinceRestart = 0;
  const auto converged = [&](Real res) {
    return res <= absTol || (b0 > 0.0 && res / b0 <= relTol);
  };
  const auto finish = [&](const std::string& status, Real res) {
    o.status = status;
    o.finalResidual = res;
    o.trueResidual = algebra::l2Norm(b - A.multiply(x));
    return o;
  };
  if (converged(b0)) return finish("Converged", b0);
  Real rhoOld = 1.0, alpha = 1.0, omega = 1.0;
  Vector v(n, 0.0), p(n, 0.0);
  Real rNorm = b0;
  // Returns true to continue after a restart, false to stop with Breakdown.
  const auto tryRestart = [&](const char* which) {
    if (!restart || sinceRestart == 0 || !(rNorm < restartResidual)) {
      if (print)
        std::printf("   %s breakdown at it %zu: stop (since restart %zu)\n", which,
                    static_cast<std::size_t>(o.iterations), static_cast<std::size_t>(sinceRestart));
      return false;
    }
    r = b - A.multiply(x);
    rNorm = algebra::l2Norm(r);
    rHat = r;
    rHatNorm = rNorm;
    restartResidual = rNorm;
    sinceRestart = 0;
    rhoOld = alpha = omega = 1.0;
    v = Vector(n, 0.0);
    p = Vector(n, 0.0);
    ++o.restarts;
    if (print)
      std::printf("   %s breakdown at it %zu -> restart %zu from true residual %.4e\n", which,
                  static_cast<std::size_t>(o.iterations), static_cast<std::size_t>(o.restarts),
                  rNorm);
    return true;
  };
  while (o.iterations < maxIt) {
    const Index iter = o.iterations + 1;
    const Real rho = algebra::dot(rHat, r);
    if (!std::isfinite(rho) || std::abs(rho) <= zero * rHatNorm * rNorm) {
      if (converged(rNorm)) return finish("Converged", rNorm);
      if (tryRestart("rho")) continue;
      return finish("Breakdown(rho)", rNorm);
    }
    const Real beta = (rho / rhoOld) * (alpha / omega);
    p = r + (p - (v * omega)) * beta;
    v = A.multiply(p);
    const Real rHatDotV = algebra::dot(rHat, v);
    if (!std::isfinite(rHatDotV) || std::abs(rHatDotV) <= zero * rHatNorm * algebra::l2Norm(v)) {
      if (tryRestart("rHat.v")) continue;
      return finish("Breakdown(rHat.v)", rNorm);
    }
    alpha = rho / rHatDotV;
    const Vector s = r - (v * alpha);
    const Real sNorm = algebra::l2Norm(s);
    if (converged(sNorm)) {
      x += p * alpha;
      o.iterations = iter;
      return finish("Converged", sNorm);
    }
    const Vector t = A.multiply(s);
    const Real tDotT = algebra::dot(t, t);
    const Real tDotS = algebra::dot(t, s);
    if (!(tDotT >= std::numeric_limits<Real>::min()) || !std::isfinite(tDotT) ||
        std::abs(tDotS) <= zero * std::sqrt(tDotT) * sNorm) {
      if (tryRestart("omega")) continue;
      return finish("Breakdown(omega)", rNorm);
    }
    omega = tDotS / tDotT;
    x += p * alpha;
    x += s * omega;
    r = s - (t * omega);
    rNorm = algebra::l2Norm(r);
    o.iterations = iter;
    ++sinceRestart;
    if (converged(rNorm)) return finish("Converged", rNorm);
    rhoOld = rho;
  }
  return finish("MaxIterations", rNorm);
}

int main(int argc, char** argv) {
  const Loaded d = load(argv[1]);
  algebra::LinearSolverSettings s;  // production thermal settings: abs 1e-12, rel 1e-10, max 1000
  const auto prod = algebra::BiCGSTAB(s).solve(d.system, d.x0);
  const Real prodTrue = algebra::l2Norm(d.system.rhs() - d.system.matrix().multiply(prod.solution));
  std::printf("%s: %zu unknowns\n", argv[1], static_cast<std::size_t>(d.system.size()));
  std::printf("  production           : status %d, %4zu it, recursive %.4e, true %.4e\n",
              static_cast<int>(prod.status), static_cast<std::size_t>(prod.iterations),
              prod.finalResidual, prodTrue);
  const Real n = static_cast<Real>(d.system.size());
  for (const Real factor : {1.0, n}) {
    for (const bool restart : {false, true}) {
      const Outcome o = candidate(d.system, d.x0, s.absoluteTolerance, s.relativeTolerance,
                                  s.maxIterations, factor, restart, argc > 2);
      std::printf("  relative %-6s %-8s: %-18s %4zu it, %zu restarts, recursive %.4e, true %.4e\n",
                  factor == 1.0 ? "eps" : "n*eps", restart ? "+restart" : "", o.status.c_str(),
                  static_cast<std::size_t>(o.iterations), static_cast<std::size_t>(o.restarts),
                  o.finalResidual, o.trueResidual);
    }
  }
}
