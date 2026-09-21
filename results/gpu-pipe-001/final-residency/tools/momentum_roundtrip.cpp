// GPU-PIPE-001 Final Residency, Part 3 Phase A -- the momentum host round trip,
// measured at the exact boundary the resident SIMPLE loop proposes to remove.
//
//   assembleMomentum()      device system -> host LinearSystem      (D2H x4)
//     toHostSystem()        SparseMatrixBuilder rebuild + sort      (CPU)
//   momentumSolver->solve() matrix / b / x0 / Jacobi up, x down
//   setMomentumSolution()   u* back to the device                   (H2D x1)
//
// Driven through the facade's public API in SIMPLE's own order, so the numbers
// are the production numbers rather than a model of them.
//
// THE PREREQUISITE THIS PROBE EXISTS FOR:
//
// `toHostSystem()` drops every entry whose assembled value is exactly 0.0. If
// the momentum matrix has any such entry, the device CSR the resident solver
// would adopt is NOT the host CSR the current solver receives, the SpMV sums a
// different set of terms, and "the resident path is bitwise the round trip it
// replaces" is false before a line of it is written. The pressure stage hit
// exactly this and had to change the assembled structure to fix it.
//
// So the structure is measured FIRST, on every component, in 2D and 3D, on
// pinned and open-boundary cases, before the design depends on the answer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSolverType;
using cfd::algebra::PreconditionerType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

int failures = 0;

struct Snap {
  std::uint64_t h2d = 0, h2dBytes = 0, d2h = 0, d2hBytes = 0, alloc = 0;
  Snap operator-(const Snap& o) const {
    return Snap{h2d - o.h2d, h2dBytes - o.h2dBytes, d2h - o.d2h, d2hBytes - o.d2hBytes,
                alloc - o.alloc};
  }
};

Snap snap() {
  const auto& s = cfd::gpu::gpuExecutionStats();
  return Snap{s.hostToDeviceCalls, s.hostToDeviceBytes, s.deviceToHostCalls, s.deviceToHostBytes,
              s.allocations};
}

struct Case {
  std::string name;
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure;
  FluidProperties fluid{1.0, 0.01};
  bool threeD = false;
};

Case cavity(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}, false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    const bool lid = i + 1 == m.boundaryPatches().size();
    c.vb.set(m, patch.name(),
             lid ? std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0, 0.0, 0.0}))
                 : std::unique_ptr<cfd::boundary::BoundaryCondition>(
                       std::make_unique<cfd::boundary::Wall>()));
    c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  for (Index cell = 0; cell < m.numberOfCells(); ++cell)
    c.velocity[cell] = Vector3{0.1, 0.05, c.threeD ? 0.02 : 0.0};
  return c;
}

Case inletOutlet(std::string name, Mesh mesh) {
  Case c{std::move(name), std::move(mesh), {}, {}, {}, {}, FluidProperties{1.0, 0.01}, false};
  const auto& m = c.mesh;
  c.threeD = m.dimension() == 3;
  const std::size_t patchCount = m.boundaryPatches().size();
  std::size_t i = 0;
  for (const auto& patch : m.boundaryPatches()) {
    if (i == 0) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{1.0, 0.0, 0.0}));
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else if (i + 1 == patchCount) {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Outlet>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      c.vb.set(m, patch.name(), std::make_unique<cfd::boundary::Wall>());
      c.pb.set(m, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    ++i;
  }
  c.velocity = VectorField(m.numberOfCells());
  c.pressure = ScalarField(m.numberOfCells());
  for (Index cell = 0; cell < m.numberOfCells(); ++cell) c.velocity[cell] = Vector3{1.0, 0.0, 0.0};
  return c;
}

// The candidate pattern the momentum plan carries: row P holds column P plus
// every internal-face neighbour, ascending. Computed from the mesh,
// independently of the device, so it is a real cross-check and not a
// restatement.
std::vector<std::vector<Index>> meshPattern(const Mesh& mesh) {
  std::vector<std::vector<Index>> rows(static_cast<std::size_t>(mesh.numberOfCells()));
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    auto& row = rows[static_cast<std::size_t>(c)];
    row.push_back(c);
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      if (face.isBoundary()) continue;
      row.push_back(face.owner() == c ? *face.neighbor() : face.owner());
    }
    std::sort(row.begin(), row.end());
  }
  return rows;
}

Index patternSize(const std::vector<std::vector<Index>>& pattern) {
  Index total = 0;
  for (const auto& row : pattern) total += static_cast<Index>(row.size());
  return total;
}

