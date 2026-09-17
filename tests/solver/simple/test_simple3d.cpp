// P12-MESH-006 -- the 3D incompressible SIMPLE: operator, reduced-system and boundary-condition
// verification (results/p12-mesh-006/acceptance_gate.md G1, G2, G3 and the six-orientation BC
// checks). Hand-derived values are written out in the comments and literals below, never computed
// by a helper that mirrors the production code.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/solver/SolverRobustness.hpp"
#include "cfd/validation/ErrorNorms.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::Symmetry;
using cfd::boundary::Wall;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::FaceFluxScheme;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

using Dense = std::vector<std::vector<Real>>;

Dense toDense(const cfd::algebra::SparseMatrix& a) {
  Dense d(a.rows(), std::vector<Real>(a.rows(), 0.0));
  for (Index r = 0; r < a.rows(); ++r) {
    for (Index k = a.rowOffsetsData()[r]; k < a.rowOffsetsData()[r + 1]; ++k) {
      d[r][a.columnIndicesData()[k]] = a.valuesData()[k];
    }
  }
  return d;
}

Real relative(Real a, Real b, Real scale) { return std::abs(a - b) / scale; }

// A 3D SIMPLE configuration for the small verification problems (tight inner solves so the outer
// iteration, not the linear solver, sets the iterative error).
SIMPLESettings settings3D(Real tolerance, Index maxIterations = 20000) {
  SIMPLESettings s;
  s.maxIterations = maxIterations;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = tolerance;
  s.pressureTolerance = tolerance;
  s.continuityTolerance = tolerance;
  s.momentumSolver.absoluteTolerance = 1e-16;
  s.momentumSolver.relativeTolerance = 1e-9;
  s.momentumSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-16;
  s.pressureSolver.relativeTolerance = 1e-9;
  s.pressureSolver.maxIterations = 20000;
  s.convectionScheme = ConvectionScheme::QUICK;
  return s;
}

// The lid-driven unit cube: lid = ymax moving (U, 0, 0), every other face a wall, zero-gradient
// pressure (closed: reference cell).
void lidCubeBoundaries(const Mesh& mesh, BoundaryConditionSet& velocity,
                       BoundaryConditionSet& pressure) {
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == "ymax") {
      velocity.set(mesh, patch.name(), std::make_unique<MovingWall>(Vector3{1.0, 0.0, 0.0}));
    } else {
      velocity.set(mesh, patch.name(), std::make_unique<Wall>());
    }
    pressure.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
}

Real gaugeRemovedMaxDifference(const Mesh& mesh, const ScalarField& a, const ScalarField& b) {
  Real meanA = 0.0;
  Real meanB = 0.0;
  Real volume = 0.0;
  for (const auto& c : mesh.cells()) {
    meanA += a[c.id()] * c.volume();
    meanB += b[c.id()] * c.volume();
    volume += c.volume();
  }
  meanA /= volume;
  meanB /= volume;
  Real m = 0.0;
  for (Index i = 0; i < a.size(); ++i) m = std::max(m, std::abs((a[i] - meanA) - (b[i] - meanB)));
  return m;
}

Real range(const ScalarField& f) {
  Real lo = f[0];
  Real hi = f[0];
  for (Index i = 0; i < f.size(); ++i) {
    lo = std::min(lo, f[i]);
    hi = std::max(hi, f[i]);
  }
  return hi - lo;
}

// ------------------------------------------------------------------------------------------------
// G1.1 permutation symmetry: problem B is problem A under the cyclic axis permutation
// (x, y, z) -> (y, z, x): the A point (x, y, z) is the B point (z, x, y), and a vector (u, v, w) of
// A is (w, u, v) in B. So A's U equation is B's V equation, A's V is B's W, A's W is B's U.
Vector3 toB(const Vector3& a) { return Vector3{a.z, a.x, a.y}; }
Vector3 toA(const Vector3& b) { return Vector3{b.y, b.z, b.x}; }

Vector3 velocityA(const Vector3& p) {
  return Vector3{0.6 + (0.2 * std::sin(p.x)) + (0.1 * p.y * p.z),
                 (0.3 * std::cos(p.y)) - (0.2 * p.x), 0.25 + (0.15 * p.x * p.y) - (0.1 * p.z)};
}
Real pressureA(const Vector3& p) {
  return (0.3 * p.x * p.x) - (0.2 * p.y) + (0.1 * std::sin(p.z)) + (0.05 * p.x * p.y * p.z);
}
Real viscosityA(const Vector3& p) { return 0.02 + (0.01 * p.x * p.x); }
Vector3 sourceA(const Vector3& p) { return Vector3{0.1 * p.y, -0.05 * p.z, 0.2 * p.x}; }

struct Problem {
  Mesh mesh;
  VectorField velocity;
  ScalarField pressure;
  ScalarField viscosity;
  VectorField source;
  SurfaceField flux;
};

// `inB`: evaluate the A fields at the A point of each B location (fields of B = permuted A).
Problem makeProblem(Mesh mesh, bool inB) {
  Problem p{std::move(mesh), {}, {}, {}, {}, {}};
  const Index n = p.mesh.numberOfCells();
  p.velocity = VectorField(n);
  p.pressure = ScalarField(n);
  p.viscosity = ScalarField(n);
  p.source = VectorField(n);
  for (const auto& c : p.mesh.cells()) {
    const Vector3 a = inB ? toA(c.centroid()) : c.centroid();
    p.velocity[c.id()] = inB ? toB(velocityA(a)) : velocityA(a);
    p.pressure[c.id()] = pressureA(a);
    p.viscosity[c.id()] = viscosityA(a);
    p.source[c.id()] = inB ? toB(sourceA(a)) : sourceA(a);
  }
  p.flux = SurfaceField(p.mesh.numberOfFaces());
  for (const auto& f : p.mesh.faces()) {
    const Vector3 a = inB ? toA(f.centroid()) : f.centroid();
    const Vector3 u = inB ? toB(velocityA(a)) : velocityA(a);
    p.flux[f.id()] = 1.1 * dot(u, f.areaVector());
  }
  return p;
}

}  // namespace

