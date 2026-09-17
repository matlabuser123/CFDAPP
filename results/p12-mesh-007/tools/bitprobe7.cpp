// P12-MESH-007 gate G9.1 (as amended by A3): bit identity of the static paths MESH-007 touches,
// compiled against the G9 baseline (nom7 = the current tree with MESH-007 removed) and against NEW
// (the current tree). The two outputs must be byte-identical. It hashes (FNV-1a over the exact IEEE
// bits) and prints, per item:
//   - every builder's geometry: C16, G16, Q16, MB2 (cells, faces, patches, block vertex grids) and
//     H8;
//   - static PISO, 10 steps, dt = 0.01, the gate's solver settings, on the C16 cavity and the G16,
//     Q16 and MB2 channels: per step the velocity, pressure, flux, maxCFL, continuity residual,
//     mass imbalance and status;
//   - a TransientSolver run of the C16 cavity (the history and the final state);
//   - 3D SIMPLE, 20 outer iterations, on the 8^3 lid cube (cases/lid_driven_cavity_3d at 8^3);
//   - the ThermalSolver in 2D (C16) and 3D (H8), with a convecting flux.
// The MESH-005 and MESH-006 probes (bitprobe.cpp, bitprobe6.cpp) cover the 2D SIMPLE path and are
// run alongside, unchanged. Uses only APIs present in both trees.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using namespace cfd;

namespace {

struct Hash {
  std::uint64_t h = 14695981039346656037ULL;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ULL;
    }
  }
  void real(Real v) { bytes(&v, sizeof v); }
  void index(Index v) { bytes(&v, sizeof v); }
  void vec(const Vector3& v) {
    real(v.x);
    real(v.y);
    real(v.z);
  }
  void text(const std::string& s) { bytes(s.data(), s.size()); }
};

void print(const char* group, const std::string& label, const Hash& h) {
  std::printf("%-10s %-44s %016llx\n", group, label.c_str(), static_cast<unsigned long long>(h.h));
}

// --- the gate's meshes (tests/support/MeshMotionCases.hpp, without the motion part) -------------
mesh::Mesh c16() { return mesh::MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0); }
mesh::Mesh g16() {
  const mesh::AxisGrading grading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both};
  return mesh::MeshGeometry::createGraded2D(16, 16, 1.0, 1.0, grading, grading);
}
mesh::Mesh q16() {
  std::vector<Vector3> v;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      const Real pi = constants::pi;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return mesh::MeshGeometry::createStructuredQuad2D(16, 16, v);
}
mesh::Mesh mb2() {
  using mesh::BlockSide;
  const auto grid = [](Real x0, Real x1) {
    std::vector<Vector3> v;
    for (Index j = 0; j <= 16; ++j) {
      for (Index i = 0; i <= 8; ++i) {
        v.push_back(Vector3{x0 + ((x1 - x0) * (static_cast<Real>(i) / 8.0)),
                            static_cast<Real>(j) / 16.0, 0.0});
      }
    }
    return v;
  };
  mesh::MultiBlockSpec spec;
  spec.blocks.push_back({"a", 8, 16, grid(0.0, 0.5)});
  spec.blocks.push_back({"b", 8, 16, grid(0.5, 1.0)});
  spec.interfaces.push_back({{0, BlockSide::Right}, {1, BlockSide::Left}, false});
  spec.patches.push_back({"left", {{0, BlockSide::Left}}});
  spec.patches.push_back({"right", {{1, BlockSide::Right}}});
  spec.patches.push_back({"bottom", {{0, BlockSide::Bottom}, {1, BlockSide::Bottom}}});
  spec.patches.push_back({"top", {{0, BlockSide::Top}, {1, BlockSide::Top}}});
  return mesh::MeshGeometry::createMultiBlock2D(spec);
}
mesh::Mesh h8() { return mesh::MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0); }

