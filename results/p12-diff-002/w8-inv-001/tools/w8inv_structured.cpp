// P12-DIFF-002-W8-INV-001 steps 3, 4, 6, 7: extended refinement of the DISTORTED POISEUILLE case
// (W8 test 1), with every raw quantity recorded BEFORE any order is computed.
//
// Investigation only: no production file, no test, no threshold, no reference, no extraction and
// no mesh family is modified. The mapping, the case constants, the solver settings and the
// extraction below are copied verbatim from
// tests/integration/case/test_structured_quad_production_case.cpp (frozen hash d20c8443...) and
// cases/poiseuille_distorted/solver.json.
//
// Two refinement families are run:
//   A: the test's own r = 1.5 family, EXTENDED   64x8, 96x12, 144x18, 216x27
//   B: an independent r = 2 family                64x8, 128x16, 256x32
// Family B exists because an observed order that is real must not depend on the refinement
// ratio; one that is an artifact of where the grids fall usually does.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kDevelopedStart = 0.50 * kLength;
constexpr Real kDevelopedEnd = 0.85 * kLength;
const Real kPi = std::acos(-1.0);
const Real kExactPressureGradient = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

// VERBATIM from the test: the case's documented mapping.
std::vector<Vector2> mappedVertices(Index nx, Index ny, Real length, Real height, Real ax, Real ay,
                                    Real lambda) {
  std::vector<Vector2> vertices;
  vertices.reserve(static_cast<std::size_t>((nx + 1) * (ny + 1)));
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = length * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = height * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / length) * std::sin(2.0 * kPi * eta / height));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / height));
      if (i == 0) x = 0.0;
      if (i == nx) x = length;
      if (j == 0) y = 0.0;
      if (j == ny) y = height;
      vertices.push_back(Vector2{x, y});
    }
  }
  return vertices;
}
std::vector<Vector2> poiseuilleVertices(Index nx, Index ny) {
  return mappedVertices(nx, ny, kLength, kHeight, 0.1, 0.05, 1.0);
}

// VERBATIM from cases/poiseuille_distorted/solver.json.
pressure_velocity::SIMPLESettings settings() {
  pressure_velocity::SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.convectionScheme = discretization::ConvectionScheme::LinearUpwind;
  s.gradientScheme = discretization::GradientScheme::GreenGauss;
  s.nonOrthogonalCorrections = 1;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  return s;
}

struct Row {
  Index nx{0}, ny{0};
  bool converged{false};
  Index iterations{0};
  Real velocityL2{0.0};
  Real dpdx{0.0};
  Real dpdxSignedError{0.0};
  Real maxNonOrtho{0.0}, meanNonOrtho{0.0}, maxSkew{0.0}, meanSkew{0.0};
  Real maxAspect{0.0};
  Real minWallSpacing{0.0};
  Real h{0.0};
  Real seconds{0.0};
  // Extraction-validity diagnostics (step 7): which cells the window actually selects.
  Index windowCells{0};
  Real windowXmin{0.0}, windowXmax{0.0};
  std::string status;
  Real finalU{0.0}, finalP{0.0}, finalCont{0.0};
};

