// P17.11 sections 30-31, 45 -- performance CHARACTERIZATION of the density-based solver.
//
// THIS FILE PRODUCES NO GATE RESULT. §31: P17 qualification does not require GPU speedup, and no
// frozen gate asks for one. If the GPU is slower, the number is recorded as measured.
//
// EVERY ROW CARRIES ITS EQUIVALENCE STATUS, because §30 forbids comparing CPU and GPU timing unless
// the underlying numerical solves have already passed equivalence. So this program does not merely
// time the two paths -- it also COMPARES THEM BITWISE on the very state it timed, and prints that
// verdict next to the wall times. A row whose equivalence column does not say BITWISE must not be
// read as a performance comparison.
//
// It deliberately does NOT try to be a benchmark harness: single process, one warm-up, fixed step
// count, no averaging over repeats, no attempt to control turbo or thermal state. A laptop under
// WSL is not an instrument for throughput claims, and pretending otherwise would be the dishonest
// part -- not the numbers themselves.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/gpu/DeviceDensityBasedSolver.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using namespace cfd::compressible;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

std::uint64_t bitsOf(Real v) {
  std::uint64_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

TotalEnergyState air() {
  const TemperatureDependentProperty cp(std::vector<Real>{1005.0}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(287.05, 1005.0), SensibleEnthalpy(cp, 0.0, 0.0));
}

DensityBasedSettings settings(Real cfl) {
  DensityBasedSettings s;
  s.cfl = cfl;
  s.mode = TimeSteppingMode::GlobalPhysical;
  s.endTime = 1.0;
  s.maxIterations = 100000;
  return s;
}

DensityBasedBoundaries walls(const Mesh& mesh) {
  DensityBasedBoundaries b;
  DensityBasedBoundary w{};
  w.kind = BoundaryKind::InviscidWall;
  for (const auto& p : mesh.boundaryPatches()) b[std::string(p.name())] = w;
  return b;
}

DensityBasedBoundaries throughFlow(const Mesh& mesh, const PrimitiveState& inflow) {
  DensityBasedBoundaries b;
  for (const auto& p : mesh.boundaryPatches()) {
    DensityBasedBoundary e{};
    const std::string n(p.name());
    if (n == "left" || n == "xmin") {
      e.kind = BoundaryKind::SupersonicInflow;
      e.prescribed = inflow;
    } else if (n == "right" || n == "xmax") {
      e.kind = BoundaryKind::SupersonicOutflow;
    } else {
      e.kind = BoundaryKind::InviscidWall;
    }
    b[n] = e;
  }
  return b;
}

ConservedField sodState(const Mesh& mesh, const TotalEnergyState& th,
                        const DensityBasedSolver& s) {
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  const Real R = th.thermodynamics().gasConstant();
  for (Index i = 0; i < n; ++i) {
    const bool left = mesh.cell(i).centroid().x < 0.5;
    rho[i] = left ? 1.0 : 0.125;
    vel[i] = Vector3{0, 0, 0};
    t[i] = (left ? 1.0e5 : 1.0e4) / (rho[i] * R);
  }
  return s.fromPrimitive(mesh, rho, vel, t);
}

ConservedField smoothState(const Mesh& mesh, const TotalEnergyState& th,
                           const DensityBasedSolver& s) {
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    const Real x = mesh.cell(i).centroid().x;
    const Real y = mesh.cell(i).centroid().y;
    rho[i] = 1.2 * (1.0 + 0.25 * std::sin(7.0 * x) * std::cos(3.0 * y));
    vel[i] = Vector3{700.0 + 40.0 * std::cos(5.0 * x), 12.0 * std::sin(4.0 * y), 0.0};
    t[i] = 300.0 * (1.0 + 0.08 * std::cos(6.0 * x + 1.0));
  }
  return s.fromPrimitive(mesh, rho, vel, t);
}

