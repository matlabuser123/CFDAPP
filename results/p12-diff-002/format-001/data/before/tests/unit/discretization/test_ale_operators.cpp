// P12-MESH-007: the ALE operators -- the moving-mesh implicit Euler time
// derivative, the mass flux relative to the moving mesh, and the shared
// convection assembly driven by it (results/p12-mesh-007/acceptance_gate.md
// G1.4 as amended by A1, G6.1, G6.5 as amended by A2).
//
// Kinds of check: G1.4 is a REGRESSION/consistency check (the static limit is
// bit for bit the static operators); G6.1 is VERIFICATION against values
// derived by hand (see each test's comment); G6.5 verifies the exact discrete
// identity behind uniform-flow preservation (a uniform field is an exact
// solution of the GCL-consistent ALE operator) on a moving 3D mesh, with two
// GCL-inconsistent variants as negative controls.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "MeshMotionCases.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/TimeDerivative.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"

namespace {

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshMotion;
using cfd::physics::FluidProperties;
using cfd::physics::VelocityComponent;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

bool sameMatrix(const SparseMatrix& a, const SparseMatrix& b) {
  if (a.rows() != b.rows() || a.nonZeros() != b.nonZeros()) return false;
  return std::memcmp(a.rowOffsetsData(), b.rowOffsetsData(), (a.rows() + 1) * sizeof(Index)) == 0 &&
         std::memcmp(a.columnIndicesData(), b.columnIndicesData(), a.nonZeros() * sizeof(Index)) == 0 &&
         std::memcmp(a.valuesData(), b.valuesData(), a.nonZeros() * sizeof(Real)) == 0;
}

bool sameVector(const Vector& a, const Vector& b) {
  if (a.size() != b.size()) return false;
  for (Index i = 0; i < a.size(); ++i) {
    if (!sameBits(a[i], b[i])) return false;
  }
  return true;
}

Real entry(const SparseMatrix& m, Index row, Index column) {
  for (Index k = m.rowOffsetsData()[row]; k < m.rowOffsetsData()[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == column) return m.valuesData()[k];
  }
  return 0.0;
}

// A deterministic, non-trivial cell field: fixed pseudo-random values.
ScalarField pattern(Index n, Real scale) {
  ScalarField f(n);
  for (Index i = 0; i < n; ++i) {
    f[i] = scale * (1.0 + (0.37 * std::sin(1.7 * static_cast<Real>(i))));
  }
  return f;
}

VectorField velocityPattern(const Mesh& mesh) {
  VectorField u(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Vector3& c = cell.centroid();
    u[cell.id()] = Vector3{std::sin(3.0 * c.y) + 0.2, 0.3 * std::cos(2.0 * c.x),
                           mesh.dimension() == 3 ? 0.1 * std::sin(c.x + c.z) : 0.0};
  }
  return u;
}

BoundaryConditionSet inletEverywhere(const Mesh& mesh, const Vector3& u0) {
  BoundaryConditionSet set;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Inlet>(u0));
  }
  return set;
}

BoundaryConditionSet zeroGradient(const Mesh& mesh) {
  BoundaryConditionSet set;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return set;
}

// --- G1.4 (Amendment A1) -----------------------------------------------------------------

TEST(AleOperatorsStaticLimit, TimeDerivativeWithUnchangedVolumesIsTheStaticOne) {
  for (const auto& [name, make] : std::vector<std::pair<std::string, Mesh (*)()>>{
           {"C16", &m7::c16}, {"Q16", &m7::q16}, {"H8", &m7::h8}}) {
    const Mesh mesh = make();
    const ScalarField phi = pattern(mesh.numberOfCells(), 1.3);
    ScalarField volumes(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) volumes[cell.id()] = cell.volume();
    const auto stat = cfd::discretization::implicitEulerTimeDerivative(mesh, phi, 1.0, 0.01);
    const auto ale = cfd::discretization::aleImplicitEulerTimeDerivative(mesh, phi, volumes, 1.0, 0.01);
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      EXPECT_TRUE(sameBits(stat.diagonal[c], ale.diagonal[c])) << name;
      EXPECT_TRUE(sameBits(stat.source[c], ale.source[c])) << name;
    }
    std::printf("G1.4 %s: aleImplicitEulerTimeDerivative(V^n = V) bitwise = implicitEulerTimeDerivative\n",
                name.c_str());
  }
}

