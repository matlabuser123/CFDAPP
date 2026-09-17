// P12-MESH-003 thermal fix: gradient-type temperature boundaries
// (Adiabatic, FixedGradient, HeatFlux) are assembled exactly, and the outer
// loop's final state is consistent with its own boundary values.
//
// The defect these tests catch: the boundary value of a gradient-type face
// was evaluated from the PREVIOUS outer iterate, so the solved system
// carried a wall heat flow Df (T_P^new - T_P^old), and the outer loop
// stopped on max |dT| < 1e-8 and returned a field that had never been
// solved with its own boundary values -- an insulated wall leaking up to
// Df * 1e-8 per face, a global energy imbalance, and hundreds of outer
// iterations for a linear conduction problem (results/p12-mesh-003,
// logs/05_thermal_fix_instrumentation.log).
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"
#include "cfd/thermal/ThermalRegionMap.hpp"
#include "BoundaryFluxProbe.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::FixedValue;
using cfd::boundary::HeatFlux;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::boundaryDiffusionContribution;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalSolverSettings;
using cfd::thermal::ThermalStatus;

namespace {

constexpr Real kConductivity = 2.0;
constexpr Real kHeatFluxOut = 0.75;  // HeatFlux q'' (outward: dT/dn = -q''/k)

// A genuinely two-dimensional conduction problem on [0, 1] x [0, 0.75]:
// left T = 1, bottom T = 0, top insulated, right losing q'' = 0.75. The
// temperature varies in x and y, so the insulated wall and the heat-flux
// wall both carry real tangential gradients next to them.
Mesh makeMesh() { return MeshGeometry::createCartesian2D(16, 12, 1.0, 0.75); }

BoundaryConditionSet makeBoundaries(const Mesh& mesh) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<FixedTemperature>(1.0));
  b.set(mesh, "bottom", std::make_unique<FixedTemperature>(0.0));
  b.set(mesh, "top", std::make_unique<Adiabatic>());
  b.set(mesh, "right", std::make_unique<HeatFlux>(kHeatFluxOut, kConductivity));
  return b;
}

std::string patchOf(const Mesh& mesh, Index faceId) {
  return std::string(cfd::boundary::boundaryPatchNameForFace(mesh, faceId));
}

// Conduction heat flow out of the owner through a boundary face.
//
// P12-DIFF-002 validation migration: this used to evaluate
//     boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * (T_P - T_b)
// i.e. the pre-DIFF-002 TWO-POINT wall flux, explicitly requesting the historical path. Production
// now assembles the DIFF-002 three-point reconstruction wherever a valid inward stencil exists, so
// that estimator measured a different operator than the solver used and reported a boundary-vs-
// interior inconsistency the solver did not have. It now uses the one shared diagnostic that
// evaluates the operator production actually assembles (tests/support/BoundaryFluxProbe.hpp); the
// conservation statement below is still formed and summed here, independently.
Real boundaryHeatFlowOut(const Mesh& mesh, const Face& face, const ScalarField& t,
                         const BoundaryConditionSet& b,
                         const cfd::fields::VectorField& gradT) {
  return -cfd::testutil::boundaryDiffusiveFluxIntoOwner(mesh, face, kConductivity, t, b, gradT);
}

Real internalHeatFlowOut(const Mesh& mesh, const Face& face, const ScalarField& t) {
  return cfd::testutil::internalDiffusiveFluxOutOfOwner(mesh, face, kConductivity, t);
}

Real residualNorm(const cfd::algebra::LinearSystem& system, const ScalarField& t) {
  cfd::algebra::Vector x(t.size());
  for (Index i = 0; i < t.size(); ++i) x[i] = t[i];
  return cfd::algebra::l2Norm(system.rhs() - system.matrix().multiply(x));
}

}  // namespace