void geometry(const std::string& label, const mesh::Mesh& m) {
  Hash h;
  for (const auto& c : m.cells()) {
    h.index(c.id());
    h.vec(c.centroid());
    h.real(c.volume());
    for (const Index f : c.faceIds()) h.index(f);
  }
  for (const auto& f : m.faces()) {
    h.index(f.owner());
    h.index(f.neighbor().value_or(static_cast<Index>(-1)));
    h.vec(f.centroid());
    h.vec(f.areaVector());
  }
  for (const auto& p : m.boundaryPatches()) {
    h.text(p.name());
    for (const Index f : p.faceIds()) h.index(f);
  }
  for (const auto& g : m.structuredBlocks()) {
    h.index(g.nx);
    h.index(g.ny);
    h.index(g.nz);
    h.text(g.name);
    for (const auto& v : g.vertices) h.vec(v);
  }
  print("geometry", label, h);
}

pressure_velocity::PISOSettings gateSettings() {
  pressure_velocity::PISOSettings s;
  s.momentumSolver.absoluteTolerance = 1e-15;
  s.momentumSolver.relativeTolerance = 1e-12;
  s.momentumSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-15;
  s.pressureSolver.relativeTolerance = 1e-12;
  s.pressureSolver.maxIterations = 20000;
  return s;
}

void hashState(Hash& h, const solver::TransientState& s) {
  for (Index i = 0; i < s.velocity.size(); ++i) h.vec(s.velocity[i]);
  for (Index i = 0; i < s.pressure.size(); ++i) h.real(s.pressure[i]);
  for (Index f = 0; f < s.massFlux.size(); ++f) h.real(s.massFlux[f]);
}

struct Flow {
  std::string label;
  mesh::Mesh mesh;
  bool cavity;
  bool symmetrySides;
};

void piso(const Flow& flow) {
  const physics::FluidProperties fluid(1.0, 0.01);
  const auto& m = flow.mesh;
  boundary::BoundaryConditionSet vbc;
  boundary::BoundaryConditionSet pbc;
  for (const auto& patch : m.boundaryPatches()) {
    const std::string& n = patch.name();
    if (flow.cavity) {
      if (n == "top") {
        vbc.set(m, n, std::make_unique<boundary::MovingWall>(Vector3{1, 0, 0}));
      } else {
        vbc.set(m, n, std::make_unique<boundary::Wall>());
      }
      pbc.set(m, n, std::make_unique<boundary::FixedGradient>(0.0));
      continue;
    }
    if (n == "left") {
      vbc.set(m, n, std::make_unique<boundary::Inlet>(Vector3{1, 0, 0}));
      pbc.set(m, n, std::make_unique<boundary::FixedGradient>(0.0));
    } else if (n == "right") {
      vbc.set(m, n, std::make_unique<boundary::Outlet>());
      pbc.set(m, n, std::make_unique<boundary::FixedValue>(0.0));
    } else {
      if (flow.symmetrySides) {
        vbc.set(m, n, std::make_unique<boundary::Symmetry>());
      } else {
        vbc.set(m, n, std::make_unique<boundary::Wall>());
      }
      pbc.set(m, n, std::make_unique<boundary::FixedGradient>(0.0));
    }
  }
  const pressure_velocity::PISO solverP(m, fluid, vbc, pbc, gateSettings());
  fields::VectorField u(m.numberOfCells(), flow.cavity ? Vector3{} : Vector3{1, 0, 0});
  solver::TransientState state{u, fields::ScalarField(m.numberOfCells(), 0.0),
                               physics::calculateMassFlux(m, u, fluid, vbc)};
  Hash all;
  for (int step = 1; step <= 10; ++step) {
    auto r = solverP.solveTimeStep(state, 0.01);
    Hash h;
    hashState(h, r.state);
    h.real(r.maxCFL);
    h.real(r.continuityResidual);
    h.real(r.massImbalance);
    h.index(static_cast<Index>(r.status));
    all.bytes(&h.h, sizeof h.h);
    print("piso", flow.label + " step " + std::to_string(step), h);
    if (r.status != solver::TransientStepStatus::Converged) break;
    state = std::move(r.state);
  }
  print("piso", flow.label + " all steps", all);

  // TransientSolver run of the same flow (history included).
  if (flow.cavity) {
    const solver::TransientSolver run(solverP, 10.0);
    const auto result = run.solve(
        solver::TransientState{u, fields::ScalarField(m.numberOfCells(), 0.0),
                               physics::calculateMassFlux(m, u, fluid, vbc)},
        solver::TimeController(0.0, 0.1, 0.01, 10));
    Hash h;
    h.index(static_cast<Index>(result.status));
    for (const auto& rec : result.history) {
      h.index(rec.step);
      h.real(rec.time);
      h.real(rec.deltaT);
      h.real(rec.maxCFL);
      h.real(rec.continuityResidual);
      h.real(rec.massImbalance);
    }
    hashState(h, result.finalState);
    print("transient", flow.label + " TransientSolver", h);
  }
}