TEST(AleOperatorsStaticLimit, RelativeFluxWithoutMeshMotionIsTheFlux) {
  for (const auto& [name, make] : std::vector<std::pair<std::string, Mesh (*)()>>{
           {"C16", &m7::c16}, {"Q16", &m7::q16}, {"H8", &m7::h8}}) {
    const Mesh mesh = make();
    const FluidProperties fluid(1.0, 0.01);
    const BoundaryConditionSet bcs = inletEverywhere(mesh, Vector3{1.0, 0.5, 0.0});
    const SurfaceField flux = cfd::physics::calculateMassFlux(mesh, velocityPattern(mesh), fluid, bcs);
    const SurfaceField relative =
        cfd::physics::relativeMassFlux(flux, SurfaceField(mesh.numberOfFaces(), 0.0), 1.0);
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) EXPECT_TRUE(sameBits(flux[f], relative[f])) << name;
    std::printf("G1.4 %s: relativeMassFlux(F, 0) bitwise = F\n", name.c_str());
  }
}

TEST(AleOperatorsStaticLimit, AleMomentumAssemblyWithoutMotionIsTheStaticAssembly) {
  for (const auto& [name, make] : std::vector<std::pair<std::string, Mesh (*)()>>{
           {"C16", &m7::c16}, {"Q16", &m7::q16}}) {
    const Mesh mesh = make();
    const FluidProperties fluid(1.0, 0.01);
    BoundaryConditionSet velocityBcs;
    velocityBcs.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
    velocityBcs.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
    velocityBcs.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
    velocityBcs.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
    const BoundaryConditionSet pressureBcs = zeroGradient(mesh);
    const VectorField u = velocityPattern(mesh);
    const ScalarField p = pattern(mesh.numberOfCells(), 0.2);
    const SurfaceField flux = cfd::physics::calculateMassFlux(mesh, u, fluid, velocityBcs);
    const ScalarField mu(mesh.numberOfCells(), 0.01);
    ScalarField volumes(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) volumes[cell.id()] = cell.volume();
    const SurfaceField relative =
        cfd::physics::relativeMassFlux(flux, SurfaceField(mesh.numberOfFaces(), 0.0), 1.0);
    for (const VelocityComponent component : {VelocityComponent::U, VelocityComponent::V}) {
      ScalarField previous(mesh.numberOfCells());
      for (Index c = 0; c < mesh.numberOfCells(); ++c) {
        previous[c] = component == VelocityComponent::U ? u[c].x : u[c].y;
      }
      const auto stat = cfd::pressure_velocity::assembleTransientMomentumComponent(
          mesh, u, p, flux, fluid, mu, velocityBcs, pressureBcs, component, previous, 0.01);
      const auto ale = cfd::pressure_velocity::assembleAleTransientMomentumComponent(
          mesh, u, p, relative, fluid, mu, velocityBcs, pressureBcs, component, previous, volumes, 0.01);
      EXPECT_TRUE(sameMatrix(stat.system.matrix(), ale.system.matrix())) << name;
      EXPECT_TRUE(sameVector(stat.system.rhs(), ale.system.rhs())) << name;
      EXPECT_TRUE(sameVector(stat.diagonal, ale.diagonal)) << name;
    }
    std::printf("G1.4 %s: ALE momentum assembly (no motion) bitwise = the effective-viscosity "
                "static assembly, U and V\n",
                name.c_str());
  }
}

TEST(AleOperatorsStaticLimit, BothMomentumAssembliesRefuseA3DMesh) {
  const Mesh mesh = m7::h8();
  const FluidProperties fluid(1.0, 0.01);
  const BoundaryConditionSet velocityBcs = inletEverywhere(mesh, Vector3{1.0, 0.0, 0.0});
  const BoundaryConditionSet pressureBcs = zeroGradient(mesh);
  const VectorField u(mesh.numberOfCells(), Vector3{1.0, 0.0, 0.0});
  const ScalarField p(mesh.numberOfCells(), 0.0);
  const ScalarField mu(mesh.numberOfCells(), 0.01);
  const ScalarField previous(mesh.numberOfCells(), 1.0);
  ScalarField volumes(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) volumes[cell.id()] = cell.volume();
  const SurfaceField flux = cfd::physics::calculateMassFlux(mesh, u, fluid, velocityBcs);
  EXPECT_THROW((void)cfd::pressure_velocity::assembleTransientMomentumComponent(
                   mesh, u, p, flux, fluid, mu, velocityBcs, pressureBcs, VelocityComponent::U,
                   previous, 0.01),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::pressure_velocity::assembleAleTransientMomentumComponent(
                   mesh, u, p, flux, fluid, mu, velocityBcs, pressureBcs, VelocityComponent::U,
                   previous, volumes, 0.01),
               InvalidArgumentError);
}