// The assembled contribution of one boundary face, per condition type:
// prescribed values unchanged (A += Df, b += Df T_b); gradient types the
// exact prescribed flux, independent of the owner temperature.
TEST(ThermalBoundaryConsistency, BoundaryDiffusionContributionIsExactPerConditionType) {
  const Mesh mesh = makeMesh();
  const Index faceId = mesh.boundaryPatches()[0].faceIds()[3];  // any boundary face
  const Face& face = mesh.face(faceId);
  const std::string patch = patchOf(mesh, faceId);
  const Real d = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Real df = 3.7;
  const ScalarField cold(mesh.numberOfCells(), -4.0);
  const ScalarField hot(mesh.numberOfCells(), 250.0);
  const auto contribution = [&](std::unique_ptr<cfd::boundary::BoundaryCondition> bc,
                                const ScalarField& t) {
    BoundaryConditionSet b;
    for (const auto& p : mesh.boundaryPatches()) {
      b.set(mesh, p.name(), p.name() == patch ? std::move(bc) : std::make_unique<Adiabatic>());
    }
    return boundaryDiffusionContribution(mesh, face, t, b, df);
  };
  for (const ScalarField* t : {&cold, &hot}) {
    const auto fixed = contribution(std::make_unique<FixedTemperature>(2.5), *t);
    EXPECT_EQ(fixed.diagonal, df);
    EXPECT_EQ(fixed.source, df * 2.5);
    const auto value = contribution(std::make_unique<FixedValue>(-1.25), *t);
    EXPECT_EQ(value.diagonal, df);
    EXPECT_EQ(value.source, df * -1.25);
    const auto adiabatic = contribution(std::make_unique<Adiabatic>(), *t);
    EXPECT_EQ(adiabatic.diagonal, 0.0);
    EXPECT_EQ(adiabatic.source, 0.0);  // exactly no heat through an insulated face
    const auto gradient = contribution(std::make_unique<FixedGradient>(0.4), *t);
    EXPECT_EQ(gradient.diagonal, 0.0);
    EXPECT_EQ(gradient.source, df * (0.4 * d));
    const auto flux = contribution(std::make_unique<HeatFlux>(kHeatFluxOut, kConductivity), *t);
    EXPECT_EQ(flux.diagonal, 0.0);
    EXPECT_EQ(flux.source, df * ((-kHeatFluxOut / kConductivity) * d));
  }
}

// THE regression: after ThermalSolver reports Converged, the returned field
// satisfies the equation assembled from its own boundary values, the
// insulated wall carries no heat, every cell (wall cells included)
// balances, and the global energy balance closes -- in at most three outer
// iterations (one solve plus confirmation), with a result that does not
// depend on the outer tolerance.
TEST(ThermalBoundaryConsistency, ConvergedFieldIsConsistentWithItsBoundaryValues) {
  const Mesh mesh = makeMesh();
  const auto boundaries = makeBoundaries(mesh);
  const ThermalProperties thermal(kConductivity, 1.0);
  const ScalarField initial(mesh.numberOfCells(), 0.5);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const ThermalResult result = ThermalSolver{}.solve(mesh, initial, noFlow, thermal, boundaries);
  ASSERT_EQ(result.status, ThermalStatus::Converged);
  const ScalarField& t = result.temperature;
  for (Index c = 0; c < t.size(); ++c) ASSERT_TRUE(std::isfinite(t[c]));

  // (1) Consistency: the equation assembled from the returned field.
  const Real initialImbalance = residualNorm(
      cfd::thermal::assembleEnergyEquation(mesh, initial, noFlow, thermal, boundaries).system,
      initial);
  const Real consistent = residualNorm(
      cfd::thermal::assembleEnergyEquation(mesh, t, noFlow, thermal, boundaries).system, t);
  EXPECT_LE(consistent, 1e-9 * initialImbalance)
      << "returned field does not satisfy its own equation (initial imbalance " << initialImbalance
      << ")";

  // (2) Per-cell balance, from the field and its own boundary values, using the operator
  // production assembles (see boundaryHeatFlowOut's note on the P12-DIFF-002 migration).
  const cfd::fields::VectorField gradT =
      cfd::testutil::diffusionCorrectionGradient(mesh, t, boundaries);
  Real throughput = 0.0;
  std::vector<Real> net(mesh.numberOfCells(), 0.0);
  Real in = 0.0, out = 0.0, insulated = 0.0, fluxWall = 0.0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      const Real q = boundaryHeatFlowOut(mesh, face, t, boundaries, gradT);
      net[face.owner()] += q;
      const std::string patch = patchOf(mesh, face.id());
      if (patch == "left") in -= q;
      if (patch == "bottom") out += q;
      if (patch == "top") insulated += std::abs(q);
      if (patch == "right") fluxWall += q;
    } else {
      const Real q = internalHeatFlowOut(mesh, face, t);
      net[face.owner()] += q;
      net[*face.neighbor()] -= q;
      throughput = std::max(throughput, std::abs(q));
    }
  }
  Real worstCell = 0.0;
  for (const Real v : net) worstCell = std::max(worstCell, std::abs(v));
  EXPECT_LE(worstCell, 1e-9 * throughput);

  // (3) The insulated wall carries no heat; the heat-flux wall exactly q''.
  EXPECT_EQ(insulated, 0.0);
  EXPECT_NEAR(fluxWall, kHeatFluxOut * 0.75, 1e-12);

  // (4) Energy conservation: heat in through the hot wall = heat out
  // through the cold wall + the prescribed loss.
  EXPECT_GT(in, 0.0);
  EXPECT_LE(std::abs(in - out - fluxWall), 1e-9 * in)
      << "in " << in << ", out " << out << ", heat-flux wall " << fluxWall;

  // (5) A linear problem: one solve plus a confirming (refinement)
  // iteration -- no lagged-boundary iterations (the pre-fix solver needed
  // 380 here); the answer does not depend on the outer tolerance.
  EXPECT_LE(result.iterations, 3u);
  EXPECT_LT(result.maxTemperatureChange, ThermalSolverSettings{}.tolerance);
  ThermalSolverSettings tight;
  tight.tolerance = 1e-14;
  const ThermalResult tighter =
      ThermalSolver{tight}.solve(mesh, initial, noFlow, thermal, boundaries);
  ASSERT_EQ(tighter.status, ThermalStatus::Converged);
  EXPECT_LE(tighter.iterations, 4u);
  for (Index c = 0; c < t.size(); ++c) EXPECT_NEAR(tighter.temperature[c], t[c], 1e-12) << c;
}

