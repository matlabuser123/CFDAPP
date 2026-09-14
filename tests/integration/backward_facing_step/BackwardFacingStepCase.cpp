#include "BackwardFacingStepCase.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

namespace cfd::validation::step {

namespace {

constexpr const char* kInletPrefix = "inlet_";

Index columns(const StepSpec& spec) {
  return static_cast<Index>(std::lround(spec.length * static_cast<Real>(spec.cellsPerHeight)));
}

// Net +x mass flow through the internal vertical face column nearest x.
Real sectionFlow(const mesh::Mesh& mesh, const fields::SurfaceField& massFlux, Real x) {
  Real bestX = 0.0;
  Real bestDistance = 1e300;
  const auto vertical = [](const mesh::Face& f) {
    return !f.isBoundary() && std::abs(f.areaVector().x) > std::abs(f.areaVector().y);
  };
  for (const auto& face : mesh.faces()) {
    if (!vertical(face)) continue;
    const Real distance = std::abs(face.centroid().x - x);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestX = face.centroid().x;
    }
  }
  Real flow = 0.0;
  for (const auto& face : mesh.faces()) {
    if (!vertical(face) || std::abs(face.centroid().x - bestX) > 1e-9 * (1.0 + bestX)) continue;
    flow += face.areaVector().x > 0.0 ? massFlux[face.id()] : -massFlux[face.id()];
  }
  return flow;
}

std::string format(const char* pattern, Real value) {
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), pattern, value);
  return buffer;
}

}  // namespace

Real averagedInletVelocity(Real y0, Real y1) {
  // Antiderivative of 24 (y - h)(H - y) in s = y - h (H - h = h = 0.5):
  // 24 (h s^2 / 2 - s^3 / 3).
  const auto primitive = [](Real y) {
    const Real s = y - kStepHeight;
    return 24.0 * (0.5 * (kChannelHeight - kStepHeight) * s * s - s * s * s / 3.0);
  };
  return (primitive(y1) - primitive(y0)) / (y1 - y0);
}

mesh::Mesh makeStepMesh(const StepSpec& spec) {
  const Index ny = spec.cellsPerHeight;
  if (ny == 0 || ny % 2 != 0) {
    throw std::invalid_argument("makeStepMesh: cellsPerHeight must be even and positive");
  }
  const mesh::Mesh base =
      mesh::MeshGeometry::createCartesian2D(columns(spec), ny, spec.length, kChannelHeight);
  std::vector<mesh::BoundaryPatch> patches;
  std::vector<Index> stepFaces;
  std::vector<mesh::BoundaryPatch> inletPatches;
  for (const auto& patch : base.boundaryPatches()) {
    if (patch.name() != "left") {
      patches.push_back(patch);
      continue;
    }
    for (const Index faceId : patch.faceIds()) {
      if (base.face(faceId).centroid().y < kStepHeight) {
        stepFaces.push_back(faceId);
      } else {
        inletPatches.emplace_back(kInletPrefix + std::to_string(faceId),
                                  std::vector<Index>{faceId});
      }
    }
  }
  patches.emplace_back("step", std::move(stepFaces));
  for (auto& patch : inletPatches) patches.push_back(std::move(patch));
  return mesh::Mesh(base.cells(), base.faces(), std::move(patches));
}

StepBoundaries makeStepBoundaries(const mesh::Mesh& mesh) {
  StepBoundaries b;
  for (const auto& patch : mesh.boundaryPatches()) {
    const std::string& name = patch.name();
    if (name == "right") {
      b.velocity.set(mesh, name, std::make_unique<boundary::Outlet>());
      b.pressure.set(mesh, name, std::make_unique<boundary::FixedValue>(0.0));
      continue;
    }
    b.pressure.set(mesh, name, std::make_unique<boundary::FixedGradient>(0.0));
    if (name.rfind(kInletPrefix, 0) == 0) {
      const auto& face = mesh.face(patch.faceIds().front());
      const Real halfHeight = 0.5 * face.area();
      const Real u =
          averagedInletVelocity(face.centroid().y - halfHeight, face.centroid().y + halfHeight);
      b.velocity.set(mesh, name, std::make_unique<boundary::Inlet>(Vector2{u, 0.0}));
    } else {
      b.velocity.set(mesh, name, std::make_unique<boundary::Wall>());
    }
  }
  return b;
}