struct Row {
  std::string name;
  std::size_t cells;
  std::string mesh;
  Index steps;
  Real cfl;
  double cpuSeconds;
  double gpuSeconds;
  bool bitwise;
  Index differing;
  Real finalTime;
};

// One case: N explicit steps on the CPU through the production pieces, then N on the device, then a
// bitwise comparison of the two final states.
Row runCase(const std::string& name, Mesh mesh, const std::string& meshDesc,
            bool sod, Index steps, Real cfl) {
  Row row;
  row.name = name;
  row.cells = mesh.numberOfCells();
  row.mesh = meshDesc;
  row.steps = steps;
  row.cfl = cfl;
  row.bitwise = false;
  row.differing = 0;
  row.finalTime = 0.0;

  const TotalEnergyState thermo = air();
  const DensityBasedSolver cpu(settings(cfl), thermo);
  const DensityBasedBoundaries bcs =
      sod ? walls(mesh)
          : throughFlow(mesh, makePrimitiveState(thermo, 1.2, Vector3{700.0, 0, 0}, 300.0));
  const ConservedField q0 = sod ? sodState(mesh, thermo, cpu) : smoothState(mesh, thermo, cpu);

  // ---- CPU ----
  ConservedField host = q0;
  Real elapsed = 0.0;
  const auto c0 = std::chrono::steady_clock::now();
  for (Index s = 0; s < steps; ++s) {
    const std::vector<PrimitiveState> prim = cpu.toPrimitive(host);
    const ResidualField r = cpu.assembleResidual(mesh, prim, bcs);
    const Real dt = cpu.globalTimeStep(mesh, prim);
    host = DensityBasedSolver::applyUpdate(
        mesh, host, r, std::vector<Real>(static_cast<std::size_t>(host.size()), dt));
    elapsed += dt;
  }
  row.cpuSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
  row.finalTime = elapsed;

  // ---- GPU ----
  cfd::gpu::DeviceDensityBasedSolver gpu;
  std::string msg;
  if (!cfd::gpu::densityBasedDeviceAvailable()) {
    row.gpuSeconds = -1.0;
    return row;
  }
  if (!gpu.upload(mesh, thermo, bcs, cfl, &msg)) {
    std::printf("  upload failed: %s\n", msg.c_str());
    row.gpuSeconds = -1.0;
    return row;
  }
  if (!gpu.setState(q0, &msg)) {
    row.gpuSeconds = -1.0;
    return row;
  }
  // One warm-up step so context creation and module load are not charged to the timed loop.
  {
    Real dt = 0;
    bool adm = false;
    Index off = 0;
    int st = 0;
    gpu.step(&dt, &adm, &off, &st, &msg);
    gpu.setState(q0, &msg);
  }
  std::vector<Real> dts;
  Index taken = 0;
  const auto g0 = std::chrono::steady_clock::now();
  const bool ok = gpu.advance(steps, &dts, &taken, &msg);
  row.gpuSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - g0).count();
  if (!ok) {
    std::printf("  gpu advance failed after %d steps: %s\n", static_cast<int>(taken), msg.c_str());
    return row;
  }

  ConservedField device;
  if (!gpu.getState(&device, &msg)) return row;
  Index diff = 0;
  for (Index i = 0; i < host.size(); ++i) {
    if (bitsOf(host.density[i]) != bitsOf(device.density[i])) ++diff;
    if (bitsOf(host.momentumDensity[i].x) != bitsOf(device.momentumDensity[i].x)) ++diff;
    if (bitsOf(host.momentumDensity[i].y) != bitsOf(device.momentumDensity[i].y)) ++diff;
    if (bitsOf(host.momentumDensity[i].z) != bitsOf(device.momentumDensity[i].z)) ++diff;
    if (bitsOf(host.totalEnergyDensity[i]) != bitsOf(device.totalEnergyDensity[i])) ++diff;
  }
  row.differing = diff;
  row.bitwise = (diff == 0) && (host.size() > 0);
  return row;
}

}  // namespace