// The same guarantees on the conjugate (multi-region) conduction path,
// which assembles its boundary faces through the same contribution.
TEST(ThermalBoundaryConsistency, ConjugateConductionPathIsConsistentToo) {
  const Mesh mesh = makeMesh();
  const auto boundaries = makeBoundaries(mesh);
  std::vector<Index> cellRegion(mesh.numberOfCells(), 0);
  const cfd::thermal::ThermalRegionMap regions(
      {cfd::thermal::ThermalRegion{"a", cfd::thermal::ThermalRegionType::Solid,
                                   ThermalProperties(kConductivity, 1.0)}},
      cellRegion);
  const ScalarField initial(mesh.numberOfCells(), 0.5);
  const ThermalResult result =
      ThermalSolver{}.solveConjugateConduction(mesh, initial, regions, boundaries);
  ASSERT_EQ(result.status, ThermalStatus::Converged);
  EXPECT_LE(result.iterations, 3u);
  const Real initialImbalance = residualNorm(
      cfd::thermal::assembleConjugateConductionEquation(mesh, initial, regions, boundaries).system,
      initial);
  const Real consistent = residualNorm(cfd::thermal::assembleConjugateConductionEquation(
                                           mesh, result.temperature, regions, boundaries)
                                           .system,
                                       result.temperature);
  EXPECT_LE(consistent, 1e-9 * initialImbalance);
  // Single region: identical to the single-material solve.
  const ThermalResult single =
      ThermalSolver{}.solve(mesh, initial, SurfaceField(mesh.numberOfFaces(), 0.0),
                            ThermalProperties(kConductivity, 1.0), boundaries);
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    EXPECT_NEAR(result.temperature[c], single.temperature[c], 1e-12) << c;
  }
}

// A cell whose faces are all gradient-type (a one-cell mesh) has no
// coefficient left in its row -- its steady equation is singular. solve()
// must report that through its status, never throw: an insulated cell with
// no heat anywhere keeps its temperature (a consistent, converged state);
// one losing heat through a HeatFlux face has no steady state.
TEST(ThermalBoundaryConsistency, SingularAllGradientCellReportsStatusInsteadOfThrowing) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField initial(1, 300.0);
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(kConductivity, 1.0);
  BoundaryConditionSet insulated;
  for (const auto& p : mesh.boundaryPatches())
    insulated.set(mesh, p.name(), std::make_unique<Adiabatic>());
  ThermalResult a;
  EXPECT_NO_THROW(a = ThermalSolver{}.solve(mesh, initial, noFlow, thermal, insulated));
  EXPECT_EQ(a.status, ThermalStatus::Converged);
  EXPECT_EQ(a.temperature[0], 300.0);

  BoundaryConditionSet losing;
  for (const auto& p : mesh.boundaryPatches()) {
    if (p.name() == "right") {
      losing.set(mesh, p.name(), std::make_unique<HeatFlux>(kHeatFluxOut, kConductivity));
    } else {
      losing.set(mesh, p.name(), std::make_unique<Adiabatic>());
    }
  }
  ThermalResult b;
  EXPECT_NO_THROW(b = ThermalSolver{}.solve(mesh, initial, noFlow, thermal, losing));
  EXPECT_FALSE(b.converged());
}