std::vector<ShearZero> shearZeroCrossings(const std::vector<Real>& x,
                                          const std::vector<Real>& tau) {
  if (x.size() != tau.size() || x.size() < 2) {
    throw std::invalid_argument("shearZeroCrossings: need >= 2 samples of equal size");
  }
  std::vector<ShearZero> zeros;
  for (std::size_t k = 0; k + 1 < x.size(); ++k) {
    const Real a = tau[k];
    const Real b = tau[k + 1];
    if (a == 0.0 && b == 0.0) continue;
    if ((a < 0.0 && b >= 0.0) || (a > 0.0 && b <= 0.0)) {
      if (b == 0.0 && k + 2 < x.size() && ((a < 0.0) == (tau[k + 2] < 0.0))) {
        continue;  // touches zero without changing sign
      }
      const Real weight = a / (a - b);
      zeros.push_back({x[k] + weight * (x[k + 1] - x[k]), a < 0.0});
    }
  }
  return zeros;
}

WallShear wallShear(const mesh::Mesh& mesh, Index nx, Index ny, const fields::VectorField& velocity,
                    Real viscosity) {
  WallShear shear;
  const Real halfCell = 0.5 * kChannelHeight / static_cast<Real>(ny);
  for (Index i = 0; i < nx; ++i) {
    shear.x.push_back(mesh.cell(i).centroid().x);
    shear.bottom.push_back(viscosity * velocity[i].x / halfCell);
    shear.top.push_back(-viscosity * velocity[((ny - 1) * nx) + i].x / halfCell);
  }
  return shear;
}

SeparationTopology separationTopology(const WallShear& shear) {
  SeparationTopology topology;
  const auto bottom = shearZeroCrossings(shear.x, shear.bottom);
  const auto top = shearZeroCrossings(shear.x, shear.top);
  topology.bottomCrossings = bottom.size();
  topology.topCrossings = top.size();
  // Primary reattachment: the downstream-most negative -> positive
  // crossing, provided the shear stays positive after it (it is the last
  // crossing overall).
  if (!bottom.empty() && bottom.back().negativeToPositive) {
    topology.reattachment = bottom.back().x / kStepHeight;
  }
  // Corner eddy: the positive-shear region at the step foot ends at the
  // positive -> negative crossing immediately upstream of the reattachment
  // (where the primary eddy's reversed wall shear begins).
  if (topology.reattachment.has_value() && bottom.size() >= 2 &&
      !bottom[bottom.size() - 2].negativeToPositive) {
    topology.cornerEddyEnd = bottom[bottom.size() - 2].x / kStepHeight;
  }
  // Upper-wall bubble: attached top shear is negative; separation is the
  // first negative -> positive crossing, reattachment the next crossing.
  for (std::size_t k = 0; k < top.size(); ++k) {
    if (!top[k].negativeToPositive) continue;
    topology.upperSeparation = top[k].x / kStepHeight;
    if (k + 1 < top.size()) topology.upperReattachment = top[k + 1].x / kStepHeight;
    break;
  }
  return topology;
}