TEST(SIMPLE3D, MomentumSystemsArePermutationSymmetric) {
  const Vector3 originA{0.2, -0.1, 0.4};
  const Problem a =
      makeProblem(MeshGeometry::createCartesian3D(3, 4, 5, 1.5, 0.7, 2.3, originA), false);
  const Problem b =
      makeProblem(MeshGeometry::createCartesian3D(5, 3, 4, 2.3, 1.5, 0.7, toB(originA)), true);
  const Index n = a.mesh.numberOfCells();
  ASSERT_EQ(n, b.mesh.numberOfCells());
  // Cell map A (i, j, k) -> B (k, i, j).
  std::vector<Index> map(n);
  for (Index k = 0; k < 5; ++k)
    for (Index j = 0; j < 4; ++j)
      for (Index i = 0; i < 3; ++i) map[((k * 4) + j) * 3 + i] = ((j * 3) + i) * 5 + k;
  for (Index c = 0; c < n; ++c) {
    const Vector3 expected = toB(a.mesh.cell(c).centroid());
    const Vector3 got = b.mesh.cell(map[c]).centroid();
    ASSERT_NEAR(got.x, expected.x, 1e-14);
    ASSERT_NEAR(got.y, expected.y, 1e-14);
    ASSERT_NEAR(got.z, expected.z, 1e-14);
  }
  // Boundary conditions of A and their images in B (patch xmin -> ymin, ymin -> zmin, zmin ->
  // xmin).
  const auto buildBoundaries = [](const Mesh& mesh, bool inB, BoundaryConditionSet& vb,
                                  BoundaryConditionSet& pb) {
    const auto name = [inB](const std::string& aName) {
      if (!inB) return aName;
      const char axis = aName[0] == 'x' ? 'y' : (aName[0] == 'y' ? 'z' : 'x');
      return std::string(1, axis) + aName.substr(1);
    };
    const auto vec = [inB](const Vector3& v) { return inB ? toB(v) : v; };
    vb.set(mesh, name("xmin"), std::make_unique<Inlet>(vec(Vector3{0.8, 0.1, -0.2})));
    vb.set(mesh, name("xmax"), std::make_unique<Outlet>());
    vb.set(mesh, name("ymin"), std::make_unique<Wall>());
    vb.set(mesh, name("ymax"), std::make_unique<MovingWall>(vec(Vector3{0.5, 0.0, 0.3})));
    vb.set(mesh, name("zmin"), std::make_unique<Symmetry>());
    vb.set(mesh, name("zmax"), std::make_unique<Wall>());
    pb.set(mesh, name("xmin"), std::make_unique<FixedGradient>(0.1));
    pb.set(mesh, name("xmax"), std::make_unique<FixedValue>(0.25));
    pb.set(mesh, name("ymin"), std::make_unique<FixedGradient>(0.0));
    pb.set(mesh, name("ymax"), std::make_unique<FixedGradient>(-0.05));
    pb.set(mesh, name("zmin"), std::make_unique<FixedGradient>(0.0));
    pb.set(mesh, name("zmax"), std::make_unique<FixedGradient>(0.02));
  };
  BoundaryConditionSet vbA;
  BoundaryConditionSet pbA;
  BoundaryConditionSet vbB;
  BoundaryConditionSet pbB;
  buildBoundaries(a.mesh, false, vbA, pbA);
  buildBoundaries(b.mesh, true, vbB, pbB);

  const auto component = [](const VectorField& v, VelocityComponent c) {
    ScalarField s(v.size());
    for (Index i = 0; i < v.size(); ++i) s[i] = cfd::physics::velocityComponentValue(v[i], c);
    return s;
  };
  const std::vector<std::pair<VelocityComponent, VelocityComponent>> pairs = {
      {VelocityComponent::U, VelocityComponent::V},
      {VelocityComponent::V, VelocityComponent::W},
      {VelocityComponent::W, VelocityComponent::U}};
  Index compared = 0;
  for (const bool corrected : {false, true}) {
    for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                              ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
      for (const auto& [cA, cB] : pairs) {
        SCOPED_TRACE(std::string(cfd::discretization::convectionSchemeName(scheme)) +
                     (corrected ? " corrected" : "") + " component pair " +
                     std::to_string(static_cast<int>(cA)));
        const GradientScheme gs =
            corrected ? GradientScheme::LeastSquares : GradientScheme::GreenGauss;
        const auto sysA = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
            a.mesh, a.velocity, a.pressure, a.flux, a.viscosity, vbA, pbA, cA,
            component(a.velocity, cA), 0.7, nullptr, nullptr, scheme, gs, corrected, nullptr,
            &a.source);
        const auto sysB = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
            b.mesh, b.velocity, b.pressure, b.flux, b.viscosity, vbB, pbB, cB,
            component(b.velocity, cB), 0.7, nullptr, nullptr, scheme, gs, corrected, nullptr,
            &b.source);
        const Dense mA = toDense(sysA.system.matrix());
        const Dense mB = toDense(sysB.system.matrix());
        for (Index i = 0; i < n; ++i) {
          Real rowScale = 0.0;
          for (Index j = 0; j < n; ++j) rowScale = std::max(rowScale, std::abs(mA[i][j]));
          for (Index j = 0; j < n; ++j) {
            EXPECT_LE(relative(mA[i][j], mB[map[i]][map[j]], rowScale), 1e-13)
                << "row " << i << " col " << j;
          }
          EXPECT_LE(relative(sysA.system.rhs()[i], sysB.system.rhs()[map[i]],
                             std::max(rowScale, std::abs(sysA.system.rhs()[i]))),
                    1e-13)
              << "rhs row " << i;
          EXPECT_LE(relative(sysA.diagonal[i], sysB.diagonal[map[i]], rowScale), 1e-13);
        }
        ++compared;
      }
    }
  }
  EXPECT_EQ(compared, 24);
}

// G1.2: the W residual gates convergence; a 2D sample (no w) behaves as before.
TEST(SIMPLE3D, MonitorNeverConvergesWhileWIsUnconverged) {
  for (const auto criterion : {cfd::solver::ConvergenceCriterion::Absolute,
                               cfd::solver::ConvergenceCriterion::Normalized}) {
    cfd::solver::SolverRobustnessSettings robustness;
    robustness.convergenceCriterion = criterion;
    const cfd::solver::OuterConvergenceTolerances tol{1e-6, 1e-6, 1e-6, 1e-6};
    {
      cfd::solver::OuterIterationMonitor monitor(robustness, tol, 0.7, 0.3);
      const cfd::solver::OuterResidualSample wHigh{1e-8, 1e-8,         1e-8, 1e-8,
                                                   1e-8, std::nullopt, 1e-3};
      EXPECT_EQ(monitor.record(wHigh), cfd::solver::OuterIterationVerdict::Continue);
      const cfd::solver::OuterResidualSample wLow{1e-8, 1e-8, 1e-8, 1e-8, 1e-8, std::nullopt, 1e-8};
      EXPECT_EQ(monitor.record(wLow), cfd::solver::OuterIterationVerdict::Converged);
      EXPECT_EQ(monitor.diagnostics().wNormalizedHistory.size(), 2U);
    }
    {
      cfd::solver::OuterIterationMonitor monitor(robustness, tol, 0.7, 0.3);
      const cfd::solver::OuterResidualSample twoD{1e-8, 1e-8, 1e-8, 1e-8, 1e-8, std::nullopt};
      EXPECT_EQ(monitor.record(twoD), cfd::solver::OuterIterationVerdict::Converged);
      EXPECT_TRUE(monitor.diagnostics().wNormalizedHistory.empty());
    }
  }
  // SIMPLE on a 3D mesh records a W history of the same length as U, V and uses Rhie-Chow.
  const Mesh mesh = MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0);
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  lidCubeBoundaries(mesh, vb, pb);
  const SIMPLE simple(settings3D(1e-30, 5), 0);
  const SIMPLEResult r =
      simple.solve(mesh, FluidProperties(1.0, 0.01), vb, pb, VectorField(mesh.numberOfCells()),
                   ScalarField(mesh.numberOfCells(), 0.0));
  EXPECT_EQ(r.iterations, 5U);
  EXPECT_EQ(r.wResidualHistory.size(), r.uResidualHistory.size());
  EXPECT_EQ(r.wResidualHistory.size(), 5U);
  EXPECT_EQ(r.finalWResidual, r.wResidualHistory.back());
  EXPECT_GT(r.finalWResidual, 0.0);
  EXPECT_EQ(r.faceFlux, FaceFluxScheme::RhieChow);
  EXPECT_EQ(r.status, cfd::pressure_velocity::SIMPLEStatus::MaxIterations);
}

