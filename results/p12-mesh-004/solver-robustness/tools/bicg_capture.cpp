// P12-MESH-004 solver-robustness: capture the thermal linear system on which the production
// BiCGSTAB reports an unaccepted Breakdown, and trace every breakdown-tested scalar with an
// instrumented, operation-for-operation copy of BiCGSTAB::solve (unpreconditioned, as ThermalSolver
// uses it). Usage: bicg_capture <smooth|rough> <amplitude|fraction> <n> [dump-file]
#include <cmath>
#include <cstdio>
#include <string>

#include "MeshQualityCampaign.hpp"
#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using namespace cfd;
using namespace cfd::test::meshq;
using algebra::Vector;

// Instrumented copy of src/algebra/BiCGSTAB.cpp (same operations, same order, no preconditioner).
struct Trace {
  std::string status;
  Index iterations{0};
  Real finalResidual{0.0};
  std::vector<Real> history;
};

Trace instrumented(const algebra::LinearSystem& system, const Vector& x0, Real absTol, Real relTol,
                   Index maxIt, bool print) {
  const auto& A = system.matrix();
  const Vector& b = system.rhs();
  const Index n = system.size();
  Trace t;
  Vector x = x0;
  Vector r = b - A.multiply(x);
  const Vector rHat = r;
  const Real b0 = algebra::l2Norm(r);
  const Real rHatNorm = algebra::l2Norm(rHat);
  t.history.push_back(b0);
  const auto converged = [&](Real res) {
    return res <= absTol || (b0 > 0.0 && res / b0 <= relTol);
  };
  const Real eps = std::numeric_limits<Real>::epsilon();
  if (print)
    std::printf(
        "#   it |        |r| |       rho |  rho/(|rh||r|) | rh.v/(|rh||v|) |       t.t | "
        "t.s/(|t||s|) |     omega\n");
  Real rhoOld = 1.0, alpha = 1.0, omega = 1.0;
  Vector v(n, 0.0), p(n, 0.0);
  for (Index iter = 1; iter <= maxIt; ++iter) {
    const Real rNorm = algebra::l2Norm(r);
    const Real rho = algebra::dot(rHat, r);
    if (!std::isfinite(rho) || std::abs(rho) < constants::tiny) {
      if (print)
        std::printf(
            "# BREAKDOWN at iteration %zu: |rho| = %.6e < tiny = 1e-30; |r| = %.6e, |rHat| = %.6e, "
            "rho/(|rHat||r|) = %.6e (eps = %.3e)\n",
            static_cast<std::size_t>(iter), std::abs(rho), rNorm, rHatNorm,
            std::abs(rho) / (rHatNorm * rNorm), eps);
      t.status = "Breakdown(rho)";
      t.iterations = iter - 1;
      t.finalResidual = rNorm;
      return t;
    }
    const Real beta = (rho / rhoOld) * (alpha / omega);
    p = r + (p - (v * omega)) * beta;
    Vector pHat = p;
    v = A.multiply(pHat);
    const Real rHatDotV = algebra::dot(rHat, v);
    const Real vNorm = algebra::l2Norm(v);
    if (!std::isfinite(rHatDotV) || std::abs(rHatDotV) < constants::tiny) {
      if (print)
        std::printf("# BREAKDOWN at iteration %zu: |rHat.v| = %.6e < tiny; relative %.6e\n",
                    static_cast<std::size_t>(iter), std::abs(rHatDotV),
                    std::abs(rHatDotV) / (rHatNorm * vNorm));
      t.status = "Breakdown(rHat.v)";
      t.iterations = iter - 1;
      t.finalResidual = rNorm;
      return t;
    }
    alpha = rho / rHatDotV;
    const Vector s = r - (v * alpha);
    const Real sNorm = algebra::l2Norm(s);
    if (converged(sNorm)) {
      t.status = "Converged(s)";
      t.iterations = iter;
      t.finalResidual = sNorm;
      t.history.push_back(sNorm);
      return t;
    }
    Vector sHat = s;
    const Vector tv = A.multiply(sHat);
    const Real tDotT = algebra::dot(tv, tv);
    const Real tDotS = algebra::dot(tv, s);
    if (!std::isfinite(tDotT) || tDotT < constants::tiny) {
      if (print)
        std::printf("# BREAKDOWN at iteration %zu: t.t = %.6e < tiny\n",
                    static_cast<std::size_t>(iter), tDotT);
      t.status = "Breakdown(t.t)";
      t.iterations = iter - 1;
      t.finalResidual = rNorm;
      return t;
    }
    omega = tDotS / tDotT;
    if (print)
      std::printf("# %4zu | %.4e | %.3e | %14.6e | %14.6e | %.3e | %12.5e | %.4e\n",
                  static_cast<std::size_t>(iter), rNorm, rho, std::abs(rho) / (rHatNorm * rNorm),
                  std::abs(rHatDotV) / (rHatNorm * vNorm), tDotT,
                  std::abs(tDotS) / (std::sqrt(tDotT) * sNorm), omega);
    if (!std::isfinite(omega) || std::abs(omega) < constants::tiny) {
      t.status = "Breakdown(omega)";
      t.iterations = iter - 1;
      t.finalResidual = rNorm;
      return t;
    }
    x += pHat * alpha;
    x += sHat * omega;
    r = s - (tv * omega);
    const Real residualNorm = algebra::l2Norm(r);
    t.history.push_back(residualNorm);
    t.iterations = iter;
    if (converged(residualNorm)) {
      t.status = "Converged";
      t.finalResidual = residualNorm;
      return t;
    }
    rhoOld = rho;
  }
  t.status = "MaxIterations";
  t.finalResidual = t.history.back();
  return t;
}