// forceMetrics: extract the metrics even when the solve did not meet its tolerance, so the field
// at the committed gate can be compared against the field much further along the same iteration
// history. Used ONLY by the plateau diagnostic; never proposed as test behaviour.
Row run(Index nx, Index ny, Real tolMultiplier = 1.0, Index maxIters = 0,
        bool forceMetrics = false) {
  Row r;
  r.nx = nx; r.ny = ny;
  r.h = kHeight / static_cast<Real>(ny);
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(nx, ny, poiseuilleVertices(nx, ny));
  const auto q = mesh::MeshQuality::evaluate(mesh);
  r.maxNonOrtho = q.maxNonOrthogonalityDegrees;
  r.meanNonOrtho = q.meanNonOrthogonalityDegrees;
  r.maxSkew = q.maxSkewness;
  r.meanSkew = q.meanSkewness;
  r.maxAspect = q.maximumAspectRatio;

  boundary::BoundaryConditionSet vel;
  vel.set(mesh, "left", std::make_unique<boundary::Inlet>(Vector2{kMeanVelocity, 0.0}));
  vel.set(mesh, "right", std::make_unique<boundary::Outlet>());
  vel.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  vel.set(mesh, "top", std::make_unique<boundary::Wall>());
  boundary::BoundaryConditionSet pres;
  pres.set(mesh, "left", std::make_unique<boundary::FixedGradient>(0.0));
  pres.set(mesh, "right", std::make_unique<boundary::FixedValue>(0.0));
  pres.set(mesh, "bottom", std::make_unique<boundary::FixedGradient>(0.0));
  pres.set(mesh, "top", std::make_unique<boundary::FixedGradient>(0.0));

  auto s = settings();
  s.velocityTolerance *= tolMultiplier;
  s.pressureTolerance *= tolMultiplier;
  s.continuityTolerance *= tolMultiplier;
  if (maxIters > 0) s.maxIterations = maxIters;
  const pressure_velocity::SIMPLE simple(s, 0);
  const auto t0 = std::chrono::steady_clock::now();
  const auto res = simple.solve(mesh, physics::FluidProperties(1.0, kViscosity), vel, pres,
                                fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                                fields::ScalarField(mesh.numberOfCells(), 0.0));
  r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  r.converged = res.converged();
  r.iterations = res.iterations;
  switch (res.status) {
    case pressure_velocity::SIMPLEStatus::Converged: r.status = "Converged"; break;
    case pressure_velocity::SIMPLEStatus::MaxIterations: r.status = "MaxIters"; break;
    case pressure_velocity::SIMPLEStatus::Diverging: r.status = "Diverging"; break;
    default: r.status = "Other"; break;
  }
  r.finalU = res.finalUResidual;
  r.finalP = res.finalPressureResidual;
  r.finalCont = res.finalContinuityResidual;
  if (!r.converged && !forceMetrics) return r;

  // VERBATIM extraction from the test's poiseuilleMetrics.
  Real errorSquared = 0.0, volume = 0.0;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  bool first = true;
  for (const auto& cell : mesh.cells()) {
    const Vector2& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Real exact = 6.0 * kMeanVelocity * (c.y / kHeight) * (1.0 - (c.y / kHeight));
    const Vector2 error = res.velocity[cell.id()] - Vector2{exact, 0.0};
    const Real v = cell.volume();
    errorSquared += dot(error, error) * v;
    volume += v;
    const Real p = res.pressure[cell.id()];
    sw += v; sx += v * c.x; sxx += v * c.x * c.x; sp += v * p; sxp += v * c.x * p;
    ++r.windowCells;
    if (first) { r.windowXmin = c.x; r.windowXmax = c.x; first = false; }
    r.windowXmin = std::min(r.windowXmin, c.x);
    r.windowXmax = std::max(r.windowXmax, c.x);
  }
  r.velocityL2 = std::sqrt(errorSquared / volume);
  r.dpdx = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  r.dpdxSignedError = r.dpdx - kExactPressureGradient;

  // Wall-normal spacing of the first cell (step 6 diagnostic).
  r.minWallSpacing = 1e30;
  for (const Index f : mesh.boundaryPatch("bottom").faceIds()) {
    const auto& face = mesh.face(f);
    r.minWallSpacing = std::min(
        r.minWallSpacing, MeshGeometry::distance(mesh.cell(face.owner()).centroid(),
                                                 face.centroid()));
  }
  return r;
}

void printRows(const char* title, const std::vector<Row>& rows) {
  std::printf("\n%s\n", title);
  std::printf("  %4s %4s %5s %7s %14s %16s %16s %9s\n", "nx", "ny", "conv", "iters",
              "velocity L2", "dp/dx", "dp/dx SIGNED err", "time s");
  for (const auto& r : rows) {
    std::printf("  %4lld %4lld %5s %7lld %14.8e %16.10f %+16.8e %9.1f\n",
                (long long)r.nx, (long long)r.ny, r.converged ? "yes" : "NO",
                (long long)r.iterations, r.velocityL2, r.dpdx, r.dpdxSignedError, r.seconds);
  }
  std::printf("  %4s %4s %10s %10s %9s %9s %9s %9s %7s %10s\n", "nx", "ny", "maxNonOrt",
              "meanNonOrt", "maxSkew", "meanSkew", "maxAspect", "wallDist", "cells", "x-window");
  for (const auto& r : rows) {
    std::printf("  %4lld %4lld %10.4f %10.4f %9.5f %9.5f %9.4f %9.6f %7lld %5.3f..%5.3f\n",
                (long long)r.nx, (long long)r.ny, r.maxNonOrtho, r.meanNonOrtho, r.maxSkew,
                r.meanSkew, r.maxAspect, r.minWallSpacing, (long long)r.windowCells,
                r.windowXmin, r.windowXmax);
  }
}