// The 3D static limit of the operators the ALE momentum is made of (A1).
TEST(AleOperatorsStaticLimit, ThreeDimensionalConvectionAndTimeTermStaticLimit) {
  const Mesh mesh = m7::h8();
  const FluidProperties fluid(1.0, 0.01);
  const BoundaryConditionSet bcs = inletEverywhere(mesh, Vector3{1.0, 0.5, -0.25});
  const VectorField u = velocityPattern(mesh);
  const SurfaceField flux = cfd::physics::calculateMassFlux(mesh, u, fluid, bcs);
  const SurfaceField relative =
      cfd::physics::relativeMassFlux(flux, SurfaceField(mesh.numberOfFaces(), 0.0), 1.0);
  ScalarField volumes(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) volumes[cell.id()] = cell.volume();
  const Index n = mesh.numberOfCells();
  for (const VelocityComponent component :
       {VelocityComponent::U, VelocityComponent::V, VelocityComponent::W}) {
    ScalarField previous(n);
    for (Index c = 0; c < n; ++c) {
      previous[c] = component == VelocityComponent::U ? u[c].x
                                                      : (component == VelocityComponent::V ? u[c].y : u[c].z);
    }
    SparseMatrixBuilder staticBuilder(n, n);
    Vector staticRhs(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, flux, u, bcs, component, staticBuilder, staticRhs);
    SparseMatrixBuilder aleBuilder(n, n);
    Vector aleRhs(n, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, relative, u, bcs, component, aleBuilder, aleRhs);
    EXPECT_TRUE(sameMatrix(staticBuilder.build(), aleBuilder.build()));
    EXPECT_TRUE(sameVector(staticRhs, aleRhs));
    const auto stat = cfd::discretization::implicitEulerTimeDerivative(mesh, previous, 1.0, 0.01);
    const auto ale = cfd::discretization::aleImplicitEulerTimeDerivative(mesh, previous, volumes, 1.0, 0.01);
    Vector d1(n), s1(n), d2(n), s2(n);
    for (Index c = 0; c < n; ++c) {
      d1[c] = stat.diagonal[c];
      s1[c] = stat.source[c];
      d2[c] = ale.diagonal[c];
      s2[c] = ale.source[c];
    }
    cfd::pressure_velocity::applyTransientTerm(staticBuilder, staticRhs, d1, s1);
    cfd::pressure_velocity::applyTransientTerm(aleBuilder, aleRhs, d2, s2);
    EXPECT_TRUE(sameMatrix(staticBuilder.build(), aleBuilder.build()));
    EXPECT_TRUE(sameVector(staticRhs, aleRhs));
  }
  std::printf("G1.4 H8: convection with relativeMassFlux(F, 0) and the ALE time term (V^n = V) "
              "bitwise = the static operators, U, V and W\n");
}

// --- G6.1 hand-derived ----------------------------------------------------------------------