// ------------------------------------------------------------------------------------------------
// G2 -- face flux and continuity.
TEST(SIMPLE3D, UniformFlowFluxIsExactOnEveryFaceOrientation) {
  const Mesh mesh =
      MeshGeometry::createCartesian3D(3, 4, 5, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1});
  const Vector3 u{0.3, -0.7, 1.1};
  const Real rho = 1.3;
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  for (const auto& p : mesh.boundaryPatches()) {
    vb.set(mesh, p.name(), std::make_unique<Inlet>(u));
    pb.set(mesh, p.name(), std::make_unique<FixedGradient>(0.0));
  }
  const VectorField velocity(mesh.numberOfCells(), u);
  const SurfaceField linear =
      cfd::physics::calculateMassFlux(mesh, velocity, FluidProperties(rho, 0.01), vb);
  // Rhie-Chow with a zero pressure field: the correction is exactly zero.
  const ScalarField zero(mesh.numberOfCells(), 0.0);
  const ScalarField d(mesh.numberOfCells(), 0.01);
  const VectorField gradZero(mesh.numberOfCells());
  const SurfaceField rc = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, zero, gradZero, d, d, &d, FluidProperties(rho, 0.01), vb, 0.7);
  int orientations[6] = {0, 0, 0, 0, 0, 0};
  for (const auto& f : mesh.faces()) {
    const Real exact = rho * dot(u, f.areaVector());
    const Real scale = rho * magnitude(u) * f.area();
    EXPECT_LE(std::abs(linear[f.id()] - exact), 1e-14 * scale) << "face " << f.id();
    EXPECT_LE(std::abs(rc[f.id()] - exact), 1e-14 * scale) << "face " << f.id();
    const Vector3& s = f.areaVector();
    const int axis = s.x != 0.0 ? 0 : (s.y != 0.0 ? 1 : 2);
    const Real sign = s.x + s.y + s.z;
    ++orientations[(2 * axis) + (sign > 0.0 ? 1 : 0)];
  }
  for (const int count : orientations) EXPECT_GT(count, 0);  // -x, +x, -y, +y, -z, +z all present
  // Continuity of the uniform flow, and the global mass balance.
  const auto continuity = cfd::physics::evaluateContinuity(mesh, linear);
  for (const auto& c : mesh.cells()) {
    Real sumAbs = 0.0;
    for (const Index fid : c.faceIds()) sumAbs += std::abs(linear[fid]);
    EXPECT_LE(std::abs(continuity.cellImbalance[c.id()]), 1e-14 * sumAbs);
  }
  const auto balance = cfd::physics::computeMassBalance(mesh, linear);
  // Inflow through xmin (u > 0), ymax (v < 0), zmin (w > 0): rho (0.3 Ly Lz + 0.7 Lx Lz + 1.1 Lx
  // Ly).
  const Real inflow = rho * ((0.3 * 0.7 * 2.3) + (0.7 * 1.5 * 2.3) + (1.1 * 1.5 * 0.7));
  EXPECT_NEAR(balance.inflow, inflow, 1e-14 * inflow);
  EXPECT_NEAR(balance.outflow, inflow, 1e-14 * inflow);
  EXPECT_LE(std::abs(balance.net), 1e-14 * inflow);
  EXPECT_LE(balance.relativeImbalance, 1e-14);
}

TEST(SIMPLE3D, RhieChowVanishesForALinearPressureField) {
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian3D(4, 5, 6, 1.2, 0.9, 1.5);
  // Exact per-face pressure data (one singleton patch per boundary face).
  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& f : mesh.faces())
    if (f.isBoundary())
      patches.emplace_back("b" + std::to_string(f.id()), std::vector<Index>{f.id()});
  const Mesh perFace(mesh.cells(), mesh.faces(), std::move(patches));
  const auto p = [](const Vector3& x) { return 2.0 - x.x + (3.0 * x.y) - (4.0 * x.z); };
  BoundaryConditionSet pb;
  BoundaryConditionSet vb;
  for (const auto& patch : perFace.boundaryPatches()) {
    pb.set(perFace, patch.name(),
           std::make_unique<FixedValue>(p(perFace.face(patch.faceIds().front()).centroid())));
    vb.set(perFace, patch.name(), std::make_unique<Inlet>(Vector3{0.4, 0.2, -0.3}));
  }
  ScalarField pressure(perFace.numberOfCells());
  for (const auto& c : perFace.cells()) pressure[c.id()] = p(c.centroid());
  const VectorField grad = cfd::discretization::gradient(perFace, pressure, pb);
  const ScalarField d(perFace.numberOfCells(), 0.02);
  const SurfaceField correction =
      cfd::pressure_velocity::rhieChowFaceCorrection(perFace, pressure, grad, d, d, &d, 1.0, 0.7);
  const Real scale = magnitude(Vector3{0.4, 0.2, -0.3});
  for (const auto& f : perFace.faces()) {
    EXPECT_LE(std::abs(correction[f.id()]), 1e-12 * scale * f.area()) << "face " << f.id();
  }
}