// Does the HOST matrix the solver currently receives have exactly the pattern
// the device holds? nnz equality alone does not answer this -- two matrices can
// share a count and order their columns differently, and the SpMV would then
// accumulate in a different order and stop being bitwise. Compared
// element-by-element against the mesh-derived pattern, which neither side
// produced.
bool patternMatches(const cfd::algebra::SparseMatrix& host,
                    const std::vector<std::vector<Index>>& pattern) {
  if (host.rows() != static_cast<Index>(pattern.size())) return false;
  const Index* offsets = host.rowOffsetsData();
  const Index* columns = host.columnIndicesData();
  for (Index r = 0; r < host.rows(); ++r) {
    const auto& expected = pattern[static_cast<std::size_t>(r)];
    if (offsets[r + 1] - offsets[r] != static_cast<Index>(expected.size())) return false;
    for (Index k = 0; k < static_cast<Index>(expected.size()); ++k) {
      if (columns[offsets[r] + k] != expected[static_cast<std::size_t>(k)]) return false;
    }
  }
  return true;
}

void runCase(const Case& c, Index convectionScheme, const char* schemeName) {
  GpuSimpleDiscretization gpu;
  std::string reason;
  if (!gpu.prepare(c.mesh, c.vb, c.pb, reason)) {
    std::printf("  FAIL %s: prepare: %s\n", c.name.c_str(), reason.c_str());
    ++failures;
    return;
  }
  const Index nc = c.mesh.numberOfCells();
  ScalarField viscosity(nc);
  for (Index i = 0; i < nc; ++i) viscosity[i] = 0.01;
  SurfaceField flux(c.mesh.numberOfFaces());
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) flux[f] = 0.01;
  gpu.uploadInitialState(c.velocity, c.pressure, flux, viscosity);

  LinearSolverSettings ls;
  ls.type = LinearSolverType::BiCGSTAB;
  ls.backend = LinearSolverBackend::GPU;
  ls.maxIterations = 1000;
  ls.absoluteTolerance = 1e-10;
  ls.relativeTolerance = 1e-8;
  ls.preconditioner = PreconditionerType::Jacobi;
  auto solver = cfd::algebra::makeLinearSolver(ls);
  if (!solver) {
    std::printf("  FAIL %s: no GPU linear solver\n", c.name.c_str());
    ++failures;
    return;
  }

  const Index components = c.threeD ? 3 : 2;
  const auto pattern = meshPattern(c.mesh);
  const Index full = patternSize(pattern);

  // warm-up so the structure upload and first-touch allocations are outside the
  // measured window
  for (Index k = 0; k < components; ++k) {
    ScalarField previous(nc);
    for (Index i = 0; i < nc; ++i) previous[i] = c.velocity[i].x;
    const auto sys = gpu.assembleMomentum(k, previous, 0.7, convectionScheme, false);
    const auto r = solver->solve(sys, cfd::algebra::Vector(static_cast<std::size_t>(nc)));
    gpu.setMomentumSolution(k, r.solution);
  }

  std::printf("  --- %s  [%s]  cells=%lld  components=%lld ---\n", c.name.c_str(), schemeName,
              static_cast<long long>(nc), static_cast<long long>(components));

  Snap assembleTotal, solveTotal, backTotal;
  double assembleSeconds = 0.0, rebuildSeconds = 0.0, solveSeconds = 0.0;
  for (Index k = 0; k < components; ++k) {
    ScalarField previous(nc);
    for (Index i = 0; i < nc; ++i)
      previous[i] = k == 0 ? c.velocity[i].x : (k == 1 ? c.velocity[i].y : c.velocity[i].z);

    const Snap a0 = snap();
    cfd::Timer assembleTimer;
    const auto system = gpu.assembleMomentum(k, previous, 0.7, convectionScheme, false);
    assembleSeconds += assembleTimer.elapsedSeconds();
    const Snap a1 = snap();

    cfd::Timer solveTimer;
    const auto result = solver->solve(system, cfd::algebra::Vector(static_cast<std::size_t>(nc)));
    solveSeconds += solveTimer.elapsedSeconds();
    const Snap a2 = snap();

    gpu.setMomentumSolution(k, result.solution);
    const Snap a3 = snap();

    const Snap as = a1 - a0, sv = a2 - a1, bk = a3 - a2;
    assembleTotal.h2d += as.h2d;
    assembleTotal.h2dBytes += as.h2dBytes; assembleTotal.d2h += as.d2h;
    assembleTotal.d2hBytes += as.d2hBytes; assembleTotal.alloc += as.alloc;
    solveTotal.h2d += sv.h2d; solveTotal.h2dBytes += sv.h2dBytes;
    solveTotal.d2h += sv.d2h; solveTotal.d2hBytes += sv.d2hBytes;
    backTotal.h2d += bk.h2d; backTotal.h2dBytes += bk.h2dBytes;
    backTotal.d2h += bk.d2h; backTotal.d2hBytes += bk.d2hBytes;

    // ---- THE STRUCTURAL QUESTION -----------------------------------------
    // Device nnz is derived from the assemble download's own byte count:
    //   D2H bytes = 8 * ((nc+1) rowOffsets + nnz columns + nnz values + nc rhs)
    // which also cross-checks the byte counter against the CSR shape.
    const Index deviceNnz =
        static_cast<Index>((static_cast<std::int64_t>(as.d2hBytes) / 8 - (nc + 1) - nc) / 2);
    const Index hostNnz = system.matrix().nonZeros();
    const bool ordered = patternMatches(system.matrix(), pattern);
    const bool identical = deviceNnz == hostNnz && deviceNnz == full && ordered;
    if (!identical) ++failures;
    std::printf("    %s component %lld  full %lld  device nnz %lld  host nnz %lld  dropped %lld  "
                "column order %s  %s\n",
                identical ? "PASS" : "FAIL", static_cast<long long>(k),
                static_cast<long long>(full), static_cast<long long>(deviceNnz),
                static_cast<long long>(hostNnz), static_cast<long long>(deviceNnz - hostNnz),
                ordered ? "matches the mesh pattern" : "DIFFERS FROM THE MESH PATTERN",
                identical ? "(device matrix == host matrix)"
                          : "(DEVICE MATRIX IS NOT THE HOST MATRIX -- a resident solve would not "
                            "be bitwise)");
    (void)rebuildSeconds;
  }

  std::printf("    window                H2D   H2D bytes      D2H   D2H bytes\n");
  std::printf("    assemble (D2H out)  %5llu  %10llu  %5llu  %10llu\n",
              (unsigned long long)assembleTotal.h2d, (unsigned long long)assembleTotal.h2dBytes,
              (unsigned long long)assembleTotal.d2h, (unsigned long long)assembleTotal.d2hBytes);
  std::printf("    solve (up + down)   %5llu  %10llu  %5llu  %10llu\n",
              (unsigned long long)solveTotal.h2d, (unsigned long long)solveTotal.h2dBytes,
              (unsigned long long)solveTotal.d2h, (unsigned long long)solveTotal.d2hBytes);
  std::printf("    setMomentumSolution %5llu  %10llu  %5llu  %10llu\n",
              (unsigned long long)backTotal.h2d, (unsigned long long)backTotal.h2dBytes,
              (unsigned long long)backTotal.d2h, (unsigned long long)backTotal.d2hBytes);
  std::printf("    TOTAL per iteration %5llu  %10llu  %5llu  %10llu\n",
              (unsigned long long)(assembleTotal.h2d + solveTotal.h2d + backTotal.h2d),
              (unsigned long long)(assembleTotal.h2dBytes + solveTotal.h2dBytes +
                                   backTotal.h2dBytes),
              (unsigned long long)(assembleTotal.d2h + solveTotal.d2h + backTotal.d2h),
              (unsigned long long)(assembleTotal.d2hBytes + solveTotal.d2hBytes +
                                   backTotal.d2hBytes));
  std::printf("    assemble+rebuild %.3f ms   solve %.3f ms\n", assembleSeconds * 1e3,
              solveSeconds * 1e3);
}

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001 final residency: momentum host round trip, measured ===\n");
  if (!cfd::gpu::cudaAvailable()) {
    std::printf("no CUDA device\n");
    return 2;
  }
  // Upwind is the production default; QUICK and LinearUpwind add
  // higher-order face terms, which is where an exactly-zero coefficient is
  // most likely to appear. Open-boundary and 3D cases carry different boundary
  // encodings again.
  runCase(cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 0, "Upwind");
  runCase(cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 2, "QUICK");
  runCase(cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 3,
          "LinearUpwind");
  runCase(cavity("cavity 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 1, "Central");
  runCase(inletOutlet("channel 2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0)), 0,
          "Upwind");
  runCase(cavity("cavity 3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0)), 0,
          "Upwind");
  runCase(cavity("cavity 2d 160", MeshGeometry::createCartesian2D(160, 160, 1.0, 1.0)), 0,
          "Upwind");

  std::printf("\n%s\n", failures == 0 ? "MOMENTUM ROUND TRIP: PASS (device structure == host "
                                        "structure everywhere measured)"
                                      : "MOMENTUM ROUND TRIP: FAIL");
  return failures == 0 ? 0 : 1;
}