// (a) The unit cell [0,1]^2 whose east edge moves from x = 1 to x = 1.1 during
// dt = 0.5 (x = X + 0.2 tau X e_x), rho = 1, uniform fluid velocity (1, 0),
// Inlet (1, 0) on every patch. By hand:
//   swept volumes (left, right, bottom, top) = (0, 0.1, 0, 0) (the bottom and
//     top edges only stretch along themselves); phi_m = (0, 0.2, 0, 0);
//   V^n = 1, V^{n+1} = 1.1; F = rho u . S = (-1, 1, 0, 0) on the new geometry;
//   F_rel = F - rho phi_m = (-1, 0.8, 0, 0);
//   U row: convection -- inflow through the left face: rhs += 1 * 1; outflow
//     through the right face: diag += 0.8; time -- diag += 1.1 / 0.5 = 2.2,
//     rhs += (1 / 0.5) * 1 = 2  =>  diag 3.0, rhs 3.0 (u^{n+1} = 1: uniform).
TEST(AleOperatorsHandDerived, UnitCellWithAMovingEastFace) {
  Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  m7::Matrix g{};
  g[0][0] = 0.2;
  MeshMotion motion(mesh, std::make_shared<cfd::mesh::AffineMotion>(g, Vector3{}, Vector3{}));
  const auto& step = motion.advance(0.5);
  const Real tol = 1e-14;
  // Face order: left, right (vertical), bottom, top (horizontal).
  const Real expectedSwept[4] = {0.0, 0.1, 0.0, 0.0};
  for (Index f = 0; f < 4; ++f) {
    EXPECT_NEAR(step.sweptVolumes[f], expectedSwept[f], tol) << f;
    EXPECT_NEAR(step.meshVolumeFlux[f], expectedSwept[f] / 0.5, tol) << f;
  }
  EXPECT_NEAR(mesh.cell(0).volume(), 1.1, tol);
  EXPECT_NEAR(step.previousVolumes[0], 1.0, tol);
  const FluidProperties fluid(1.0, 0.01);
  const BoundaryConditionSet bcs = inletEverywhere(mesh, Vector3{1.0, 0.0, 0.0});
  const VectorField u(1, Vector3{1.0, 0.0, 0.0});
  const SurfaceField flux = cfd::physics::calculateMassFlux(mesh, u, fluid, bcs);
  SurfaceField meshFlux(4);
  for (Index f = 0; f < 4; ++f) meshFlux[f] = step.meshVolumeFlux[f];
  const SurfaceField relative = cfd::physics::relativeMassFlux(flux, meshFlux, 1.0);
  const Real expectedRelative[4] = {-1.0, 0.8, 0.0, 0.0};
  for (Index f = 0; f < 4; ++f) EXPECT_NEAR(relative[f], expectedRelative[f], tol) << f;
  ScalarField previousVolume(1, step.previousVolumes[0]);
  const auto timeTerm = cfd::discretization::aleImplicitEulerTimeDerivative(mesh, ScalarField(1, 1.0),
                                                                        previousVolume, 1.0, 0.5);
  EXPECT_NEAR(timeTerm.diagonal[0], 2.2, tol);
  EXPECT_NEAR(timeTerm.source[0], 2.0, tol);
  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  cfd::physics::assembleConvectionContribution(mesh, relative, u, bcs, VelocityComponent::U, builder, rhs);
  cfd::pressure_velocity::applyTransientTerm(builder, rhs, Vector(1, timeTerm.diagonal[0]),
                                             Vector(1, timeTerm.source[0]));
  const SparseMatrix a = builder.build();
  EXPECT_NEAR(a.diagonal(0), 3.0, tol);
  EXPECT_NEAR(rhs[0], 3.0, tol);
  std::printf("G6.1(a): swept (%.17g, %.17g, %.17g, %.17g), F_rel (%.17g, %.17g, %.17g, %.17g), "
              "row diag %.17g rhs %.17g (hand: 3, 3)\n",
              step.sweptVolumes[0], step.sweptVolumes[1], step.sweptVolumes[2], step.sweptVolumes[3],
              relative[0], relative[1], relative[2], relative[3], a.diagonal(0), rhs[0]);
}

// A test motion that moves only the vertices on x = 1 (the internal face of a
// 2 x 1 mesh on [0,2] x [0,1]) with speed w in x.
class MiddleFaceMotion final : public cfd::mesh::PrescribedMotion {
 public:
  explicit MiddleFaceMotion(Real speed) : speed_(speed) {}
  Vector3 position(const Vector3& x, Real elapsed) const override {
    return x.x == 1.0 ? Vector3{x.x + (speed_ * elapsed), x.y, x.z} : x;
  }
  std::string description() const override { return "middle face"; }

 private:
  Real speed_;
};