void orders(const char* label, const std::vector<Row>& rows, Real ratio, bool useAbsDpdx) {
  std::printf("\n  %s (r = %.3f)\n", label, ratio);
  for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
    const Real vOrder =
        std::log(rows[k].velocityL2 / rows[k + 1].velocityL2) / std::log(ratio);
    const Real e0 = std::abs(rows[k].dpdxSignedError);
    const Real e1 = std::abs(rows[k + 1].dpdxSignedError);
    const Real gOrder = std::log(e0 / e1) / std::log(ratio);
    const bool crossing = (rows[k].dpdxSignedError * rows[k + 1].dpdxSignedError) < 0.0;
    std::printf("    pair %zu (%lldx%lld -> %lldx%lld): velocity %7.4f | dp/dx |e| %7.4f%s\n",
                k, (long long)rows[k].nx, (long long)rows[k].ny, (long long)rows[k + 1].nx,
                (long long)rows[k + 1].ny, vOrder, gOrder,
                crossing ? "   <-- SIGN CHANGE: |e| order is not defined here" : "");
    (void)useAbsDpdx;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = (argc > 1 && std::string(argv[1]) == "quick");

  // ---- TOLERANCE-CONTAMINATION DIAGNOSTIC (step 3) -------------------------------------------
  // The committed case stops at velocityTolerance 2e-5 and maxIterations 3000, and the finest
  // converging grid (144x18) consumes 2794 of those 3000. If the recorded errors are partly
  // ITERATIVE rather than DISCRETISATION errors, their observed order is not a discretisation
  // order at all. This re-runs the same three grids with the committed tolerances and with 100x
  // tighter ones, both at maxIterations 40000, and reports the change. Diagnosis only: nothing
  // here changes the test, and the tightened numbers are never proposed as the test's settings.
  if (argc > 1 && std::string(argv[1]) == "tol") {
    std::printf("# W8-INV-001 TOLERANCE-CONTAMINATION DIAGNOSTIC\n");
    std::printf("# committed tolerances (u 2e-5) vs 100x tighter (u 2e-7); maxIterations 40000.\n\n");
    std::vector<Row> base, tight;
    for (auto [nx, ny] : std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}}) {
      base.push_back(run(nx, ny, 1.0, 40000));
      tight.push_back(run(nx, ny, 0.01, 40000));
    }
    printRows("COMMITTED tolerances, maxIterations 40000", base);
    orders("COMMITTED tolerances", base, 1.5, true);
    printRows("100x TIGHTER tolerances, maxIterations 40000", tight);
    orders("100x TIGHTER tolerances", tight, 1.5, true);
    std::printf("\n  velocity L2: committed -> tighter (relative change)\n");
    for (std::size_t k = 0; k < base.size(); ++k) {
      std::printf("    %4lldx%-3lld %.10e -> %.10e   rel change %.3e\n", (long long)base[k].nx,
                  (long long)base[k].ny, base[k].velocityL2, tight[k].velocityL2,
                  std::abs(tight[k].velocityL2 - base[k].velocityL2) /
                      std::max(base[k].velocityL2, 1e-300));
    }
    std::printf("\n  dp/dx SIGNED error: committed -> tighter\n");
    for (std::size_t k = 0; k < base.size(); ++k) {
      std::printf("    %4lldx%-3lld %+.10e -> %+.10e\n", (long long)base[k].nx,
                  (long long)base[k].ny, base[k].dpdxSignedError, tight[k].dpdxSignedError);
    }
    return 0;
  }

  // ---- RESIDUAL-PLATEAU / ITERATIVE-CONTAMINATION DIAGNOSTIC -----------------------------------
  // The 100x-tighter run showed this case cannot reach u 2e-7 at all (40000 iterations on every
  // grid). So the question is not "can it converge further" but "how far does the SOLUTION move
  // between the committed 2e-5 gate and the plateau". This runs each grid twice -- once to the
  // committed gate, once for a fixed 20000-iteration budget with an unreachable tolerance, with
  // the metrics force-extracted -- and reports the drift in exactly the two quantities W8 asserts.
  if (argc > 1 && std::string(argv[1]) == "plateau") {
    std::printf("# W8-INV-001 RESIDUAL-PLATEAU DIAGNOSTIC\n");
    std::printf("# committed gate (u 2e-5) vs 20000 iterations at an unreachable tolerance.\n");
    std::printf("# Diagnosis only: the plateau values are never proposed as test settings.\n\n");
    std::vector<Row> gate, plateau;
    for (auto [nx, ny] : std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}}) {
      gate.push_back(run(nx, ny));
      plateau.push_back(run(nx, ny, 1e-7, 20000, /*forceMetrics=*/true));
    }
    printRows("COMMITTED GATE (what W8 measures)", gate);
    orders("COMMITTED GATE", gate, 1.5, true);
    printRows("PLATEAU (20000 iterations, metrics force-extracted)", plateau);
    orders("PLATEAU", plateau, 1.5, true);
    std::printf("\n  DRIFT between the committed gate and the plateau\n");
    std::printf("  %8s %18s %18s %12s | %18s %18s\n", "grid", "velocityL2 gate",
                "velocityL2 plateau", "rel change", "dp/dx err gate", "dp/dx err plateau");
    for (std::size_t k = 0; k < gate.size(); ++k) {
      std::printf("  %4lldx%-3lld %18.10e %18.10e %12.3e | %+18.8e %+18.8e\n",
                  (long long)gate[k].nx, (long long)gate[k].ny, gate[k].velocityL2,
                  plateau[k].velocityL2,
                  std::abs(plateau[k].velocityL2 - gate[k].velocityL2) /
                      std::max(gate[k].velocityL2, 1e-300),
                  gate[k].dpdxSignedError, plateau[k].dpdxSignedError);
    }
    return 0;
  }

  std::printf("# W8-INV-001 distorted Poiseuille, extended refinement\n");
  std::printf("# mapping ax=0.1 ay=0.05 lambda=1.0 (ABSOLUTE, independent of nx/ny), L=%.1f H=%.1f\n",
              kLength, kHeight);
  std::printf("# exact dp/dx = %.10f; window x in [%.2f, %.2f]\n", kExactPressureGradient,
              kDevelopedStart, kDevelopedEnd);
  std::printf("# RAW values first; orders afterwards.\n");

  std::vector<Row> famA;
  for (auto [nx, ny] : std::vector<std::pair<Index, Index>>{{64, 8}, {96, 12}, {144, 18}, {216, 27}}) {
    if (quick && nx > 96) break;
    famA.push_back(run(nx, ny));
  }
  printRows("FAMILY A -- the test's own r = 1.5 family, EXTENDED", famA);
  orders("FAMILY A observed orders", famA, 1.5, true);

  std::vector<Row> famB;
  for (auto [nx, ny] : std::vector<std::pair<Index, Index>>{{64, 8}, {128, 16}, {256, 32}}) {
    if (quick && nx > 128) break;
    famB.push_back(run(nx, ny));
  }
  printRows("FAMILY B -- independent r = 2 family", famB);
  orders("FAMILY B observed orders", famB, 2.0, true);

  std::printf("\n#CSV family,nx,ny,conv,iters,velocityL2,dpdx,dpdxSignedError,maxNonOrtho,"
              "meanNonOrtho,maxSkew,maxAspect,wallDist,windowCells\n");
  const auto dump = [](const char* fam, const std::vector<Row>& rows) {
    for (const auto& r : rows) {
      std::printf("CSV,%s,%lld,%lld,%d,%lld,%.12e,%.12e,%.12e,%.6f,%.6f,%.6f,%.6f,%.8f,%lld\n",
                  fam, (long long)r.nx, (long long)r.ny, r.converged ? 1 : 0,
                  (long long)r.iterations, r.velocityL2, r.dpdx, r.dpdxSignedError,
                  r.maxNonOrtho, r.meanNonOrtho, r.maxSkew, r.maxAspect, r.minWallSpacing,
                  (long long)r.windowCells);
    }
  };
  dump("A", famA);
  dump("B", famB);
  return 0;
}