TEST(SIMPLE3D, RhieChowSeesTheCheckerboardTheLinearFluxCannot) {
  const Index n = 6;
  const Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  for (const auto& patch : mesh.boundaryPatches()) {
    vb.set(mesh, patch.name(), std::make_unique<Wall>());
    pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  ScalarField pressure(mesh.numberOfCells());
  for (Index k = 0; k < n; ++k)
    for (Index j = 0; j < n; ++j)
      for (Index i = 0; i < n; ++i)
        pressure[((k * n) + j) * n + i] = ((i + j + k) % 2 == 0) ? 1.0 : -1.0;
  const VectorField zeroVelocity(mesh.numberOfCells());
  const FluidProperties fluid(1.0, 0.01);
  const SurfaceField linear = cfd::physics::calculateMassFlux(mesh, zeroVelocity, fluid, vb);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) EXPECT_EQ(linear[f], 0.0);

  const VectorField grad = cfd::discretization::gradient(mesh, pressure, pb);
  const ScalarField d(mesh.numberOfCells(), 0.02);
  const Real alpha = 0.7;
  const SurfaceField rc = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, zeroVelocity, pressure, grad, d, d, &d, fluid, vb, alpha);
  const auto ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
  // D_f = rho A_f d / |d| = 1 * (1/36) * 0.02 / (1/6) = 0.02 / 6 on every internal face.
  const Real coupling = (1.0 / 36.0) * 0.02 / (1.0 / 6.0);
  Index checked = 0;
  for (const auto& f : mesh.faces()) {
    if (f.isBoundary() || ring[f.owner()] || ring[*f.neighbor()]) continue;
    const Real expected = -(coupling / alpha) * (pressure[*f.neighbor()] - pressure[f.owner()]);
    EXPECT_NEAR(std::abs(expected), 2.0 * coupling / alpha, 1e-16);
    EXPECT_LE(std::abs(rc[f.id()] - expected), 1e-14 * std::abs(expected)) << "face " << f.id();
    ++checked;
  }
  // Internal faces between two of the 4 x 4 x 4 inner cells: 3 * 3 * 4 * 4 = 144.
  EXPECT_EQ(checked, 144U);
}

// ------------------------------------------------------------------------------------------------
// G3.1 -- reduced pressure-velocity systems derived by hand.
namespace {

// Pressure BCs: FixedValue(0) on `fixedPatch`, zero gradient elsewhere; the p' gradient BCs are the
// ones correctVelocity uses (p' = 0 on the FixedValue patch, zero gradient elsewhere).
void pressureBoundaries(const Mesh& mesh, const std::string& fixedPatch, BoundaryConditionSet& pb,
                        BoundaryConditionSet& gradientBoundaries) {
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == fixedPatch) {
      pb.set(mesh, patch.name(), std::make_unique<FixedValue>(0.0));
      gradientBoundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(0.0));
    } else {
      pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
      gradientBoundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
  }
}

cfd::algebra::Vector diag(std::initializer_list<Real> values) {
  cfd::algebra::Vector v(values.size());
  Index i = 0;
  for (const Real x : values) v[i++] = x;
  return v;
}

// Checks the velocity correction u = u* - (d_u dp'/dx, d_v dp'/dy, d_w dp'/dz) with the hand d
// values and the production gradient of p' (verified in P12-MESH-005, C2).
void expectVelocityCorrection(const Mesh& mesh, const ScalarField& pPrime,
                              const BoundaryConditionSet& pb,
                              const BoundaryConditionSet& gradientBoundaries, const ScalarField& dU,
                              const ScalarField& dV, const ScalarField& dW) {
  const Index n = mesh.numberOfCells();
  VectorField star(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = static_cast<Real>(i);
    star[i] = Vector3{0.1 * x, -0.2, 0.3 + (0.05 * x)};
  }
  const VectorField corrected = cfd::pressure_velocity::correctVelocity(
      mesh, star, dU, dV, pPrime, pb, GradientScheme::GreenGauss, &dW);
  const VectorField g = cfd::discretization::gradient(mesh, pPrime, gradientBoundaries);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(corrected[i].x, star[i].x - (dU[i] * g[i].x), 1e-15);
    EXPECT_NEAR(corrected[i].y, star[i].y - (dV[i] * g[i].y), 1e-15);
    EXPECT_NEAR(corrected[i].z, star[i].z - (dW[i] * g[i].z), 1e-15);
  }
}

}  // namespace

// 2 x 1 x 1 on the unit cube (dx = 0.5, dy = dz = 1), rho = 1.2, FixedValue(0) on xmax.
// Faces: x-faces 0 (xmin, cell 0), 1 (internal 0 -> 1), 2 (xmax, cell 1); y-faces 3..6;
// z-faces 7..10. d = V / A with V = 0.5: d_u = (0.125, 1/12), d_v = (0.1, 1/14), d_w = (0.0625,
// 1/18). Internal x-face: D = rho A (d_u,0 + d_u,1)/2 / |d| = 1.2 * 1 * (5/48) / 0.5 = 0.25. xmax
// face (Dirichlet): D = rho A d_u,1 / |d_Pf| = 1.2 * (1/12) / 0.25 = 0.4. Matrix [[0.25, -0.25],
// [-0.25, 0.65]]. Predictor fluxes (owner-oriented) F = (-0.9, 0.7, 0.55, 0.02, 0, -0.01, 0, 0.03,
// 0, 0, -0.04): imbalances R0 = -0.9 + 0.7 + 0.02 - 0.01 + 0.03 = -0.16, R1 = -0.7 + 0.55 - 0.04 =
// -0.19 -> RHS (0.16, 0.19). det = 0.1: p' = (1.515, 0.875). Corrected: F1 = 0.7 + 0.25 * 0.64 =
// 0.86, F2 = 0.55 + 0.4 * 0.875 = 0.9 -> both cells balance.
TEST(SIMPLE3D, ReducedSystemTwoByOneByOneMatchesHandDerivation) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  BoundaryConditionSet pb;
  BoundaryConditionSet gb;
  pressureBoundaries(mesh, "xmax", pb, gb);
  const ScalarField dU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({4, 6}));
  const ScalarField dV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({5, 7}));
  const ScalarField dW =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({8, 9}));
  EXPECT_NEAR(dU[0], 0.125, 1e-16);
  EXPECT_NEAR(dU[1], 1.0 / 12.0, 1e-16);
  EXPECT_NEAR(dW[1], 1.0 / 18.0, 1e-16);
  const Real f[11] = {-0.9, 0.7, 0.55, 0.02, 0.0, -0.01, 0.0, 0.03, 0.0, 0.0, -0.04};
  SurfaceField flux(mesh.numberOfFaces());
  for (Index i = 0; i < 11; ++i) flux[i] = f[i];
  const auto pc =
      cfd::pressure_velocity::assemblePressureCorrection(mesh, flux, dU, dV, 1.2, 0, pb, {}, &dW);
  const Dense a = toDense(pc.system.matrix());
  EXPECT_NEAR(a[0][0], 0.25, 1e-15);
  EXPECT_NEAR(a[0][1], -0.25, 1e-15);
  EXPECT_NEAR(a[1][0], -0.25, 1e-15);
  EXPECT_NEAR(a[1][1], 0.65, 1e-15);
  EXPECT_NEAR(pc.system.rhs()[0], 0.16, 1e-15);
  EXPECT_NEAR(pc.system.rhs()[1], 0.19, 1e-15);
  for (Index i = 0; i < 11; ++i) {
    const Real expected = i == 1 ? 0.25 : (i == 2 ? 0.4 : 0.0);
    EXPECT_NEAR(pc.faceCoefficient[i], expected, 1e-15) << "face " << i;
  }
  ScalarField pPrime(2);
  pPrime[0] = 1.515;
  pPrime[1] = 0.875;
  // The production linear solver reproduces the hand solution.
  cfd::algebra::LinearSolverSettings ls;
  ls.relativeTolerance = 1e-14;
  ls.absoluteTolerance = 1e-16;
  const auto solved = cfd::algebra::BiCGSTAB(ls).solve(pc.system);
  ASSERT_TRUE(solved.converged());
  EXPECT_NEAR(solved.solution[0], 1.515, 1e-13);
  EXPECT_NEAR(solved.solution[1], 0.875, 1e-13);
  const SurfaceField corrected =
      cfd::pressure_velocity::correctFaceMassFlux(mesh, flux, pc.faceCoefficient, pPrime);
  EXPECT_NEAR(corrected[1], 0.86, 1e-14);
  EXPECT_NEAR(corrected[2], 0.9, 1e-14);
  for (Index i = 0; i < 11; ++i) {
    if (i != 1 && i != 2) {
      EXPECT_EQ(corrected[i], flux[i]);
    }
  }
  const auto continuity = cfd::physics::evaluateContinuity(mesh, corrected);
  EXPECT_LE(std::abs(continuity.cellImbalance[0]), 1e-14);
  EXPECT_LE(std::abs(continuity.cellImbalance[1]), 1e-14);
  expectVelocityCorrection(mesh, pPrime, pb, gb, dU, dV, dW);
}