ValidationRun runStep(const StepSpec& spec) {
  const mesh::Mesh mesh = makeStepMesh(spec);
  const StepBoundaries boundaries = makeStepBoundaries(mesh);
  const Index nx = columns(spec);
  const Index ny = spec.cellsPerHeight;
  const Real viscosity = kDensity * kMeanInletVelocity * kChannelHeight / spec.reynolds;

  pressure_velocity::SIMPLESettings settings;
  settings.maxIterations = 60000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.convectionScheme = spec.scheme;
  settings.momentumSolver.maxIterations = 2000;
  settings.momentumSolver.absoluteTolerance = 1e-12;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.absoluteTolerance = 1e-12;
  settings.pressureSolver.relativeTolerance = 1e-6;
  settings.pressureSolver.preconditioner = algebra::PreconditionerType::Jacobi;
  settings.robustness.linearSolverFallback.enabled = true;
  const pressure_velocity::SIMPLE simple(settings, /*referenceCell=*/0);

  const auto start = std::chrono::steady_clock::now();
  const auto result = simple.solve(mesh, physics::FluidProperties(kDensity, viscosity),
                                   boundaries.velocity, boundaries.pressure,
                                   fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                                   fields::ScalarField(mesh.numberOfCells(), 0.0));
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  ValidationRun run = makeSimpleValidationRun(
      "backward_facing_step_re" +
          std::to_string(static_cast<long long>(std::lround(spec.reynolds))),
      spec.reynolds, std::string(discretization::convectionSchemeName(spec.scheme)),
      GridSpec{std::to_string(nx) + "x" + std::to_string(ny), nx, ny, spec.length, kChannelHeight},
      result, 1e-6, seconds);
  run.checks.push_back({"solve_accepted", run.level.accepted,
                        run.level.solverStatus + " " + run.level.rejectionReason});
  if (!run.level.accepted) return run;

  // Mass flow: inflow (outward normal -x), outflow, and 4 sections.
  const Real inflowRate = kDensity * kMeanInletVelocity * (kChannelHeight - kStepHeight);
  Real inflow = 0.0, outflow = 0.0, maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      if (patch.name().rfind(kInletPrefix, 0) == 0) {
        inflow -= result.massFlux[f];
      } else if (patch.name() == "right") {
        outflow += result.massFlux[f];
      } else {
        maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[f]));
      }
    }
  }
  Real maxFlowError = std::max(std::abs(inflow - inflowRate), std::abs(outflow - inflowRate));
  auto& d = run.level.diagnostics;
  d.emplace_back("inflow", inflow);
  d.emplace_back("outflow", outflow);
  for (const Real fraction : {0.1, 0.25, 0.5, 0.75}) {
    const Real flow = sectionFlow(mesh, result.massFlux, fraction * spec.length);
    d.emplace_back(format("section_flow_x%.2fL", fraction), flow);
    maxFlowError = std::max(maxFlowError, std::abs(flow - inflowRate));
  }
  d.emplace_back("max_flow_error", maxFlowError);
  d.emplace_back("max_wall_normal_flux", maxWallFlux);

  const SeparationTopology topology =
      separationTopology(wallShear(mesh, nx, ny, result.velocity, viscosity));
  const auto put = [&](const char* name, const std::optional<Real>& value) {
    if (value.has_value()) d.emplace_back(name, *value);
  };
  put("reattachment_length_over_h", topology.reattachment);
  put("corner_eddy_end_over_h", topology.cornerEddyEnd);
  put("upper_separation_over_h", topology.upperSeparation);
  put("upper_reattachment_over_h", topology.upperReattachment);
  if (topology.upperSeparation && topology.upperReattachment) {
    d.emplace_back("upper_bubble_length_over_h",
                   *topology.upperReattachment - *topology.upperSeparation);
  }
  d.emplace_back("bottom_shear_zero_crossings", static_cast<Real>(topology.bottomCrossings));
  d.emplace_back("top_shear_zero_crossings", static_cast<Real>(topology.topCrossings));

  run.checks.push_back(
      {"mass_flow", maxFlowError <= 1e-6,
       format("max |Q - 0.5| over inflow, outflow and 4 sections %.3g (bound 1e-6)",
              maxFlowError)});
  run.checks.push_back({"wall_normal_flux", maxWallFlux <= 1e-6,
                        format("max |wall mass flux| %.3g (bound 1e-6)", maxWallFlux)});
  run.checks.push_back({"reattachment_detected", topology.reattachment.has_value(),
                        topology.reattachment ? format("x_r/h = %.4f", *topology.reattachment)
                                              : std::string("no negative -> positive crossing "
                                                            "ends the bottom-wall shear")});
  return run;
}

}  // namespace cfd::validation::step
