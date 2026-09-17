#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"

namespace cfd::pressure_velocity {

using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::physics::assembleBuoyancySourceContribution;
using cfd::physics::assembleConvectionContribution;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::assemblePressureSourceContribution;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;

MomentumAssembly assembleRelaxedMomentumComponent(
    const Mesh& mesh, const VectorField& velocity, const ScalarField& pressure,
    const SurfaceField& massFlux, const ScalarField& effectiveViscosity,
    const BoundaryConditionSet& velocityBoundaries, const BoundaryConditionSet& pressureBoundaries,
    VelocityComponent component, const ScalarField& previousComponentValue, Real alpha,
    const ScalarField* temperature, const BoussinesqBuoyancy* buoyancy,
    cfd::discretization::ConvectionScheme convectionScheme,
    cfd::discretization::GradientScheme gradientScheme, bool applyNonOrthogonalCorrection,
    const VectorField* nonOrthogonalCorrectionVelocity, const VectorField* momentumSource) {
  const Index n = mesh.numberOfCells();
  if (velocity.size() != n || pressure.size() != n) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: velocity/pressure size does not match mesh cell "
        "count");
  }
  if (massFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: massFlux size does not match mesh face count");
  }
  if (effectiveViscosity.size() != n) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: effectiveViscosity size does not match mesh cell "
        "count");
  }
  if ((temperature == nullptr) != (buoyancy == nullptr)) {
    throw InvalidArgumentError(
        "assembleRelaxedMomentumComponent: temperature and buoyancy must be both null or both "
        "non-null");
  }

  SparseMatrixBuilder builder(n, n);
  // P4 -- Performance: reserve for a 2D 5-point-stencil upper bound (each
  // internal face contributes up to 4 triplets between diffusion+
  // convection, each boundary face up to 2 -- 5*n comfortably covers a
  // structured Cartesian mesh's actual triplet count without ever being a
  // hard limit; add() still works correctly past it, just without the
  // reservation's benefit). This is SIMPLE's own per-outer-iteration
  // momentum assembly, the measured hotspot -- see TODO.md's own P4
  // status note for the before/after benchmark evidence.
  builder.reserve(5 * n);
  Vector rhs(n, 0.0);

  assembleDiffusionContribution(mesh, effectiveViscosity, velocity, velocityBoundaries, component,
                                builder, rhs, applyNonOrthogonalCorrection, gradientScheme,
                                nonOrthogonalCorrectionVelocity);
  assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, builder,
                                 rhs, convectionScheme);
  assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs,
                                     gradientScheme);
  if (buoyancy != nullptr) {
    assembleBuoyancySourceContribution(mesh, *temperature, *buoyancy, component, rhs);
  }
  if (momentumSource != nullptr) {
    cfd::physics::assembleMomentumSourceContribution(mesh, *momentumSource, component, rhs);
  }

  Vector previousComponentVector(n);
  for (Index i = 0; i < n; ++i) {
    previousComponentVector[i] = previousComponentValue[i];
  }
  applyImplicitUnderRelaxation(builder, rhs, previousComponentVector, alpha);

  SparseMatrix matrix = builder.build();
  Vector diagonal(n);
  for (Index row = 0; row < n; ++row) {
    diagonal[row] = matrix.diagonal(row);
  }

  if (!matrix.allFinite() || !rhs.allFinite()) {
    throw NumericalError(
        "assembleRelaxedMomentumComponent: assembled system contains a non-finite value");
  }

  return MomentumAssembly{cfd::algebra::LinearSystem(std::move(matrix), rhs), std::move(diagonal)};
}

namespace {

Vector componentVector(const VectorField& velocity, VelocityComponent component) {
  Vector v(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    v[i] = cfd::physics::velocityComponentValue(velocity[i], component);
  }
  return v;
}

}  // namespace