// 1 x 1 x 2 (dz = 0.5, dx = dy = 1): the coupling goes through the z-faces with d_w.
// Faces: x-faces 0, 1 (cell 0), 2, 3 (cell 1); y-faces 4, 5 (cell 0), 6, 7 (cell 1); z-faces 8
// (zmin), 9 (internal 0 -> 1), 10 (zmax). FixedValue(0) on zmax. A_w = (8, 10): d_w = (0.0625,
// 0.05). Internal z-face: D = 1.2 * 1 * 0.05625 / 0.5 = 0.135; zmax: D = 1.2 * 0.05 / 0.25 = 0.24.
// Matrix [[0.135, -0.135], [-0.135, 0.375]]. F = (0.01, 0, 0, -0.02, 0, 0, 0, 0, -0.6, 0.5, 0.3):
// R0 = 0.01 - 0.6 + 0.5 = -0.09, R1 = -0.02 - 0.5 + 0.3 = -0.22 -> RHS (0.09, 0.22).
// det = 0.0324: p' = (47/24, 31/24). Corrected: F9 = 0.5 + 0.135 * 2/3 = 0.59,
// F10 = 0.3 + 0.24 * 31/24 = 0.61.
TEST(SIMPLE3D, ReducedSystemOneByOneByTwoCouplesThroughZ) {
  const Mesh mesh = MeshGeometry::createCartesian3D(1, 1, 2, 1.0, 1.0, 1.0);
  BoundaryConditionSet pb;
  BoundaryConditionSet gb;
  pressureBoundaries(mesh, "zmax", pb, gb);
  const ScalarField dU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({3, 5}));
  const ScalarField dV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({4, 6}));
  const ScalarField dW =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diag({8, 10}));
  const Real f[11] = {0.01, 0.0, 0.0, -0.02, 0.0, 0.0, 0.0, 0.0, -0.6, 0.5, 0.3};
  SurfaceField flux(mesh.numberOfFaces());
  for (Index i = 0; i < 11; ++i) flux[i] = f[i];
  const auto pc =
      cfd::pressure_velocity::assemblePressureCorrection(mesh, flux, dU, dV, 1.2, 0, pb, {}, &dW);
  const Dense a = toDense(pc.system.matrix());
  EXPECT_NEAR(a[0][0], 0.135, 1e-15);
  EXPECT_NEAR(a[0][1], -0.135, 1e-15);
  EXPECT_NEAR(a[1][0], -0.135, 1e-15);
  EXPECT_NEAR(a[1][1], 0.375, 1e-15);
  EXPECT_NEAR(pc.system.rhs()[0], 0.09, 1e-15);
  EXPECT_NEAR(pc.system.rhs()[1], 0.22, 1e-15);
  EXPECT_NEAR(pc.faceCoefficient[9], 0.135, 1e-15);
  EXPECT_NEAR(pc.faceCoefficient[10], 0.24, 1e-15);
  ScalarField pPrime(2);
  pPrime[0] = 47.0 / 24.0;
  pPrime[1] = 31.0 / 24.0;
  cfd::algebra::LinearSolverSettings ls;
  ls.relativeTolerance = 1e-14;
  ls.absoluteTolerance = 1e-16;
  const auto solved = cfd::algebra::BiCGSTAB(ls).solve(pc.system);
  ASSERT_TRUE(solved.converged());
  EXPECT_NEAR(solved.solution[0], 47.0 / 24.0, 1e-13);
  EXPECT_NEAR(solved.solution[1], 31.0 / 24.0, 1e-13);
  const SurfaceField corrected =
      cfd::pressure_velocity::correctFaceMassFlux(mesh, flux, pc.faceCoefficient, pPrime);
  EXPECT_NEAR(corrected[9], 0.59, 1e-14);
  EXPECT_NEAR(corrected[10], 0.61, 1e-14);
  const auto continuity = cfd::physics::evaluateContinuity(mesh, corrected);
  EXPECT_LE(std::abs(continuity.cellImbalance[0]), 1e-14);
  EXPECT_LE(std::abs(continuity.cellImbalance[1]), 1e-14);
  expectVelocityCorrection(mesh, pPrime, pb, gb, dU, dV, dW);
}