// (b) The 2 x 1 mesh (cells [0,1] and [1,2] x [0,1]) whose internal face moves
// from x = 1 to x = 1.25 (speed 0.5, dt = 0.5); rho = 1, uniform fluid (uf, 0),
// Inlet (uf, 0) everywhere. By hand: V^n = (1, 1), V^{n+1} = (1.25, 0.75);
// the internal face (owner 0 -> neighbor 1, S = (1, 0)) sweeps +0.25, phi_m =
// 0.5, F = uf, F_rel = uf - 0.5.
//   uf = 1.0: F_rel = +0.5, upwind = the owner:
//     row 0: A00 = 2.5 + 0.5 = 3.0, A01 = 0,    rhs0 = 2 * 1 + 1 * 1 = 3;
//     row 1: A11 = 1.5 + 1.0 = 2.5, A10 = -0.5, rhs1 = 2 * 1 = 2.
//   uf = 0.2: F_rel = -0.3, the mesh motion REVERSES the upwind direction
//   (the static flux 0.2 would pick the owner): upwind = the neighbor:
//     row 0: A00 = 2.5, A01 = -0.3, rhs0 = 2 * 0.2 + 0.2 * 0.2 = 0.44;
//     row 1: A11 = 1.5 + 0.3 + 0.2 = 2.0, A10 = 0, rhs1 = 2 * 0.2 = 0.4.
// (Time: diag V^{n+1}/dt = 2.5 / 1.5, source (V^n/dt) phi^n = 2 phi^n; the right
// boundary face is an outflow F = uf, the left an inflow bringing uf * uf.)
// Both give the uniform field back exactly.
TEST(AleOperatorsHandDerived, TwoCellsWithAMovingInternalFaceIncludingUpwindReversal) {
  const Real tol = 1e-14;
  for (const Real uf : {1.0, 0.2}) {
    Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
    MeshMotion motion(mesh, std::make_shared<MiddleFaceMotion>(0.5));
    const auto& step = motion.advance(0.5);
    EXPECT_NEAR(step.sweptVolumes[1], 0.25, tol);
    EXPECT_NEAR(mesh.cell(0).volume(), 1.25, tol);
    EXPECT_NEAR(mesh.cell(1).volume(), 0.75, tol);
    const FluidProperties fluid(1.0, 0.01);
    const BoundaryConditionSet bcs = inletEverywhere(mesh, Vector3{uf, 0.0, 0.0});
    const VectorField u(2, Vector3{uf, 0.0, 0.0});
    SurfaceField meshFlux(mesh.numberOfFaces());
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) meshFlux[f] = step.meshVolumeFlux[f];
    const SurfaceField relative = cfd::physics::relativeMassFlux(
        cfd::physics::calculateMassFlux(mesh, u, fluid, bcs), meshFlux, 1.0);
    EXPECT_NEAR(relative[1], uf - 0.5, tol);
    ScalarField previousVolume(2);
    previousVolume[0] = step.previousVolumes[0];
    previousVolume[1] = step.previousVolumes[1];
    const auto timeTerm = cfd::discretization::aleImplicitEulerTimeDerivative(mesh, ScalarField(2, uf),
                                                                          previousVolume, 1.0, 0.5);
    SparseMatrixBuilder builder(2, 2);
    Vector rhs(2, 0.0);
    cfd::physics::assembleConvectionContribution(mesh, relative, u, bcs, VelocityComponent::U, builder, rhs);
    Vector d(2), s(2);
    d[0] = timeTerm.diagonal[0];
    d[1] = timeTerm.diagonal[1];
    s[0] = timeTerm.source[0];
    s[1] = timeTerm.source[1];
    cfd::pressure_velocity::applyTransientTerm(builder, rhs, d, s);
    const SparseMatrix a = builder.build();
    if (uf == 1.0) {
      EXPECT_NEAR(entry(a, 0, 0), 3.0, tol);
      EXPECT_NEAR(entry(a, 0, 1), 0.0, tol);
      EXPECT_NEAR(entry(a, 1, 1), 2.5, tol);
      EXPECT_NEAR(entry(a, 1, 0), -0.5, tol);
      EXPECT_NEAR(rhs[0], 3.0, tol);
      EXPECT_NEAR(rhs[1], 2.0, tol);
    } else {
      EXPECT_NEAR(entry(a, 0, 0), 2.5, tol);
      EXPECT_NEAR(entry(a, 0, 1), -0.3, tol);
      EXPECT_NEAR(entry(a, 1, 1), 2.0, tol);
      EXPECT_NEAR(entry(a, 1, 0), 0.0, tol);
      EXPECT_NEAR(rhs[0], 0.44, tol);
      EXPECT_NEAR(rhs[1], 0.4, tol);
    }
    // The uniform field solves the assembled system (GCL-consistent operator).
    const Vector residual = a.multiply(Vector(2, uf));
    EXPECT_NEAR(residual[0], rhs[0], tol);
    EXPECT_NEAR(residual[1], rhs[1], tol);
    std::printf("G6.1(b) uf = %.1f: F_rel = %.17g, A = [[%.17g, %.17g], [%.17g, %.17g]], rhs = (%.17g, "
                "%.17g)\n",
                uf, relative[1], entry(a, 0, 0), entry(a, 0, 1), entry(a, 1, 0), entry(a, 1, 1),
                rhs[0], rhs[1]);
  }
}