void dump(const std::string& path, const algebra::LinearSystem& system, const Vector& x0,
          const std::string& provenance) {
  const auto& A = system.matrix();
  FILE* f = std::fopen(path.c_str(), "w");
  std::fprintf(f, "# %s\n", provenance.c_str());
  std::fprintf(f,
               "# format: rows nonzeros / row offsets (rows+1) / column indices / values / rhs / "
               "initial guess;"
               " reals as %%.17g (exact round trip)\n");
  std::fprintf(f, "%zu %zu\n", static_cast<std::size_t>(A.rows()),
               static_cast<std::size_t>(A.nonZeros()));
  for (Index i = 0; i <= A.rows(); ++i)
    std::fprintf(f, "%zu\n", static_cast<std::size_t>(A.rowOffsetsData()[i]));
  for (Index k = 0; k < A.nonZeros(); ++k)
    std::fprintf(f, "%zu\n", static_cast<std::size_t>(A.columnIndicesData()[k]));
  for (Index k = 0; k < A.nonZeros(); ++k) std::fprintf(f, "%.17g\n", A.valuesData()[k]);
  for (Index i = 0; i < A.rows(); ++i) std::fprintf(f, "%.17g\n", system.rhs()[i]);
  for (Index i = 0; i < A.rows(); ++i) std::fprintf(f, "%.17g\n", x0[i]);
  std::fclose(f);
}

