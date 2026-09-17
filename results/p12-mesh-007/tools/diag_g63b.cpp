// P12-MESH-007 diagnosis of the G6.3 failure, part 2: replicate step 1 of the fixed cavity (A) and of
// the translating cavity (B) stage by stage with the public building blocks, and print where the two
// first differ (relative flux, predictor u*, predictor flux F*, p'1, u1). Evidence only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"

using namespace cfd;
using physics::VelocityComponent;

int main() {
  const Vector3 b{0.5, 0.25, 0.0};
  const Real dt = 0.01;
  const physics::FluidProperties fluid(1.0, 0.01);
  algebra::LinearSolverSettings ls;
  ls.absoluteTolerance = 1e-15;
  ls.relativeTolerance = 1e-12;
  ls.maxIterations = 20000;
  const algebra::BiCGSTAB solver(ls);
  const mesh::Mesh meshA = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  mesh::Mesh meshB = mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0);
  boundary::BoundaryConditionSet vA, pA, vB, pB;
  vA.set(meshA, "left", std::make_unique<boundary::Wall>());
  vA.set(meshA, "right", std::make_unique<boundary::Wall>());
  vA.set(meshA, "bottom", std::make_unique<boundary::Wall>());
  vA.set(meshA, "top", std::make_unique<boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}));
  vB.set(meshB, "left", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "right", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "bottom", std::make_unique<boundary::MovingWall>(b));
  vB.set(meshB, "top", std::make_unique<boundary::MovingWall>(b + Vector3{1.0, 0.0, 0.0}));
  for (const char* p : {"left", "right", "bottom", "top"}) {
    pA.set(meshA, p, std::make_unique<boundary::FixedGradient>(0.0));
    pB.set(meshB, p, std::make_unique<boundary::FixedGradient>(0.0));
  }
  const Index n = meshA.numberOfCells();
  const fields::VectorField uA0(n, Vector3{});
  const fields::VectorField uB0(n, b);
  const fields::ScalarField p0(n, 0.0);
  const fields::SurfaceField fA0 = physics::calculateMassFlux(meshA, uA0, fluid, vA);
  const fields::SurfaceField fB0 = physics::calculateMassFlux(meshB, uB0, fluid, vB);
  mesh::MeshMotion motion(meshB, std::make_shared<mesh::AffineMotion>(mesh::AffineMotion::Matrix{}, Vector3{}, b));
  const auto& step = motion.advance(dt);
  fields::SurfaceField meshFlux(meshB.numberOfFaces());
  for (Index f = 0; f < meshB.numberOfFaces(); ++f) meshFlux[f] = step.meshVolumeFlux[f];
  const fields::SurfaceField fRel = physics::relativeMassFlux(fB0, meshFlux, 1.0);
  Real maxRel = 0.0;
  for (Index f = 0; f < meshB.numberOfFaces(); ++f) maxRel = std::max(maxRel, std::abs(fRel[f] - fA0[f]));
  std::printf("step 1 convecting flux: max |F_rel,B - F_A| = %.3e\n", maxRel);
  fields::ScalarField vol(n);
  for (Index c = 0; c < n; ++c) vol[c] = step.previousVolumes[c];
  const fields::ScalarField mu(n, 0.01);
  const auto comp = [&](const fields::VectorField& u, VelocityComponent c) {
    fields::ScalarField s(n);
    for (Index i = 0; i < n; ++i) s[i] = c == VelocityComponent::U ? u[i].x : u[i].y;
    return s;
  };
  const auto vec = [&](const fields::ScalarField& s) {
    algebra::Vector v(n);
    for (Index i = 0; i < n; ++i) v[i] = s[i];
    return v;
  };
  // Predictors.
  fields::VectorField uStarA(n), uStarB(n);
  fields::ScalarField dUA(n), dVA(n), dUB(n), dVB(n);
  for (const VelocityComponent c : {VelocityComponent::U, VelocityComponent::V}) {
    const auto asmA = pressure_velocity::assembleTransientMomentumComponent(meshA, uA0, p0, fA0, fluid, mu, vA, pA,
                                                                             c, comp(uA0, c), dt);
    const auto asmB = pressure_velocity::assembleAleTransientMomentumComponent(
        meshB, uB0, p0, fRel, fluid, mu, vB, pB, c, comp(uB0, c), vol, dt);
    const auto solA = solver.solve(asmA.system, vec(comp(uA0, c)));
    const auto solB = solver.solve(asmB.system, vec(comp(uB0, c)));
    const Real shift = c == VelocityComponent::U ? b.x : b.y;
    Real maxDiff = 0.0, maxDiag = 0.0;
    Index worst = 0;
    for (Index i = 0; i < n; ++i) {
      const Real d = std::abs((solB.solution[i] - shift) - solA.solution[i]);
      if (d > maxDiff) {
        maxDiff = d;
        worst = i;
      }
      maxDiag = std::max(maxDiag, std::abs(asmA.diagonal[i] - asmB.diagonal[i]));
    }
    // A * (shift) vs the extra RHS: row sums of B's matrix times shift against rhs_B - rhs_A.
    const algebra::Vector ones(n, shift);
    const algebra::Vector aShift = asmB.system.matrix().multiply(ones);
    Real maxShiftResidual = 0.0;
    Index worstRow = 0;
    for (Index i = 0; i < n; ++i) {
      const Real r = std::abs((asmB.system.rhs()[i] - asmA.system.rhs()[i]) - aShift[i]);
      if (r > maxShiftResidual) {
        maxShiftResidual = r;
        worstRow = i;
      }
    }
    std::printf("%s predictor: iterations A %zu B %zu; max |u*_B - b - u*_A| %.3e at cell (%zu,%zu); max |diag_A - "
                "diag_B| %.3e; max |(rhs_B - rhs_A) - A_B (shift)| %.3e at cell (%zu,%zu)\n",
                c == VelocityComponent::U ? "U" : "V", solA.iterations, solB.iterations, maxDiff, worst % 16,
                worst / 16, maxDiag, maxShiftResidual, worstRow % 16, worstRow / 16);
    for (Index i = 0; i < n; ++i) {
      if (c == VelocityComponent::U) {
        uStarA[i].x = solA.solution[i];
        uStarB[i].x = solB.solution[i];
        dUA[i] = meshA.cell(i).volume() / asmA.diagonal[i];
        dUB[i] = meshB.cell(i).volume() / asmB.diagonal[i];
      } else {
        uStarA[i].y = solA.solution[i];
        uStarB[i].y = solB.solution[i];
        dVA[i] = meshA.cell(i).volume() / asmA.diagonal[i];
        dVB[i] = meshB.cell(i).volume() / asmB.diagonal[i];
      }
    }
  }
  const fields::SurfaceField fStarA = physics::calculateMassFlux(meshA, uStarA, fluid, vA);
  const fields::SurfaceField fStarB = physics::calculateMassFlux(meshB, uStarB, fluid, vB);
  const auto contA = physics::evaluateContinuity(meshA, fStarA);
  const auto contB = physics::evaluateContinuity(meshB, fStarB);
  Real maxImb = 0.0;
  for (Index i = 0; i < n; ++i) maxImb = std::max(maxImb, std::abs(contA.cellImbalance[i] - contB.cellImbalance[i]));
  std::printf("predictor flux imbalance per cell: max |imb_A - imb_B| = %.3e\n", maxImb);
  // Check the wall faces of B: flux of F* against rho b . S.
  Real maxWall = 0.0;
  for (const auto& patch : meshB.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      const Real expected = dot(patch.name() == "top" ? b + Vector3{1.0, 0.0, 0.0} : b, meshB.face(f).areaVector());
      maxWall = std::max(maxWall, std::abs(fStarB[f] - expected));
    }
  }
  std::printf("B wall predictor fluxes vs rho V_w . S: max diff %.3e\n", maxWall);
  // Pressure correction 1, velocity correction 1.
  const auto pcA = pressure_velocity::assemblePressureCorrection(meshA, fStarA, dUA, dVA, 1.0, 0, pA);
  const auto pcB = pressure_velocity::assemblePressureCorrection(meshB, fStarB, dUB, dVB, 1.0, 0, pB);
  const auto ppA = solver.solve(pcA.system);
  const auto ppB = solver.solve(pcB.system);
  fields::ScalarField pPrimeA(n), pPrimeB(n);
  Real maxPP = 0.0;
  for (Index i = 0; i < n; ++i) {
    pPrimeA[i] = ppA.solution[i];
    pPrimeB[i] = ppB.solution[i];
    maxPP = std::max(maxPP, std::abs(pPrimeA[i] - pPrimeB[i]));
  }
  std::printf("p'1: iterations A %zu B %zu, max |p'_A - p'_B| %.3e (max |p'_A| %.3e)\n", ppA.iterations,
              ppB.iterations, maxPP, [&] {
                Real m = 0.0;
                for (Index i = 0; i < n; ++i) m = std::max(m, std::abs(pPrimeA[i]));
                return m;
              }());
  const fields::VectorField u1A = pressure_velocity::correctVelocity(meshA, uStarA, dUA, dVA, pPrimeA, pA);
  const fields::VectorField u1B = pressure_velocity::correctVelocity(meshB, uStarB, dUB, dVB, pPrimeB, pB);
  Real maxU1 = 0.0;
  Index worstU1 = 0;
  for (Index i = 0; i < n; ++i) {
    const Real d = magnitude((u1B[i] - b) - u1A[i]);
    if (d > maxU1) {
      maxU1 = d;
      worstU1 = i;
    }
  }
  std::printf("u1: max |u1_B - b - u1_A| %.3e at cell (%zu,%zu)\n", maxU1, worstU1 % 16, worstU1 / 16);
  // Gradient of p'_A on both meshes (isolates the gradient operator on the translated mesh).
  const fields::VectorField gA = pressure_velocity::correctVelocity(meshA, fields::VectorField(n, Vector3{}),
                                                                    fields::ScalarField(n, 1.0),
                                                                    fields::ScalarField(n, 1.0), pPrimeA, pA);
  const fields::VectorField gB = pressure_velocity::correctVelocity(meshB, fields::VectorField(n, Vector3{}),
                                                                    fields::ScalarField(n, 1.0),
                                                                    fields::ScalarField(n, 1.0), pPrimeA, pB);
  Real maxG = 0.0;
  Index worstG = 0;
  for (Index i = 0; i < n; ++i) {
    const Real d = magnitude(gA[i] - gB[i]);
    if (d > maxG) {
      maxG = d;
      worstG = i;
    }
  }
  std::printf("grad(p'_A) on mesh A vs translated mesh B: max diff %.3e at cell (%zu,%zu): A (%.6e, %.6e) B "
              "(%.6e, %.6e)\n",
              maxG, worstG % 16, worstG / 16, gA[worstG].x, gA[worstG].y, gB[worstG].x, gB[worstG].y);
  return 0;
}