NonOrthogonalPassResult runNonOrthogonalCorrectionPasses(
    Index totalPasses, const VectorField& velocityStar,
    const cfd::algebra::LinearSolver& momentumSolver, const Mesh& mesh, const VectorField& velocity,
    const ScalarField& pressure, const SurfaceField& massFlux,
    const ScalarField& effectiveViscosity, const BoundaryConditionSet& velocityBoundaries,
    const BoundaryConditionSet& pressureBoundaries, const ScalarField& previousU,
    const ScalarField& previousV, Real alpha, const ScalarField* temperature,
    const BoussinesqBuoyancy* buoyancy, cfd::discretization::ConvectionScheme convectionScheme,
    cfd::discretization::GradientScheme gradientScheme, const VectorField* momentumSource,
    const ScalarField* previousW) {
  NonOrthogonalPassResult result;
  result.velocityStar = velocityStar;
  // P12-MESH-006: a W pass exactly like the U and V ones, on a 3D mesh only.
  if ((mesh.dimension() == 3) != (previousW != nullptr)) {
    throw InvalidArgumentError(
        "runNonOrthogonalCorrectionPasses: previousW must be given exactly for a 3D mesh");
  }

  for (Index pass = 1; pass < totalPasses; ++pass) {
    std::optional<MomentumAssembly> uPass;
    std::optional<MomentumAssembly> vPass;
    std::optional<MomentumAssembly> wPass;
    try {
      uPass = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, effectiveViscosity, velocityBoundaries,
          pressureBoundaries, VelocityComponent::U, previousU, alpha, temperature, buoyancy,
          convectionScheme, gradientScheme, /*applyNonOrthogonalCorrection=*/true,
          &result.velocityStar, momentumSource);
      vPass = assembleRelaxedMomentumComponent(
          mesh, velocity, pressure, massFlux, effectiveViscosity, velocityBoundaries,
          pressureBoundaries, VelocityComponent::V, previousV, alpha, temperature, buoyancy,
          convectionScheme, gradientScheme, /*applyNonOrthogonalCorrection=*/true,
          &result.velocityStar, momentumSource);
      if (previousW != nullptr) {
        wPass = assembleRelaxedMomentumComponent(
            mesh, velocity, pressure, massFlux, effectiveViscosity, velocityBoundaries,
            pressureBoundaries, VelocityComponent::W, *previousW, alpha, temperature, buoyancy,
            convectionScheme, gradientScheme, /*applyNonOrthogonalCorrection=*/true,
            &result.velocityStar, momentumSource);
      }
    } catch (const NumericalError&) {
      result.status = NonOrthogonalPassStatus::NonFiniteState;
      return result;
    } catch (const InvalidArgumentError&) {
      result.status = NonOrthogonalPassStatus::NonFiniteState;
      return result;
    }

    const auto uSolve = momentumSolver.solve(
        uPass->system, componentVector(result.velocityStar, VelocityComponent::U));
    if (uSolve.fallback.attempted) result.fallbackReports.push_back(uSolve.fallback);
    if (!uSolve.converged()) {
      result.status = NonOrthogonalPassStatus::MomentumFailure;
      result.failedSolve = uSolve;
      return result;
    }
    const auto vSolve = momentumSolver.solve(
        vPass->system, componentVector(result.velocityStar, VelocityComponent::V));
    if (vSolve.fallback.attempted) result.fallbackReports.push_back(vSolve.fallback);
    if (!vSolve.converged()) {
      result.status = NonOrthogonalPassStatus::MomentumFailure;
      result.failedSolve = vSolve;
      return result;
    }
    std::optional<cfd::algebra::SolverResult> wSolve;
    if (wPass.has_value()) {
      wSolve = momentumSolver.solve(wPass->system,
                                    componentVector(result.velocityStar, VelocityComponent::W));
      if (wSolve->fallback.attempted) result.fallbackReports.push_back(wSolve->fallback);
      if (!wSolve->converged()) {
        result.status = NonOrthogonalPassStatus::MomentumFailure;
        result.failedSolve = *wSolve;
        return result;
      }
    }

    VectorField next(mesh.numberOfCells());
    Real increment = 0.0;
    bool finite = true;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      next[i] = wSolve.has_value()
                    ? Vector3{uSolve.solution[i], vSolve.solution[i], wSolve->solution[i]}
                    : Vector2{uSolve.solution[i], vSolve.solution[i]};
      finite = finite && isFinite(next[i]);
      increment = std::max(increment, magnitude(next[i] - result.velocityStar[i]));
    }
    if (!finite) {
      result.status = NonOrthogonalPassStatus::NonFiniteState;
      return result;
    }
    result.velocityStar = std::move(next);
    result.u = std::move(uPass);
    result.v = std::move(vPass);
    result.w = std::move(wPass);
    result.passIncrements.push_back(increment);
    result.linearIterations += uSolve.iterations + vSolve.iterations;
    if (wSolve.has_value()) result.linearIterations += wSolve->iterations;
    ++result.passesExecuted;
  }
  return result;
}

}  // namespace cfd::pressure_velocity