// 2 x 2 x 2 (dx = dy = dz = 0.5, V = 0.125, face area 0.25), FixedValue(0) on zmax. Cell id
// (k*2 + j)*2 + i. Hand-chosen diagonals A_u = 1 + id, A_v = 2 + id, A_w = 3 + 2 id, so a component
// mix-up changes the coefficients. Couplings (D = rho A d_f / |d|, rho = 1.2):
//   internal x-face P|N: 1.2 * 0.25 * (d_u,P + d_u,N)/2 / 0.5 = 0.3 (d_u,P + d_u,N)   (and y: d_v,
//   z: d_w) zmax face of cell P (Dirichlet): 1.2 * 0.25 * d_w,P / 0.25 = 1.2 d_w,P.
// The predictor fluxes are zero on internal faces; each cell's x-boundary face carries a flux
// chosen so that the exact p' is the hand vector below (R* = -A p', A the hand matrix).
TEST(SIMPLE3D, ReducedSystemTwoByTwoByTwoMatchesHandDerivation) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 2, 2, 1.0, 1.0, 1.0);
  BoundaryConditionSet pb;
  BoundaryConditionSet gb;
  pressureBoundaries(mesh, "zmax", pb, gb);
  Real du[8];
  Real dv[8];
  Real dw[8];
  cfd::algebra::Vector au(8);
  cfd::algebra::Vector av(8);
  cfd::algebra::Vector aw(8);
  for (Index id = 0; id < 8; ++id) {
    au[id] = 1.0 + static_cast<Real>(id);
    av[id] = 2.0 + static_cast<Real>(id);
    aw[id] = 3.0 + (2.0 * static_cast<Real>(id));
    du[id] = 0.125 / au[id];
    dv[id] = 0.125 / av[id];
    dw[id] = 0.125 / aw[id];
  }
  Dense hand(8, std::vector<Real>(8, 0.0));
  const auto couple = [&](Index p, Index q, Real coefficient) {
    hand[p][p] += coefficient;
    hand[q][q] += coefficient;
    hand[p][q] -= coefficient;
    hand[q][p] -= coefficient;
  };
  for (const auto& [p, q] : {std::pair<Index, Index>{0, 1}, {2, 3}, {4, 5}, {6, 7}})
    couple(p, q, 0.3 * (du[p] + du[q]));
  for (const auto& [p, q] : {std::pair<Index, Index>{0, 2}, {1, 3}, {4, 6}, {5, 7}})
    couple(p, q, 0.3 * (dv[p] + dv[q]));
  for (const auto& [p, q] : {std::pair<Index, Index>{0, 4}, {1, 5}, {2, 6}, {3, 7}})
    couple(p, q, 0.3 * (dw[p] + dw[q]));
  for (Index p = 4; p < 8; ++p) hand[p][p] += 1.2 * dw[p];

  const Real pExact[8] = {0.3, -0.2, 0.5, 0.1, 0.4, -0.1, 0.2, 0.6};
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (const auto& c : mesh.cells()) {
    Real rStar = 0.0;  // R* = -(A p')_c
    for (Index q = 0; q < 8; ++q) rStar -= hand[c.id()][q] * pExact[q];
    for (const Index fid : c.faceIds()) {
      const auto& face = mesh.face(fid);
      if (face.isBoundary() && face.areaVector().x != 0.0) {
        flux[fid] = rStar;  // the owner-oriented flux of this cell's x-boundary face
        break;
      }
    }
  }
  const ScalarField dU = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, au);
  const ScalarField dV = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, av);
  const ScalarField dW = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, aw);
  const auto pc =
      cfd::pressure_velocity::assemblePressureCorrection(mesh, flux, dU, dV, 1.2, 0, pb, {}, &dW);
  const Dense a = toDense(pc.system.matrix());
  for (Index p = 0; p < 8; ++p) {
    for (Index q = 0; q < 8; ++q) {
      EXPECT_LE(std::abs(a[p][q] - hand[p][q]), 1e-13 * std::abs(hand[p][p])) << p << "," << q;
    }
    Real rhs = 0.0;
    for (Index q = 0; q < 8; ++q) rhs += hand[p][q] * pExact[q];
    EXPECT_LE(std::abs(pc.system.rhs()[p] - rhs), 1e-13 * std::abs(hand[p][p]));
  }
  cfd::algebra::LinearSolverSettings ls;
  ls.relativeTolerance = 1e-15;
  ls.absoluteTolerance = 1e-17;
  const auto solved = cfd::algebra::BiCGSTAB(ls).solve(pc.system);
  ASSERT_TRUE(solved.converged());
  ScalarField pPrime(8);
  for (Index q = 0; q < 8; ++q) {
    EXPECT_NEAR(solved.solution[q], pExact[q], 1e-13) << "cell " << q;
    pPrime[q] = pExact[q];
  }
  const SurfaceField corrected =
      cfd::pressure_velocity::correctFaceMassFlux(mesh, flux, pc.faceCoefficient, pPrime);
  const auto continuity = cfd::physics::evaluateContinuity(mesh, corrected);
  Real scale = 0.0;
  for (Index f = 0; f < corrected.size(); ++f) scale += std::abs(corrected[f]);
  for (Index c = 0; c < 8; ++c) EXPECT_LE(std::abs(continuity.cellImbalance[c]), 1e-13 * scale);
  expectVelocityCorrection(mesh, pPrime, pb, gb, dU, dV, dW);
}

// ------------------------------------------------------------------------------------------------
// G3.2 -- the pressure reference changes nothing but the pressure gauge.
TEST(SIMPLE3D, PressureReferenceDoesNotChangeVelocityOrFlux) {
  const Mesh mesh = MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  lidCubeBoundaries(mesh, vb, pb);
  const FluidProperties fluid(1.0, 0.01);  // Re = 100
  const Index n = mesh.numberOfCells();
  const SIMPLEResult first =
      SIMPLE(settings3D(1e-10), 0).solve(mesh, fluid, vb, pb, VectorField(n), ScalarField(n, 0.0));
  const SIMPLEResult last = SIMPLE(settings3D(1e-10), n - 1)
                                .solve(mesh, fluid, vb, pb, VectorField(n), ScalarField(n, 0.0));
  ASSERT_TRUE(first.converged());
  ASSERT_TRUE(last.converged());
  Real du = 0.0;
  for (Index i = 0; i < n; ++i) du = std::max(du, magnitude(first.velocity[i] - last.velocity[i]));
  Real dF = 0.0;
  Real maxF = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    dF = std::max(dF, std::abs(first.massFlux[f] - last.massFlux[f]));
    maxF = std::max(maxF, std::abs(first.massFlux[f]));
  }
  const Real dp = gaugeRemovedMaxDifference(mesh, first.pressure, last.pressure);
  std::printf(
      "G3.2: max |du| %.3e, max |dF| / max|F| %.3e, gauge-removed |dp| / range %.3e "
      "(iterations %llu / %llu)\n",
      du, dF / maxF, dp / range(first.pressure), static_cast<unsigned long long>(first.iterations),
      static_cast<unsigned long long>(last.iterations));
  EXPECT_LE(du, 1e-7);
  EXPECT_LE(dF, 1e-7 * maxF);
  EXPECT_LE(dp, 1e-6 * range(first.pressure));
}

