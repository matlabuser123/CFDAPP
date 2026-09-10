// P2-TURB-003: proves PISO's transient momentum predictor actually
// consumes a cfd::turbulence::TurbulenceModel -- the default (no model
// supplied) path stays exactly laminar, and a model reporting a nonzero
// mu_t demonstrably changes the predicted/corrected state for one time
// step.
#include <gtest/gtest.h>

#include <memory>
#include <string_view>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/solver/TransientSolver.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::solver::TransientState;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepStatus;
using cfd::turbulence::LaminarModel;
using cfd::turbulence::TurbulenceModel;

namespace {

// Same test-only probe as SIMPLE's own P2-TURB-003 test
// (test_simple_turbulence.cpp) -- a constant, caller-supplied mu_t with
// no turbulence physics of its own.
class TestConstantEddyViscosityModel final : public TurbulenceModel {
 public:
  TestConstantEddyViscosityModel(const Mesh& mesh, Real muT)
      : turbulentViscosity_(mesh.numberOfCells(), muT) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "test-constant-eddy"; }

  [[nodiscard]] const ScalarField& turbulentViscosity() const override {
    return turbulentViscosity_;
  }

  void correct(const Mesh& /*mesh*/, const VectorField& /*velocity*/,
               const ScalarField& /*pressure*/) override {}

 private:
  ScalarField turbulentViscosity_;
};

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

PISOSettings makeSettings() {
  cfd::algebra::LinearSolverSettings linear;
  linear.maxIterations = 500;
  linear.absoluteTolerance = 1e-12;
  linear.relativeTolerance = 1e-10;
  PISOSettings settings;
  settings.momentumSolver = linear;
  settings.pressureSolver = linear;
  return settings;
}

TransientState makeRestState(const Mesh& mesh) {
  TransientState state;
  state.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  state.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  state.massFlux = SurfaceField(mesh.numberOfFaces(), 0.0);
  return state;
}

}  // namespace

TEST(PISOTurbulenceTest, DefaultTurbulenceModelMatchesExplicitLaminarModel) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const TransientState previousState = makeRestState(mesh);
  const Real dt = 0.01;

  const PISO defaultPiso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeSettings(), 0);
  const TransientStepResult withDefault = defaultPiso.solveTimeStep(previousState, dt);

  LaminarModel explicitLaminar(mesh);
  const PISO explicitPiso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeSettings(), 0,
                          &explicitLaminar);
  const TransientStepResult withExplicitLaminar = explicitPiso.solveTimeStep(previousState, dt);

  ASSERT_EQ(withDefault.status, TransientStepStatus::Converged);
  ASSERT_EQ(withDefault.status, withExplicitLaminar.status);
  EXPECT_EQ(withDefault.continuityResidual, withExplicitLaminar.continuityResidual);
  EXPECT_EQ(withDefault.massImbalance, withExplicitLaminar.massImbalance);

  ASSERT_EQ(withDefault.state.velocity.size(), withExplicitLaminar.state.velocity.size());
  for (Index i = 0; i < withDefault.state.velocity.size(); ++i) {
    EXPECT_EQ(withDefault.state.velocity[i].x, withExplicitLaminar.state.velocity[i].x);
    EXPECT_EQ(withDefault.state.velocity[i].y, withExplicitLaminar.state.velocity[i].y);
    EXPECT_EQ(withDefault.state.pressure[i], withExplicitLaminar.state.pressure[i]);
  }
  for (Index i = 0; i < withDefault.state.massFlux.size(); ++i) {
    EXPECT_EQ(withDefault.state.massFlux[i], withExplicitLaminar.state.massFlux[i]);
  }
}

TEST(PISOTurbulenceTest, ElevatedEddyViscosityChangesPredictedState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const TransientState previousState = makeRestState(mesh);
  const Real dt = 0.01;

  const PISO laminarPiso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeSettings(), 0);
  const TransientStepResult laminarResult = laminarPiso.solveTimeStep(previousState, dt);
  ASSERT_EQ(laminarResult.status, TransientStepStatus::Converged);

  TestConstantEddyViscosityModel elevatedModel(mesh, fluid.dynamicViscosity() * 50.0);
  const PISO turbulentPiso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeSettings(), 0,
                           &elevatedModel);
  const TransientStepResult turbulentResult = turbulentPiso.solveTimeStep(previousState, dt);
  ASSERT_EQ(turbulentResult.status, TransientStepStatus::Converged);

  bool anyDifferent = false;
  for (Index i = 0; i < laminarResult.state.velocity.size(); ++i) {
    if (laminarResult.state.velocity[i].x != turbulentResult.state.velocity[i].x ||
        laminarResult.state.velocity[i].y != turbulentResult.state.velocity[i].y) {
      anyDifferent = true;
      break;
    }
  }
  EXPECT_TRUE(anyDifferent);
}