// --- G6.5 (Amendment A2): 3D, operator level ---------------------------------------------------

struct OperatorResidual {
  Real ale{0.0};
  Real noMeshFlux{0.0};  // N1
  Real noVolumeChange{0.0};  // N2
  Real staticFormulation{0.0};  // recorded only
};

// One advance t = 0.1 -> 0.12 of H8 under `prescribed`; uniform u0; F^n on the
// mesh at t^n; residual of the assembled time + convection system at u0,
// normalised by rho V_P |u0| / dt, max over cells and components.
OperatorResidual uniformResidual3D(const m7::Motion& prescribed) {
  Mesh mesh = m7::h8();
  MeshMotion motion(mesh, prescribed);
  (void)motion.advance(0.1);
  const Vector3 u0{1.0, 0.5, -0.25};
  const FluidProperties fluid(1.0, 0.01);
  const BoundaryConditionSet bcs = inletEverywhere(mesh, u0);
  const VectorField u(mesh.numberOfCells(), u0);
  const SurfaceField fluxN = cfd::physics::calculateMassFlux(mesh, u, fluid, bcs);  // at t^n
  const Real dt = 0.02;
  const auto& step = motion.advance(0.1 + dt);
  const Index n = mesh.numberOfCells();
  SurfaceField meshFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) meshFlux[f] = step.meshVolumeFlux[f];
  const SurfaceField relative = cfd::physics::relativeMassFlux(fluxN, meshFlux, 1.0);
  ScalarField previousVolume(n);
  for (Index c = 0; c < n; ++c) previousVolume[c] = step.previousVolumes[c];
  const Real speed = magnitude(u0);
  OperatorResidual result;
  for (const VelocityComponent component :
       {VelocityComponent::U, VelocityComponent::V, VelocityComponent::W}) {
    const Real phi0 = component == VelocityComponent::U ? u0.x : (component == VelocityComponent::V ? u0.y : u0.z);
    const ScalarField phiOld(n, phi0);
    const auto residual = [&](const SurfaceField& convecting, bool aleTime) {
      SparseMatrixBuilder builder(n, n);
      Vector rhs(n, 0.0);
      cfd::physics::assembleConvectionContribution(mesh, convecting, u, bcs, component, builder, rhs);
      const auto timeTerm = aleTime ? cfd::discretization::aleImplicitEulerTimeDerivative(
                                      mesh, phiOld, previousVolume, 1.0, dt)
                                : cfd::discretization::implicitEulerTimeDerivative(mesh, phiOld, 1.0, dt);
      Vector d(n), s(n);
      for (Index c = 0; c < n; ++c) {
        d[c] = timeTerm.diagonal[c];
        s[c] = timeTerm.source[c];
      }
      cfd::pressure_velocity::applyTransientTerm(builder, rhs, d, s);
      const Vector au = builder.build().multiply(Vector(n, phi0));
      Real worst = 0.0;
      for (Index c = 0; c < n; ++c) {
        worst = std::max(worst, std::abs(rhs[c] - au[c]) / (mesh.cell(c).volume() * speed / dt));
      }
      return worst;
    };
    result.ale = std::max(result.ale, residual(relative, true));
    result.noMeshFlux = std::max(result.noMeshFlux, residual(fluxN, true));
    result.noVolumeChange = std::max(result.noVolumeChange, residual(relative, false));
    result.staticFormulation = std::max(result.staticFormulation, residual(fluxN, false));
  }
  return result;
}

TEST(AleOperators3D, UniformFieldIsAnExactSolutionOfTheAleOperator) {
  for (const auto& [name, motion] : std::vector<std::pair<std::string, m7::Motion>>{
           {"H8/SN3", m7::sn3()}, {"H8/EX3", m7::affine(m7::ex3Spec())}}) {
    const OperatorResidual r = uniformResidual3D(motion);
    EXPECT_LE(r.ale, 1e-10) << name;
    EXPECT_GE(r.noMeshFlux, 1e-4) << name;
    EXPECT_GE(r.noVolumeChange, 1e-4) << name;
    std::printf("G6.5 %s: relative residual of the uniform field -- ALE %.3e (<= 1e-10); N1 (no mesh "
                "flux) %.3e, N2 (V^{n+1} both levels) %.3e (each >= 1e-4); static formulation %.3e "
                "(recorded)\n",
                name.c_str(), r.ale, r.noMeshFlux, r.noVolumeChange, r.staticFormulation);
  }
}

}  // namespace