// G3.3 -- Rhie-Chow removes a checkerboard pressure the linear flux cannot see.
TEST(SIMPLE3D, RhieChowRemovesACheckerboardPressure) {
  const Index n = 8;
  const Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  for (const auto& patch : mesh.boundaryPatches()) {
    vb.set(mesh, patch.name(), std::make_unique<Wall>());
    pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  ScalarField checkerboard(mesh.numberOfCells());
  for (Index k = 0; k < n; ++k)
    for (Index j = 0; j < n; ++j)
      for (Index i = 0; i < n; ++i)
        checkerboard[((k * n) + j) * n + i] = ((i + j + k) % 2 == 0) ? 1.0 : -1.0;
  const auto amplitude = [&](const ScalarField& p) {
    Real mean = 0.0;
    for (Index i = 0; i < p.size(); ++i) mean += p[i];
    mean /= static_cast<Real>(p.size());
    Real sum = 0.0;
    for (Index i = 0; i < p.size(); ++i) sum += checkerboard[i] * (p[i] - mean);
    return std::abs(sum) / static_cast<Real>(p.size());  // uniform volumes
  };
  const FluidProperties fluid(1.0, 0.01);
  SIMPLESettings rc = settings3D(1e-12);
  const SIMPLEResult r =
      SIMPLE(rc, 0).solve(mesh, fluid, vb, pb, VectorField(mesh.numberOfCells()), checkerboard);
  ASSERT_TRUE(r.converged()) << static_cast<int>(r.status);
  Real maxU = 0.0;
  for (Index i = 0; i < r.velocity.size(); ++i) maxU = std::max(maxU, magnitude(r.velocity[i]));
  SIMPLESettings linear = settings3D(1e-12);
  linear.faceFlux = FaceFluxScheme::Linear;
  const SIMPLEResult l =
      SIMPLE(linear, 0).solve(mesh, fluid, vb, pb, VectorField(mesh.numberOfCells()), checkerboard);
  std::printf(
      "G3.3: Rhie-Chow: checkerboard amplitude %.3e, max |u| %.3e (%llu iterations); "
      "linear flux (reported): amplitude %.3e, status %d after %llu iterations\n",
      amplitude(r.pressure), maxU, static_cast<unsigned long long>(r.iterations),
      amplitude(l.pressure), static_cast<int>(l.status),
      static_cast<unsigned long long>(l.iterations));
  EXPECT_EQ(r.faceFlux, FaceFluxScheme::RhieChow);
  EXPECT_LE(amplitude(r.pressure), 1e-6);
  EXPECT_LE(maxU, 1e-8);
}

// G3.4' (a) -- on Cartesian hexahedra the non-orthogonal correction is exactly inert.
TEST(SIMPLE3D, NonOrthogonalCorrectionIsExactlyInertOnCartesianHexahedra) {
  const Mesh mesh = MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
  for (const auto& face : mesh.faces()) {
    const auto d = face.isBoundary() ? MeshGeometry::decomposeBoundaryFaceArea(mesh, face)
                                     : MeshGeometry::decomposeFaceArea(mesh, face);
    EXPECT_EQ(d.nonOrthogonal.x, 0.0);
    EXPECT_EQ(d.nonOrthogonal.y, 0.0);
    EXPECT_EQ(d.nonOrthogonal.z, 0.0);
  }
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  lidCubeBoundaries(mesh, vb, pb);
  const Index n = mesh.numberOfCells();
  // A non-trivial state: 30 SIMPLE iterations of the Re = 100 lid cube.
  const SIMPLEResult state =
      SIMPLE(settings3D(1e-30, 30), 0)
          .solve(mesh, FluidProperties(1.0, 0.01), vb, pb, VectorField(n), ScalarField(n, 0.0));
  const ScalarField mu(n, 0.01);
  for (const auto c : {VelocityComponent::U, VelocityComponent::V, VelocityComponent::W}) {
    ScalarField previous(n);
    for (Index i = 0; i < n; ++i)
      previous[i] = cfd::physics::velocityComponentValue(state.velocity[i], c);
    const auto off = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, state.velocity, state.pressure, state.massFlux, mu, vb, pb, c, previous, 0.7, nullptr,
        nullptr, ConvectionScheme::QUICK, GradientScheme::LeastSquares, false);
    const auto on = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, state.velocity, state.pressure, state.massFlux, mu, vb, pb, c, previous, 0.7, nullptr,
        nullptr, ConvectionScheme::QUICK, GradientScheme::LeastSquares, true);
    const auto& mOff = off.system.matrix();
    const auto& mOn = on.system.matrix();
    ASSERT_EQ(mOff.nonZeros(), mOn.nonZeros());
    for (Index k = 0; k < mOff.nonZeros(); ++k)
      EXPECT_EQ(mOff.valuesData()[k], mOn.valuesData()[k]);
    for (Index i = 0; i < n; ++i) EXPECT_EQ(off.system.rhs()[i], on.system.rhs()[i]);
  }
  ScalarField d(n, 0.01);
  ScalarField previousPPrime(n);
  for (Index i = 0; i < n; ++i) previousPPrime[i] = 1e-3 * std::sin(static_cast<Real>(i));
  cfd::pressure_velocity::PressureCorrectionOptions plain;
  cfd::pressure_velocity::PressureCorrectionOptions corrected;
  corrected.nonOrthogonal = true;
  corrected.gradientScheme = GradientScheme::LeastSquares;
  corrected.previousPressureCorrection = &previousPPrime;
  const auto pOff = cfd::pressure_velocity::assemblePressureCorrection(mesh, state.massFlux, d, d,
                                                                       1.0, 0, pb, plain, &d);
  const auto pOn = cfd::pressure_velocity::assemblePressureCorrection(mesh, state.massFlux, d, d,
                                                                      1.0, 0, pb, corrected, &d);
  ASSERT_EQ(pOff.system.matrix().nonZeros(), pOn.system.matrix().nonZeros());
  for (Index k = 0; k < pOff.system.matrix().nonZeros(); ++k)
    EXPECT_EQ(pOff.system.matrix().valuesData()[k], pOn.system.matrix().valuesData()[k]);
  for (Index i = 0; i < n; ++i) EXPECT_EQ(pOff.system.rhs()[i], pOn.system.rhs()[i]);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) EXPECT_EQ(pOn.explicitFaceFlux[f], 0.0);
}