int main() {
  std::printf("P17.11 -- performance CHARACTERIZATION (no gate, no speedup claim)\n");
  std::printf("backend CPU: single-threaded host, production pieces in the solver's own order\n");
  std::printf("backend GPU: DeviceDensityBasedSolver::advance(), resident, --fmad=false\n\n");

  std::vector<Row> rows;
  rows.push_back(runCase("sod_200x3", MeshGeometry::createCartesian2D(200, 3, 1.0, 0.015),
                         "200x3 uniform", true, 200, 0.4));
  rows.push_back(runCase("sod_800x3", MeshGeometry::createCartesian2D(800, 3, 1.0, 0.00375),
                         "800x3 uniform", true, 200, 0.4));
  rows.push_back(runCase("smooth_100x40", MeshGeometry::createCartesian2D(100, 40, 1.0, 0.4),
                         "100x40 uniform", false, 200, 0.4));
  rows.push_back(runCase("smooth_200x80", MeshGeometry::createCartesian2D(200, 80, 1.0, 0.4),
                         "200x80 uniform", false, 200, 0.4));
  rows.push_back(runCase("smooth_400x160", MeshGeometry::createCartesian2D(400, 160, 1.0, 0.4),
                         "400x160 uniform", false, 100, 0.4));

  std::printf("%-16s %8s %-16s %6s %5s %11s %11s %9s %s\n", "case", "cells", "mesh", "steps",
              "CFL", "cpu wall(s)", "gpu wall(s)", "ratio", "equivalence");
  std::printf("%s\n", std::string(118, '-').c_str());
  for (const Row& r : rows) {
    const char* eq = r.gpuSeconds < 0.0   ? "NO GPU -- no comparison"
                     : r.bitwise          ? "BITWISE"
                                          : "NOT BITWISE -- DO NOT COMPARE";
    if (r.gpuSeconds > 0.0) {
      std::printf("%-16s %8zu %-16s %6d %5.2f %11.4f %11.4f %8.2fx %s",
                  r.name.c_str(), r.cells, r.mesh.c_str(), static_cast<int>(r.steps), r.cfl,
                  r.cpuSeconds, r.gpuSeconds, r.cpuSeconds / r.gpuSeconds, eq);
      if (!r.bitwise) std::printf(" (%d values differ)", static_cast<int>(r.differing));
      std::printf("\n");
    } else {
      std::printf("%-16s %8zu %-16s %6d %5.2f %11.4f %11s %9s %s\n", r.name.c_str(), r.cells,
                  r.mesh.c_str(), static_cast<int>(r.steps), r.cfl, r.cpuSeconds, "n/a", "n/a", eq);
    }
  }
  std::printf("\nphysical final time reached, per case (all steps CFL-limited):\n");
  for (const Row& r : rows) {
    std::printf("  %-16s t_final = %.6e s over %d steps\n", r.name.c_str(), r.finalTime,
                static_cast<int>(r.steps));
  }
  std::printf("\nREADING THIS TABLE:\n");
  std::printf("  * 'ratio' is cpu/gpu wall time. It is a measurement, not a claim, and no gate\n");
  std::printf("    depends on it. §31: qualification does not require GPU speedup.\n");
  std::printf("  * a row is only comparable at all if its equivalence column says BITWISE.\n");
  std::printf("  * the CPU side is SINGLE-THREADED. Against a multi-core CPU implementation the\n");
  std::printf("    ratio would be smaller, and no multi-core CPU density-based path exists to\n");
  std::printf("    measure, so this is not a CPU-vs-GPU hardware comparison.\n");
  std::printf("  * one warm-up GPU step is excluded; context and module load are not charged to\n");
  std::printf("    the timed loop. Host<->device transfer of the initial and final state IS\n");
  std::printf("    excluded from the GPU loop, because advance() is resident by design -- so the\n");
  std::printf("    ratio describes the STEP LOOP, not an end-to-end solve.\n");
  return 0;
}