int main(int argc, char** argv) {
  const std::string family = argv[1];
  const Real parameter = std::stod(argv[2]);
  const Index n = std::stoul(argv[3]);
  const std::string dumpPath = argc > 4 ? argv[4] : "";
  const io::MeshConfig mesh =
      family == "smooth" ? smoothDistortedMesh(n, parameter) : roughDistortedMesh(n, parameter);
  const auto def = caseDefinition(mesh, Discretization{});
  const std::string dir = "/tmp/m4probe/capture_" + family + "_" + argv[2] + "_" + argv[3];
  io::CaseWriter::write(dir, def);
  const auto setup = io::CaseBuilder{}.build(io::CaseReader{}.read(dir));
  fields::VectorField force(setup.mesh.numberOfCells());
  fields::ScalarField heat(setup.mesh.numberOfCells());
  for (const auto& c : setup.mesh.cells()) {
    force[c.id()] = momentumForcing(c.centroid());
    heat[c.id()] = heatSource(c.centroid());
  }
  pressure_velocity::SIMPLE simple(setup.solverSettings, 0);
  simple.setMomentumSource(&force);
  const auto flow =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);
  std::printf("# %s %g n=%zu: SIMPLE %s after %zu iterations\n", family.c_str(), parameter,
              static_cast<std::size_t>(n), flow.converged() ? "Converged" : "NOT converged",
              static_cast<std::size_t>(flow.iterations));
  thermal::ThermalSolverSettings ts;
  ts.nonOrthogonal = pressure_velocity::nonOrthogonalOptions(setup.solverSettings);
  const auto& ls = ts.linearSolver;
  algebra::BiCGSTAB solver(ls);
  fields::ScalarField T = *setup.initialTemperature;
  Real reference = 0.0;
  for (Index outer = 0; outer < 60; ++outer) {
    const auto a =
        thermal::assembleEnergyEquation(setup.mesh, T, flow.massFlux, *setup.thermal,
                                        *setup.temperatureBoundaries, heat, ts.nonOrthogonal);
    Vector x0(T.size());
    for (Index i = 0; i < T.size(); ++i) x0[i] = T[i];
    const auto s = solver.solve(a.system, x0);
    const Trace copy = instrumented(a.system, x0, ls.absoluteTolerance, ls.relativeTolerance,
                                    ls.maxIterations, false);
    bool identical = copy.history.size() == s.residualHistory.size() &&
                     copy.iterations == s.iterations && copy.finalResidual == s.finalResidual;
    for (std::size_t k = 0; identical && k < copy.history.size(); ++k)
      identical = copy.history[k] == s.residualHistory[k];
    if (outer == 0) reference = s.initialResidual;
    const bool breakdown = s.status == algebra::SolverStatus::Breakdown;
    const bool attained = outer > 0 && breakdown &&
                          (s.finalResidual <= ls.absoluteTolerance ||
                           s.finalResidual <= ls.relativeTolerance * reference);
    Real change = 0.0;
    for (Index i = 0; i < T.size(); ++i) change = std::max(change, std::abs(s.solution[i] - T[i]));
    std::printf(
        "outer %2zu: status %d (%s), %4zu it, %.4e -> %.4e | instrumented copy %s (%s) | max dT "
        "%.3e%s\n",
        static_cast<std::size_t>(outer + 1), static_cast<int>(s.status), copy.status.c_str(),
        static_cast<std::size_t>(s.iterations), s.initialResidual, s.finalResidual,
        identical ? "bit-identical" : "DIFFERS", copy.status.c_str(), change,
        breakdown ? (attained ? "  [breakdown ACCEPTED by runPicardLoop]"
                              : "  [breakdown -> LinearSolveFailure]")
                  : "");
    if (breakdown && !attained) {
      std::printf(
          "# failing system: %zu unknowns, %zu nonzeros; initial residual %.6e; linear settings "
          "abs %.0e rel %.0e max %zu\n",
          static_cast<std::size_t>(a.system.size()),
          static_cast<std::size_t>(a.system.matrix().nonZeros()), s.initialResidual,
          ls.absoluteTolerance, ls.relativeTolerance, static_cast<std::size_t>(ls.maxIterations));
      (void)instrumented(a.system, x0, ls.absoluteTolerance, ls.relativeTolerance, ls.maxIterations,
                         true);
      if (!dumpPath.empty()) {
        char provenance[512];
        std::snprintf(
            provenance, sizeof(provenance),
            "P12-MESH-004 thermal energy system, %s mesh parameter %g, %zux%zu, outer iteration "
            "%zu of the "
            "production Picard loop (campaign case, SIMPLE tol 1e-7, relaxation 0.8/0.4); "
            "production BiCGSTAB "
            "(abs 1e-12, rel 1e-10) reports Breakdown after %zu iterations at residual %.6e",
            family.c_str(), parameter, static_cast<std::size_t>(n), static_cast<std::size_t>(n),
            static_cast<std::size_t>(outer + 1), static_cast<std::size_t>(s.iterations),
            s.finalResidual);
        dump(dumpPath, a.system, x0, provenance);
        std::printf("# dumped to %s\n", dumpPath.c_str());
      }
      return 0;
    }
    for (Index i = 0; i < T.size(); ++i) T[i] = s.solution[i];
    if (change < ts.tolerance) {
      std::printf("# thermal converged at outer %zu\n", static_cast<std::size_t>(outer + 1));
      return 0;
    }
  }
  return 0;
}