// G3.4' (b) -- end to end, N = 2 vs N = 0 agree to the iterative tolerance.
TEST(SIMPLE3D, NonOrthogonalCorrectionPassesAgreeWithUncorrectedOnCartesianHexahedra) {
  const Mesh mesh = MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0);
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  lidCubeBoundaries(mesh, vb, pb);
  const Index n = mesh.numberOfCells();
  const FluidProperties fluid(1.0, 0.01);
  SIMPLESettings zero = settings3D(1e-11);
  SIMPLESettings two = settings3D(1e-11);
  two.nonOrthogonalCorrections = 2;
  const SIMPLEResult a =
      SIMPLE(zero, 0).solve(mesh, fluid, vb, pb, VectorField(n), ScalarField(n, 0.0));
  const SIMPLEResult b =
      SIMPLE(two, 0).solve(mesh, fluid, vb, pb, VectorField(n), ScalarField(n, 0.0));
  ASSERT_TRUE(a.converged());
  ASSERT_TRUE(b.converged());
  EXPECT_EQ(b.momentumPredictorPasses, 2 * b.iterations);
  Real du = 0.0;
  for (Index i = 0; i < n; ++i) du = std::max(du, magnitude(a.velocity[i] - b.velocity[i]));
  Real dF = 0.0;
  Real maxF = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    dF = std::max(dF, std::abs(a.massFlux[f] - b.massFlux[f]));
    maxF = std::max(maxF, std::abs(a.massFlux[f]));
  }
  const Real dp = gaugeRemovedMaxDifference(mesh, a.pressure, b.pressure);
  std::printf("G3.4'(b): max |du| %.3e, |dF|/max|F| %.3e, |dp|/range %.3e\n", du, dF / maxF,
              dp / range(a.pressure));
  EXPECT_LE(du, 1e-8);
  EXPECT_LE(dF, 1e-8 * maxF);
  EXPECT_LE(dp, 1e-8 * range(a.pressure));
}

// ------------------------------------------------------------------------------------------------
// Boundary conditions on all six surface orientations.

// Plug flow in +-x, +-y, +-z: velocity inlet on one face, pressure outlet (FixedValue 0) on the
// opposite one, symmetry on the four others. The uniform field with p = 0 is an exact discrete
// solution (no shear, no pressure gradient); SIMPLE must reach it from rest.
TEST(SIMPLE3D, PlugFlowIsExactInAllSixDirections) {
  const Mesh mesh = MeshGeometry::createCartesian3D(4, 5, 6, 1.2, 0.9, 1.5);
  const std::string axes = "xyz";
  const Real speed = 0.8;
  for (int axis = 0; axis < 3; ++axis) {
    for (const Real sign : {1.0, -1.0}) {
      const std::string inletPatch = std::string(1, axes[axis]) + (sign > 0.0 ? "min" : "max");
      const std::string outletPatch = std::string(1, axes[axis]) + (sign > 0.0 ? "max" : "min");
      SCOPED_TRACE(inletPatch + " -> " + outletPatch);
      Vector3 u{};
      (axis == 0 ? u.x : (axis == 1 ? u.y : u.z)) = sign * speed;
      BoundaryConditionSet vb;
      BoundaryConditionSet pb;
      for (const auto& patch : mesh.boundaryPatches()) {
        if (patch.name() == inletPatch) {
          vb.set(mesh, patch.name(), std::make_unique<Inlet>(u));
          pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
        } else if (patch.name() == outletPatch) {
          vb.set(mesh, patch.name(), std::make_unique<Outlet>());
          pb.set(mesh, patch.name(), std::make_unique<FixedValue>(0.0));
        } else {
          vb.set(mesh, patch.name(), std::make_unique<Symmetry>());
          pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
        }
      }
      const Index n = mesh.numberOfCells();
      const SIMPLEResult r =
          SIMPLE(settings3D(1e-12), 0)
              .solve(mesh, FluidProperties(1.0, 0.05), vb, pb, VectorField(n), ScalarField(n, 0.0));
      ASSERT_TRUE(r.converged()) << static_cast<int>(r.status);
      Real du = 0.0;
      Real dp = 0.0;
      for (Index i = 0; i < n; ++i) {
        du = std::max(du, magnitude(r.velocity[i] - u));
        dp = std::max(dp, std::abs(r.pressure[i]));
      }
      EXPECT_LE(du, 1e-9 * speed);
      EXPECT_LE(dp, 1e-9 * speed * speed);
      const auto balance = cfd::physics::computeMassBalance(mesh, r.massFlux);
      EXPECT_LE(balance.relativeImbalance, 1e-10);
    }
  }
}

// Couette flow with the moving wall on each of three axes (the cyclic permutations of: ymax moving
// +x over a ymin wall, zero-gradient outlets on the x faces, symmetry on the z faces). The linear
// profile u = U y / H is an exact discrete solution.
TEST(SIMPLE3D, CouetteFlowIsExactForMovingWallsOnThreeAxes) {
  const Real speed = 1.0;
  for (int rotation = 0; rotation < 3; ++rotation) {
    // rotation 0: wall normal y, motion x, open x, symmetry z; then cyclic (x->y->z->x).
    const int normal = (1 + rotation) % 3;
    const int motion = rotation % 3;
    const int open = motion;
    const int symmetric = (2 + rotation) % 3;
    const std::string axes = "xyz";
    SCOPED_TRACE(std::string("moving wall ") + axes[normal] + "max moving +" + axes[motion]);
    Index cells[3] = {3, 3, 3};
    cells[normal] = 6;
    const Mesh mesh = MeshGeometry::createCartesian3D(cells[0], cells[1], cells[2], 1.0, 1.0, 1.0);
    Vector3 wall{};
    (motion == 0 ? wall.x : (motion == 1 ? wall.y : wall.z)) = speed;
    BoundaryConditionSet vb;
    BoundaryConditionSet pb;
    for (const auto& patch : mesh.boundaryPatches()) {
      const int axis = patch.name()[0] - 'x';
      const bool isMax = patch.name().substr(1) == "max";
      if (axis == normal) {
        if (isMax)
          vb.set(mesh, patch.name(), std::make_unique<MovingWall>(wall));
        else
          vb.set(mesh, patch.name(), std::make_unique<Wall>());
      } else if (axis == open) {
        vb.set(mesh, patch.name(), std::make_unique<Outlet>());
      } else {
        ASSERT_EQ(axis, symmetric);
        vb.set(mesh, patch.name(), std::make_unique<Symmetry>());
      }
      pb.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
    }
    const Index n = mesh.numberOfCells();
    const SIMPLEResult r =
        SIMPLE(settings3D(1e-12), 0)
            .solve(mesh, FluidProperties(1.0, 0.1), vb, pb, VectorField(n), ScalarField(n, 0.0));
    ASSERT_TRUE(r.converged()) << static_cast<int>(r.status);
    Real err = 0.0;
    for (const auto& c : mesh.cells()) {
      const Vector3& x = c.centroid();
      const Real h = normal == 0 ? x.x : (normal == 1 ? x.y : x.z);
      Vector3 exact{};
      (motion == 0 ? exact.x : (motion == 1 ? exact.y : exact.z)) = speed * h;
      err = std::max(err, magnitude(r.velocity[c.id()] - exact));
    }
    EXPECT_LE(err, 1e-8 * speed);
  }
}