void simple3d() {
  io::CaseDefinition d = io::CaseReader{}.read("cases/lid_driven_cavity_3d");
  d.mesh.nx = 8;
  d.mesh.ny = 8;
  d.mesh.nz = 8;
  const io::SimulationSetup setup = io::CaseBuilder{}.build(d);
  auto settings = setup.solverSettings;
  settings.maxIterations = 20;
  const pressure_velocity::SIMPLE simple(settings, 0);
  const auto r = simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries,
                              setup.pressureBoundaries, setup.initialVelocity,
                              setup.initialPressure);
  Hash h;
  for (Index i = 0; i < r.velocity.size(); ++i) h.vec(r.velocity[i]);
  for (Index i = 0; i < r.pressure.size(); ++i) h.real(r.pressure[i]);
  for (Index f = 0; f < r.massFlux.size(); ++f) h.real(r.massFlux[f]);
  for (const auto* hist : {&r.uResidualHistory, &r.vResidualHistory, &r.pressureResidualHistory,
                           &r.continuityHistory}) {
    for (const Real v : *hist) h.real(v);
  }
  h.index(r.iterations);
  h.index(static_cast<Index>(r.status));
  print("simple3d", "lid cube 8^3, 20 iterations", h);
}

void thermalRun(const std::string& label, const mesh::Mesh& m, const Vector3& velocity) {
  boundary::BoundaryConditionSet tbc;
  boundary::BoundaryConditionSet vbc;
  Index k = 0;
  for (const auto& patch : m.boundaryPatches()) {
    if (k == 0) {
      tbc.set(m, patch.name(), std::make_unique<boundary::FixedTemperature>(1.0));
    } else if (k == 1) {
      tbc.set(m, patch.name(), std::make_unique<boundary::FixedTemperature>(0.0));
    } else {
      tbc.set(m, patch.name(), std::make_unique<boundary::Adiabatic>());
    }
    vbc.set(m, patch.name(), std::make_unique<boundary::Inlet>(velocity));
    ++k;
  }
  const physics::FluidProperties fluid(1.0, 0.01);
  const fields::VectorField u(m.numberOfCells(), velocity);
  const auto flux = physics::calculateMassFlux(m, u, fluid, vbc);
  thermal::ThermalSolverSettings settings;
  settings.tolerance = 1e-12;
  const thermal::ThermalSolver solver(settings);
  const auto r = solver.solve(m, fields::ScalarField(m.numberOfCells(), 0.5), flux,
                              thermal::ThermalProperties(0.05, 1.0), tbc, 0.1);
  Hash h;
  for (Index i = 0; i < r.temperature.size(); ++i) h.real(r.temperature[i]);
  h.index(static_cast<Index>(r.status));
  h.index(r.iterations);
  h.index(r.linearIterations);
  h.real(r.finalResidual);
  for (const Real v : r.outerChangeHistory) h.real(v);
  print("thermal", label, h);
}

}  // namespace

int main() {
  std::printf("# P12-MESH-007 G9.1 bit probe (FNV-1a of the exact IEEE bits)\n");
  geometry("C16", c16());
  geometry("G16", g16());
  geometry("Q16", q16());
  geometry("MB2", mb2());
  geometry("H8", h8());
  piso(Flow{"C16 cavity", c16(), true, false});
  piso(Flow{"G16 channel", g16(), false, false});
  piso(Flow{"Q16 channel", q16(), false, false});
  piso(Flow{"MB2 channel", mb2(), false, true});
  simple3d();
  thermalRun("2D C16, u = (0.3, 0.1)", c16(), Vector3{0.3, 0.1, 0.0});
  thermalRun("3D H8, u = (0.3, 0.1, 0.05)", h8(), Vector3{0.3, 0.1, 0.05});
  return 0;
}
