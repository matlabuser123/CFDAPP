// P16-GPU-TURB -- device discrete semantics for scalar transport.
//
// WHY THIS FILE EXISTS
// --------------------
// P14 qualified the device ASSEMBLY of the generic scalar transport equation
// (diffusion + convection + Patankar source + transient). It did not port the
// two things the CPU turbulence path additionally relies on, and the Phase A
// audit named both as structural gaps:
//
//   * Patankar implicit UNDER-RELAXATION, which every turbulence equation uses;
//   * IMPOSED CELLS (SparseMatrixBuilder::fixRow), which the P16 wall treatment
//     uses to fix epsilon in every wall-adjacent cell.
//
// Without them a device k-epsilon solve could not reproduce the CPU one even in
// principle, so they come before any turbulence kernel.
//
// THE FROZEN METHODOLOGY (written down before any number was measured)
// --------------------------------------------------------------------
// The CPU order is assemble -> under-relax -> fix imposed rows -> solve
// (cfd::turbulence::solveRelaxedScalarTransport), and the device must match it
// operation for operation. Three gates, each with a stated reason for its
// tolerance:
//
//   G1  STRUCTURE, exact, and checked in BOTH directions.
//       The host builder drops entries that accumulate to exactly zero; the
//       device keeps the candidate pattern. So the host pattern is a SUBSET,
//       and equivalence means: every host entry is found on the device with an
//       equal value, AND every device entry with no host counterpart is
//       exactly zero. Only the first half is what the P14 probe checked. The
//       second half is what makes an imposed row honest -- a device row that
//       kept its assembled off-diagonals passes a one-directional check and is
//       a different matrix. No tolerance: this is a structural property.
//
//   G2  THE SEMANTICS, BITWISE (0 ulp), zero tolerance.
//       Assembly association order differs between host and device by design
//       (P14 measured <= 2 ulp on diagonals where convection and diffusion both
//       act), and that pre-existing difference would otherwise mask the thing
//       under test here. So G2 removes it by construction: both sides START
//       from the device's own assembled system, and only then does the host
//       apply applyImplicitUnderRelaxation + fixRow while the device applies
//       applyImplicitUnderRelaxationDevice + fixImposedRowsDevice. Identical
//       inputs, identical operations -- anything but bitwise equality is a
//       semantic difference, which is exactly what this task is about.
//
//   G3  THE COMPOSED PATH, gated by P14's already-frozen solution bound.
//       The full CPU path against the full device path, including the assembly
//       difference G2 removes. The ulp numbers are REPORTED rather than bounded
//       by a threshold invented here; the acceptance criterion is P14's
//       measured, frozen 2.1e-12 relative-L2 agreement of the SOLVED field,
//       which is a pre-existing number from results/p14/transport-foundation/
//       and not one chosen after seeing these results.
//
// NON-VACUITY CONTROLS -- each one asserts that a gate above can FAIL.
//
//   NV1  With alpha != 1 the relaxed system must DIFFER from the unrelaxed one.
//        Otherwise "relaxed CPU matches relaxed device" is satisfied by two
//        paths that both ignored alpha.
//   NV2  Dropping one imposed row, and separately perturbing one imposed value,
//        must make the comparison FAIL. Otherwise G1/G2 do not actually see
//        imposed rows.
//   NV3  The pre-relaxation-diagonal bug, implemented on purpose. A host RHS
//        built with the ALREADY-RELAXED diagonal (a_P/alpha instead of a_P)
//        must disagree with the device. This is the specific subtle error the
//        formula invites, and a test suite that cannot distinguish it does not
//        pin the formula at all.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/gpu/DeviceScalarTransport.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"
#include "cfd/transport/ScalarTransport.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::ImposedCell;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

// Distance between two doubles in representable steps -- the only unit in
// which "how close" is meaningful against a bitwise target.
long long ulpDistance(Real a, Real b) {
  if (a == b) return 0;
  if (!std::isfinite(a) || !std::isfinite(b)) return -1;
  long long ia = 0;
  long long ib = 0;
  std::memcpy(&ia, &a, sizeof(ia));
  std::memcpy(&ib, &b, sizeof(ib));
  if (ia < 0) ia = static_cast<long long>(0x8000000000000000ULL) - ia;
  if (ib < 0) ib = static_cast<long long>(0x8000000000000000ULL) - ib;
  return std::llabs(ia - ib);
}

// --- the shared problem ------------------------------------------------------

ScalarField varying(const Mesh& mesh, Real base, Real slope) {
  ScalarField field(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    field[i] = base + (slope * static_cast<Real>(i % 7)) + (0.01 * static_cast<Real>(i));
  }
  return field;
}

BoundaryConditionSet mixedBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(300.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(280.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

struct Problem {
  Mesh mesh;
  ScalarField phi;
  ScalarField gamma;
  ScalarField Su;
  ScalarField Sp;
  SurfaceField massFlux;
  BoundaryConditionSet boundaries;

  explicit Problem(Mesh m)
      : mesh(std::move(m)),
        phi(varying(mesh, 300.0, 1.5)),
        gamma(varying(mesh, 0.7, 0.05)),
        Su(varying(mesh, 2.0, 0.25)),
        Sp(varying(mesh, -4.0, -0.10)),
        massFlux(mesh.numberOfFaces(), 0.0),
        boundaries(mixedBoundaries(mesh)) {
    // Both upwind branches exercised, so the convection contribution is not
    // one-sided and the diagonal really does receive several terms.
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      massFlux[f] = (f % 3 == 0) ? -0.35 : 0.42;
    }
  }

  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }
};

// --- the host reference ------------------------------------------------------
//
// This is the body of cfd::turbulence::solveRelaxedScalarTransport up to (but
// not including) the linear solve, which has no API of its own -- production
// returns a solved field, and what has to be compared here is the SYSTEM.
// ReplicaReproducesTheProductionSolve below ties this replica back to the
// production function, so a future divergence between them fails a test rather
// than quietly weakening every comparison in this file.
struct HostSystem {
  SparseMatrix matrix;
  Vector rhs;
};

HostSystem hostReference(const Problem& p, Real alpha,
                         const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal,
                         const std::vector<ImposedCell>& imposedCells) {
  const Index n = p.cellCount();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  cfd::transport::assembleScalarDiffusionContribution(p.mesh, p.gamma, p.phi, p.boundaries, builder,
                                                      rhs, nonOrthogonal);
  cfd::transport::assembleScalarConvectionContribution(p.mesh, p.massFlux, p.phi, p.boundaries,
                                                       builder, rhs);
  cfd::transport::applyImplicitScalarSource(p.mesh, builder, rhs, p.Su, p.Sp);

  Vector phiOld(n, 0.0);
  for (Index i = 0; i < n; ++i) phiOld[i] = p.phi[i];
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, phiOld, alpha);

  for (const ImposedCell& imposed : imposedCells) {
    builder.fixRow(imposed.cell);
    rhs[imposed.cell] = imposed.value;
  }
  return HostSystem{builder.build(), rhs};
}

// --- the device path ---------------------------------------------------------

struct DeviceBuffers {
  cfd::gpu::DeviceBuffer<Real> phi;
  cfd::gpu::DeviceBuffer<Real> gamma;
  cfd::gpu::DeviceBuffer<Real> massFlux;
  cfd::gpu::DeviceBuffer<Real> Su;
  cfd::gpu::DeviceBuffer<Real> Sp;
  cfd::gpu::DeviceBuffer<Real> volumes;
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
};

// Returns false (with `why` filled) when the device discretization refuses the
// mesh -- reported as a failure rather than silently passing, because a
// comparison that never ran is not a comparison that agreed.
bool prepareDevice(const Problem& p, DeviceBuffers& out, std::string& why) {
  if (!out.diffusionPlan.build(p.mesh, p.boundaries)) {
    why = "diffusion plan unusable: " + out.diffusionPlan.unsupportedReason();
    return false;
  }
  if (!out.convectionPlan.build(p.mesh, p.boundaries)) {
    why = "convection plan unusable: " + out.convectionPlan.unsupportedReason();
    return false;
  }
  const Index n = p.cellCount();
  out.phi.uploadFrom(p.phi.data(), n);
  out.gamma.uploadFrom(p.gamma.data(), n);
  out.massFlux.uploadFrom(p.massFlux.data(), p.mesh.numberOfFaces());
  out.Su.uploadFrom(p.Su.data(), n);
  out.Sp.uploadFrom(p.Sp.data(), n);
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = p.mesh.cell(i).volume();
  out.volumes.uploadFrom(volumes.data(), n);
  return true;
}

void assembleOnDevice(const Problem& p, DeviceBuffers& buffers, bool nonOrthogonalEnabled,
                      cfd::gpu::DeviceScalarTransportSystem& system) {
  const cfd::gpu::DeviceTransportTimeState steady;
  cfd::gpu::assembleScalarTransportDevice(
      buffers.diffusionPlan, buffers.convectionPlan, buffers.volumes, buffers.phi, buffers.massFlux,
      buffers.gamma, buffers.Su, buffers.Sp, nonOrthogonalEnabled, steady, system);
  (void)p;
}

// A host-side snapshot of a device CSR system.
struct DeviceSnapshot {
  std::vector<Index> rowOffsets;
  std::vector<Index> columnIndices;
  std::vector<Real> values;
  std::vector<Real> rhs;
};

DeviceSnapshot download(const cfd::gpu::DeviceScalarTransportSystem& system, Index cellCount) {
  DeviceSnapshot snapshot;
  snapshot.rowOffsets.resize(static_cast<std::size_t>(cellCount) + 1);
  system.rowOffsets.downloadTo(snapshot.rowOffsets.data(), cellCount + 1);
  snapshot.columnIndices.resize(static_cast<std::size_t>(system.columnIndices.size()));
  system.columnIndices.downloadTo(snapshot.columnIndices.data(), system.columnIndices.size());
  snapshot.values.resize(static_cast<std::size_t>(system.values.size()));
  system.values.downloadTo(snapshot.values.data(), system.values.size());
  snapshot.rhs.resize(static_cast<std::size_t>(cellCount));
  system.rhs.downloadTo(snapshot.rhs.data(), cellCount);
  return snapshot;
}

// --- the comparison ----------------------------------------------------------

struct Comparison {
  bool hostEntriesAllFound = true;        // G1, host -> device
  bool extraDeviceEntriesAllZero = true;  // G1, device -> host  (the new half)
  long long maxValueUlps = 0;
  long long maxRhsUlps = 0;
  Real maxAbsValueDiff = 0.0;
  Real maxAbsRhsDiff = 0.0;
  Index comparedValues = 0;
  Index bitwiseValues = 0;
  Index extraDeviceEntries = 0;
  std::string firstDifference;

  [[nodiscard]] bool structureOk() const {
    return hostEntriesAllFound && extraDeviceEntriesAllZero;
  }
  [[nodiscard]] bool bitwise() const {
    return structureOk() && maxValueUlps == 0 && maxRhsUlps == 0;
  }
};

void noteDifference(Comparison& c, const std::string& what) {
  if (c.firstDifference.empty()) c.firstDifference = what;
}

Comparison compare(const HostSystem& host, const DeviceSnapshot& device, Index cellCount) {
  Comparison c;
  const SparseMatrix& matrix = host.matrix;
  for (Index row = 0; row < cellCount; ++row) {
    const std::size_t deviceBegin = device.rowOffsets[static_cast<std::size_t>(row)];
    const std::size_t deviceEnd = device.rowOffsets[static_cast<std::size_t>(row) + 1];

    // host -> device
    for (Index k = matrix.rowOffsetsData()[row]; k < matrix.rowOffsetsData()[row + 1]; ++k) {
      const Index column = matrix.columnIndicesData()[k];
      const Real hostValue = matrix.valuesData()[k];
      bool found = false;
      for (std::size_t d = deviceBegin; d < deviceEnd; ++d) {
        if (device.columnIndices[d] != column) continue;
        found = true;
        const Real deviceValue = device.values[d];
        ++c.comparedValues;
        if (deviceValue == hostValue) ++c.bitwiseValues;
        const long long ulps = ulpDistance(deviceValue, hostValue);
        if (ulps != 0) {
          noteDifference(c, "row " + std::to_string(row) + " col " + std::to_string(column) +
                                ": host " + std::to_string(hostValue) + " device " +
                                std::to_string(deviceValue));
        }
        c.maxValueUlps = std::max(c.maxValueUlps, ulps);
        c.maxAbsValueDiff = std::max(c.maxAbsValueDiff, std::abs(deviceValue - hostValue));
        break;
      }
      if (!found) {
        c.hostEntriesAllFound = false;
        noteDifference(c, "host entry (row " + std::to_string(row) + ", col " +
                              std::to_string(column) + ") has no device slot");
      }
    }

    // device -> host: every device entry the host does not carry must be an
    // exact zero, because that is the only reason the host builder drops one.
    for (std::size_t d = deviceBegin; d < deviceEnd; ++d) {
      const Index column = device.columnIndices[d];
      bool inHost = false;
      for (Index k = matrix.rowOffsetsData()[row]; k < matrix.rowOffsetsData()[row + 1]; ++k) {
        if (matrix.columnIndicesData()[k] == column) {
          inHost = true;
          break;
        }
      }
      if (inHost) continue;
      ++c.extraDeviceEntries;
      if (device.values[d] != 0.0) {
        c.extraDeviceEntriesAllZero = false;
        noteDifference(c, "device entry (row " + std::to_string(row) + ", col " +
                              std::to_string(column) + ") = " + std::to_string(device.values[d]) +
                              " is absent from the host matrix and is NOT zero");
      }
    }

    const Real hostRhs = host.rhs[row];
    const Real deviceRhs = device.rhs[static_cast<std::size_t>(row)];
    const long long rhsUlps = ulpDistance(deviceRhs, hostRhs);
    if (rhsUlps != 0) {
      noteDifference(c, "rhs row " + std::to_string(row) + ": host " + std::to_string(hostRhs) +
                            " device " + std::to_string(deviceRhs));
    }
    c.maxRhsUlps = std::max(c.maxRhsUlps, rhsUlps);
    c.maxAbsRhsDiff = std::max(c.maxAbsRhsDiff, std::abs(deviceRhs - hostRhs));
  }
  return c;
}

std::string describe(const Comparison& c) {
  return "structure host->device " + std::string(c.hostEntriesAllFound ? "OK" : "BAD") +
         ", device->host " + std::string(c.extraDeviceEntriesAllZero ? "OK" : "BAD") + " (" +
         std::to_string(c.extraDeviceEntries) + " device-only entries), values " +
         std::to_string(c.bitwiseValues) + "/" + std::to_string(c.comparedValues) +
         " bitwise, max value " + std::to_string(c.maxValueUlps) + " ulp (" +
         std::to_string(c.maxAbsValueDiff) + "), max rhs " + std::to_string(c.maxRhsUlps) +
         " ulp (" + std::to_string(c.maxAbsRhsDiff) +
         "); first difference: " + (c.firstDifference.empty() ? "none" : c.firstDifference);
}

// --- the six frozen configurations -------------------------------------------

struct Configuration {
  const char* name;
  Real alpha;
  bool nonOrthogonal;
  bool distortedMesh;
  // Which of the frozen imposed slots this configuration uses. The concrete
  // cell indices depend on the mesh size and are resolved in imposedFor().
  bool imposeFirst;
  bool imposeInterior;
  bool imposeLast;
};

// 1. no imposed cells                       2. cell 0 only
// 3. several, including one interior and one at the end of the index range
// 4. imposed + under-relaxation             5. imposed + non-orthogonal
// 6. CSR structure / coefficients / RHS -- applied to ALL of the above rather
//    than being a case of its own, which is what compare() does.
const std::vector<Configuration>& configurations() {
  static const std::vector<Configuration> cases = {
      {"1 no imposed, alpha=1", 1.0, false, false, false, false, false},
      {"1b no imposed, alpha=0.7", 0.7, false, false, false, false, false},
      {"2 imposed cell 0, alpha=1", 1.0, false, false, true, false, false},
      {"3 imposed {0,interior,last}, alpha=1", 1.0, false, false, true, true, true},
      {"4 imposed {0,interior,last}, alpha=0.7", 0.7, false, false, true, true, true},
      {"5 imposed + non-orthogonal, alpha=0.7", 0.7, true, true, true, true, true},
  };
  return cases;
}

std::vector<ImposedCell> imposedFor(const Configuration& config, Index cellCount) {
  std::vector<ImposedCell> imposed;
  // Cell 0 alone is not enough: it is the first row, its candidate pattern has
  // the diagonal first, and an off-by-one in the row scan would survive it. The
  // interior cell has neighbours on both sides of the diagonal, and the last
  // cell is where a row-offset overrun would show.
  if (config.imposeFirst) imposed.push_back(ImposedCell{0, 4.0});
  if (config.imposeInterior) imposed.push_back(ImposedCell{(cellCount / 2) + 3, 9.25});
  if (config.imposeLast) imposed.push_back(ImposedCell{cellCount - 1, 0.5});
  return imposed;
}

Mesh meshFor(const Configuration& config, Index nx, Index ny) {
  if (!config.distortedMesh) return MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0);
  // The repository's distortion convention: amplitude 0.3/nx (larger values
  // fold cells over and the factory rejects them).
  return cfd::test::createDistortedQuad2D(nx, ny, 1.0, 1.0, 0.3 / static_cast<Real>(nx));
}

cfd::discretization::NonOrthogonalCorrectionOptions optionsFor(const Configuration& config) {
  cfd::discretization::NonOrthogonalCorrectionOptions options;
  options.enabled = config.nonOrthogonal;
  return options;
}

void uploadImposed(const std::vector<ImposedCell>& imposed, Index cellCount,
                   cfd::gpu::DeviceImposedCells& out) {
  std::vector<Index> cells;
  std::vector<Real> values;
  for (const ImposedCell& entry : imposed) {
    cells.push_back(entry.cell);
    values.push_back(entry.value);
  }
  cfd::gpu::uploadImposedCellsDevice(cells, values, cellCount, out);
}

// Builds a host system from the device's OWN assembled values, so the host and
// device start from bit-identical input and the only thing that can differ
// afterwards is relaxation/imposition semantics. This is what makes G2 a
// zero-tolerance gate rather than a tolerance inherited from the assembly.
HostSystem hostSemanticsFromDeviceAssembly(const DeviceSnapshot& assembled, const Problem& problem,
                                           Real alpha,
                                           const std::vector<ImposedCell>& imposedCells) {
  const Index n = problem.cellCount();
  SparseMatrixBuilder builder(n, n);
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = assembled.rowOffsets[static_cast<std::size_t>(row)];
         d < assembled.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      builder.add(row, assembled.columnIndices[d], assembled.values[d]);
    }
  }
  Vector rhs(n, 0.0);
  for (Index i = 0; i < n; ++i) rhs[i] = assembled.rhs[static_cast<std::size_t>(i)];

  Vector phiOld(n, 0.0);
  for (Index i = 0; i < n; ++i) phiOld[i] = problem.phi[i];
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, phiOld, alpha);
  for (const ImposedCell& imposed : imposedCells) {
    builder.fixRow(imposed.cell);
    rhs[imposed.cell] = imposed.value;
  }
  return HostSystem{builder.build(), rhs};
}

// Rebuilds a CPU-solvable system from a device snapshot, so the same solver can
// be run on both and the SOLVED fields compared.
cfd::algebra::LinearSystem toLinearSystem(const DeviceSnapshot& snapshot, Index n) {
  SparseMatrixBuilder builder(n, n);
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = snapshot.rowOffsets[static_cast<std::size_t>(row)];
         d < snapshot.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      builder.add(row, snapshot.columnIndices[d], snapshot.values[d]);
    }
  }
  Vector rhs(n, 0.0);
  for (Index i = 0; i < n; ++i) rhs[i] = snapshot.rhs[static_cast<std::size_t>(i)];
  return cfd::algebra::LinearSystem(builder.build(), rhs);
}

}  // namespace

// =============================================================================
// G3 -- the composed CPU path against the composed device path.
// =============================================================================

TEST(DeviceScalarTransportSemantics, ComposedPathMatchesTheHostOnEveryFrozenConfiguration) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const Configuration& config : configurations()) {
    const Problem problem(meshFor(config, 12, 9));
    const Index n = problem.cellCount();
    const auto imposed = imposedFor(config, n);

    DeviceBuffers buffers;
    std::string why;
    ASSERT_TRUE(prepareDevice(problem, buffers, why)) << config.name << ": " << why;

    cfd::gpu::DeviceScalarTransportSystem device;
    assembleOnDevice(problem, buffers, config.nonOrthogonal, device);
    cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, config.alpha, device);
    cfd::gpu::DeviceImposedCells deviceImposed;
    uploadImposed(imposed, n, deviceImposed);
    cfd::gpu::fixImposedRowsDevice(deviceImposed, device);

    const HostSystem host = hostReference(problem, config.alpha, optionsFor(config), imposed);
    const DeviceSnapshot snapshot = download(device, n);
    const Comparison c = compare(host, snapshot, n);

    // G1: exact, both directions, no tolerance.
    EXPECT_TRUE(c.structureOk()) << config.name << " -- " << describe(c);
    ASSERT_GT(c.comparedValues, 0u) << config.name << ": nothing was compared";

    // G3: the ulp numbers are evidence, not a gate. The gate is the solved
    // field below, against P14's already-frozen 2.1e-12.
    std::printf("  %-40s %s\n", config.name, describe(c).c_str());

    const cfd::algebra::LinearSolverSettings settings;
    const cfd::algebra::BiCGSTAB solver(settings);
    Vector guess(n, 0.0);
    for (Index i = 0; i < n; ++i) guess[i] = problem.phi[i];

    SparseMatrix hostCopy = host.matrix;
    const auto hostSolve =
        solver.solve(cfd::algebra::LinearSystem(std::move(hostCopy), host.rhs), guess);
    ASSERT_TRUE(hostSolve.converged()) << config.name << ": host solve did not converge";
    const auto deviceSolve = solver.solve(toLinearSystem(snapshot, n), guess);
    ASSERT_TRUE(deviceSolve.converged()) << config.name << ": device-system solve did not converge";

    Real numerator = 0.0;
    Real denominator = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real difference = deviceSolve.solution[i] - hostSolve.solution[i];
      numerator += difference * difference;
      denominator += hostSolve.solution[i] * hostSolve.solution[i];
    }
    const Real relativeL2 = std::sqrt(numerator) / std::sqrt(denominator);
    // P14's measured, frozen bound (results/p14/transport-foundation/). Not a
    // number chosen after seeing this result.
    EXPECT_LE(relativeL2, 2.1e-12)
        << config.name << ": solved fields disagree (rel L2 " << relativeL2 << ")";
  }
}

// =============================================================================
// G2 -- the semantics, isolated from the assembly, BITWISE.
// =============================================================================

TEST(DeviceScalarTransportSemantics, RelaxationAndImposedRowsAreBitwiseIdenticalToTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const Configuration& config : configurations()) {
    const Problem problem(meshFor(config, 12, 9));
    const Index n = problem.cellCount();
    const auto imposed = imposedFor(config, n);

    DeviceBuffers buffers;
    std::string why;
    ASSERT_TRUE(prepareDevice(problem, buffers, why)) << config.name << ": " << why;

    cfd::gpu::DeviceScalarTransportSystem device;
    assembleOnDevice(problem, buffers, config.nonOrthogonal, device);
    const DeviceSnapshot assembled = download(device, n);

    cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, config.alpha, device);
    cfd::gpu::DeviceImposedCells deviceImposed;
    uploadImposed(imposed, n, deviceImposed);
    cfd::gpu::fixImposedRowsDevice(deviceImposed, device);

    const HostSystem host =
        hostSemanticsFromDeviceAssembly(assembled, problem, config.alpha, imposed);
    const Comparison c = compare(host, download(device, n), n);

    EXPECT_TRUE(c.bitwise()) << config.name
                             << " -- relaxation/imposition is NOT bitwise from identical input: "
                             << describe(c);
  }
}

// =============================================================================
// NV1 -- alpha != 1 must actually change the system.
// =============================================================================

TEST(DeviceScalarTransportSemantics, UnderRelaxationChangesTheSystemSoTheComparisonIsNotVacuous) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;

  cfd::gpu::DeviceScalarTransportSystem unrelaxed;
  assembleOnDevice(problem, buffers, false, unrelaxed);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 1.0, unrelaxed);
  const DeviceSnapshot withoutRelaxation = download(unrelaxed, n);

  cfd::gpu::DeviceScalarTransportSystem relaxed;
  assembleOnDevice(problem, buffers, false, relaxed);
  const Real alpha = 0.7;
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, alpha, relaxed);
  const DeviceSnapshot withRelaxation = download(relaxed, n);

  // alpha == 1 must be an EXACT no-op: every value and every rhs entry
  // unchanged. That is what lets an unrelaxed device assembly stay
  // byte-identical to the pre-P16 result.
  cfd::gpu::DeviceScalarTransportSystem untouched;
  assembleOnDevice(problem, buffers, false, untouched);
  const DeviceSnapshot never = download(untouched, n);
  EXPECT_EQ(withoutRelaxation.values, never.values) << "alpha == 1 perturbed the matrix";
  EXPECT_EQ(withoutRelaxation.rhs, never.rhs) << "alpha == 1 perturbed the right-hand side";

  Index changedDiagonals = 0;
  Index changedRhs = 0;
  Index changedOffDiagonals = 0;
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = withRelaxation.rowOffsets[static_cast<std::size_t>(row)];
         d < withRelaxation.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      if (withRelaxation.values[d] == withoutRelaxation.values[d]) continue;
      if (withRelaxation.columnIndices[d] == row) {
        ++changedDiagonals;
      } else {
        ++changedOffDiagonals;
      }
    }
    if (withRelaxation.rhs[static_cast<std::size_t>(row)] !=
        withoutRelaxation.rhs[static_cast<std::size_t>(row)]) {
      ++changedRhs;
    }
  }
  EXPECT_EQ(changedDiagonals, n) << "under-relaxation left some diagonal untouched";
  EXPECT_EQ(changedRhs, n) << "under-relaxation left some rhs entry untouched";
  // Patankar relaxation is a DIAGONAL operation. Touching an off-diagonal would
  // be a different scheme.
  EXPECT_EQ(changedOffDiagonals, 0u) << "under-relaxation modified an off-diagonal coefficient";

  // And the relation is the frozen one, checked against the device's own
  // pre-relaxation diagonal rather than against a re-derivation of a_P.
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = withRelaxation.rowOffsets[static_cast<std::size_t>(row)];
         d < withRelaxation.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      if (withRelaxation.columnIndices[d] != row) continue;
      const Real unrelaxedDiagonal = withoutRelaxation.values[d];
      const Real extra = unrelaxedDiagonal * ((1.0 / alpha) - 1.0);
      EXPECT_EQ(withRelaxation.values[d], unrelaxedDiagonal + extra) << "row " << row;
      EXPECT_EQ(withRelaxation.rhs[static_cast<std::size_t>(row)],
                withoutRelaxation.rhs[static_cast<std::size_t>(row)] + (extra * problem.phi[row]))
          << "row " << row;
    }
  }
}

// =============================================================================
// NV3 -- the pre-relaxation-diagonal bug must be distinguishable.
// =============================================================================

TEST(DeviceScalarTransportSemantics, UsingTheRelaxedDiagonalInTheRhsIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;

  cfd::gpu::DeviceScalarTransportSystem device;
  assembleOnDevice(problem, buffers, false, device);
  const DeviceSnapshot assembled = download(device, n);
  const Real alpha = 0.7;
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, alpha, device);
  const DeviceSnapshot relaxed = download(device, n);

  const Real factor = (1.0 / alpha) - 1.0;
  Index rowsWhereTheBugWouldShow = 0;
  for (Index row = 0; row < n; ++row) {
    Real unrelaxedDiagonal = 0.0;
    for (std::size_t d = assembled.rowOffsets[static_cast<std::size_t>(row)];
         d < assembled.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      if (assembled.columnIndices[d] == row) unrelaxedDiagonal = assembled.values[d];
    }
    ASSERT_NE(unrelaxedDiagonal, 0.0) << "row " << row << " has no diagonal to relax";
    // The bug: the RHS correction built from a_P/alpha instead of a_P.
    const Real relaxedDiagonal = unrelaxedDiagonal + (unrelaxedDiagonal * factor);
    const Real wrongRhs = assembled.rhs[static_cast<std::size_t>(row)] +
                          ((relaxedDiagonal * factor) * problem.phi[row]);
    if (wrongRhs != relaxed.rhs[static_cast<std::size_t>(row)]) ++rowsWhereTheBugWouldShow;
  }
  EXPECT_EQ(rowsWhereTheBugWouldShow, n)
      << "the wrong (already-relaxed) diagonal produces the SAME right-hand side, so no test in "
         "this file can distinguish the two formulas";
}

// =============================================================================
// NV2 -- imposed rows must be load-bearing.
// =============================================================================

TEST(DeviceScalarTransportSemantics, DroppingOrPerturbingAnImposedRowBreaksTheComparison) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  const Real alpha = 0.7;
  const std::vector<ImposedCell> imposed = {ImposedCell{0, 4.0}, ImposedCell{(n / 2) + 3, 9.25},
                                            ImposedCell{n - 1, 0.5}};

  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;

  cfd::gpu::DeviceScalarTransportSystem device;
  assembleOnDevice(problem, buffers, false, device);
  const DeviceSnapshot assembled = download(device, n);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, alpha, device);
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);
  cfd::gpu::fixImposedRowsDevice(deviceImposed, device);
  const DeviceSnapshot fixed = download(device, n);

  // The control's baseline: the correct host list agrees bitwise.
  const Comparison good =
      compare(hostSemanticsFromDeviceAssembly(assembled, problem, alpha, imposed), fixed, n);
  ASSERT_TRUE(good.bitwise()) << "the control's own baseline does not agree: " << describe(good);

  // (a) drop one imposed row -- each of the three in turn.
  for (std::size_t drop = 0; drop < imposed.size(); ++drop) {
    std::vector<ImposedCell> reduced;
    for (std::size_t i = 0; i < imposed.size(); ++i) {
      if (i != drop) reduced.push_back(imposed[i]);
    }
    const Comparison c =
        compare(hostSemanticsFromDeviceAssembly(assembled, problem, alpha, reduced), fixed, n);
    EXPECT_FALSE(c.bitwise()) << "dropping imposed cell " << imposed[drop].cell
                              << " was NOT detected, so the comparison does not see imposed rows: "
                              << describe(c);
  }

  // (b) perturb one imposed VALUE -- the matrix row is identical either way, so
  // this is specifically a right-hand-side control.
  for (std::size_t perturb = 0; perturb < imposed.size(); ++perturb) {
    std::vector<ImposedCell> altered = imposed;
    altered[perturb].value += 1e-9;
    const Comparison c =
        compare(hostSemanticsFromDeviceAssembly(assembled, problem, alpha, altered), fixed, n);
    EXPECT_FALSE(c.bitwise()) << "perturbing imposed cell " << imposed[perturb].cell
                              << "'s value was NOT detected: " << describe(c);
    EXPECT_GT(c.maxRhsUlps, 0) << "the perturbation did not reach the right-hand side";
  }
}

// =============================================================================
// The imposed row's SHAPE -- a unit diagonal, explicit zeros, nothing else.
// =============================================================================

TEST(DeviceScalarTransportSemantics, AnImposedRowIsAUnitDiagonalAndExplicitZeros) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  const std::vector<ImposedCell> imposed = {ImposedCell{0, 4.0}, ImposedCell{(n / 2) + 3, 9.25},
                                            ImposedCell{n - 1, 0.5}};

  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;

  cfd::gpu::DeviceScalarTransportSystem reference;
  assembleOnDevice(problem, buffers, false, reference);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 0.7, reference);
  const DeviceSnapshot before = download(reference, n);

  cfd::gpu::DeviceScalarTransportSystem device;
  assembleOnDevice(problem, buffers, false, device);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 0.7, device);
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);
  cfd::gpu::fixImposedRowsDevice(deviceImposed, device);
  const DeviceSnapshot snapshot = download(device, n);

  for (const ImposedCell& entry : imposed) {
    bool sawDiagonal = false;
    for (std::size_t d = snapshot.rowOffsets[static_cast<std::size_t>(entry.cell)];
         d < snapshot.rowOffsets[static_cast<std::size_t>(entry.cell) + 1]; ++d) {
      if (snapshot.columnIndices[d] == entry.cell) {
        sawDiagonal = true;
        EXPECT_EQ(snapshot.values[d], 1.0) << "imposed row " << entry.cell << " diagonal";
      } else {
        EXPECT_EQ(snapshot.values[d], 0.0)
            << "imposed row " << entry.cell << " kept an off-diagonal coefficient";
      }
    }
    EXPECT_TRUE(sawDiagonal) << "imposed row " << entry.cell << " has no diagonal slot";
    EXPECT_EQ(snapshot.rhs[static_cast<std::size_t>(entry.cell)], entry.value)
        << "imposed row " << entry.cell << " right-hand side was accumulated, not overwritten";
  }

  // The rows that were NOT imposed must be untouched by fixImposedRowsDevice.
  for (Index row = 0; row < n; ++row) {
    const bool isImposed = std::any_of(imposed.begin(), imposed.end(),
                                       [row](const ImposedCell& e) { return e.cell == row; });
    if (isImposed) continue;
    for (std::size_t d = snapshot.rowOffsets[static_cast<std::size_t>(row)];
         d < snapshot.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      ASSERT_EQ(snapshot.values[d], before.values[d]) << "row " << row << " was collateral damage";
    }
    ASSERT_EQ(snapshot.rhs[static_cast<std::size_t>(row)],
              before.rhs[static_cast<std::size_t>(row)])
        << "row " << row << " rhs was collateral damage";
  }
}

// =============================================================================
// Ordering -- relax THEN impose, never the reverse.
// =============================================================================

TEST(DeviceScalarTransportSemantics, ImposingBeforeRelaxingProducesADifferentSystem) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  const Real alpha = 0.7;
  const std::vector<ImposedCell> imposed = {ImposedCell{0, 4.0}, ImposedCell{n - 1, 0.5}};

  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);

  cfd::gpu::DeviceScalarTransportSystem correct;
  assembleOnDevice(problem, buffers, false, correct);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, alpha, correct);
  cfd::gpu::fixImposedRowsDevice(deviceImposed, correct);
  const DeviceSnapshot correctOrder = download(correct, n);

  cfd::gpu::DeviceScalarTransportSystem swapped;
  assembleOnDevice(problem, buffers, false, swapped);
  cfd::gpu::fixImposedRowsDevice(deviceImposed, swapped);
  cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, alpha, swapped);
  const DeviceSnapshot wrongOrder = download(swapped, n);

  // If these agreed, "the order is load-bearing" would be a claim with no
  // evidence and a later refactor could swap the two calls freely.
  EXPECT_NE(correctOrder.values, wrongOrder.values)
      << "imposing before relaxing produced the same matrix, so the documented order is not "
         "actually load-bearing";
  for (const ImposedCell& entry : imposed) {
    const std::size_t row = static_cast<std::size_t>(entry.cell);
    EXPECT_EQ(correctOrder.rhs[row], entry.value);
    EXPECT_NE(wrongOrder.rhs[row], entry.value)
        << "relaxing after imposing left the imposed value intact, which would make the ordering "
           "harmless -- it is not";
  }
}

// =============================================================================
// Determinism and input validation.
// =============================================================================

TEST(DeviceScalarTransportSemantics, TheSameInputsProduceTheSameBytesTwice) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  const std::vector<ImposedCell> imposed = {ImposedCell{0, 4.0}, ImposedCell{(n / 2) + 3, 9.25},
                                            ImposedCell{n - 1, 0.5}};
  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);

  DeviceSnapshot first;
  DeviceSnapshot second;
  for (int pass = 0; pass < 2; ++pass) {
    cfd::gpu::DeviceScalarTransportSystem system;
    assembleOnDevice(problem, buffers, false, system);
    cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 0.7, system);
    cfd::gpu::fixImposedRowsDevice(deviceImposed, system);
    (pass == 0 ? first : second) = download(system, n);
  }
  EXPECT_EQ(first.rowOffsets, second.rowOffsets);
  EXPECT_EQ(first.columnIndices, second.columnIndices);
  EXPECT_EQ(first.values, second.values);
  EXPECT_EQ(first.rhs, second.rhs);
}

TEST(DeviceScalarTransportSemantics, InvalidInputsAreRefused) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Problem problem(MeshGeometry::createCartesian2D(6, 5, 1.0, 1.0));
  const Index n = problem.cellCount();
  DeviceBuffers buffers;
  std::string why;
  ASSERT_TRUE(prepareDevice(problem, buffers, why)) << why;

  cfd::gpu::DeviceScalarTransportSystem system;
  assembleOnDevice(problem, buffers, false, system);

  // alpha outside (0, 1], matching applyImplicitUnderRelaxation's own contract.
  EXPECT_THROW(cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 0.0, system),
               std::runtime_error);
  EXPECT_THROW(cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, -0.5, system),
               std::runtime_error);
  EXPECT_THROW(cfd::gpu::applyImplicitUnderRelaxationDevice(buffers.phi, 1.5, system),
               std::runtime_error);

  cfd::gpu::DeviceImposedCells imposed;
  EXPECT_THROW(cfd::gpu::uploadImposedCellsDevice({n}, {1.0}, n, imposed), std::runtime_error);
  EXPECT_THROW(cfd::gpu::uploadImposedCellsDevice({0}, {std::nan("")}, n, imposed),
               std::runtime_error);
  EXPECT_THROW(cfd::gpu::uploadImposedCellsDevice({0, 1}, {1.0}, n, imposed), std::runtime_error);
  // The duplicate refusal: two threads, one row, a scheduler-dependent winner.
  EXPECT_THROW(cfd::gpu::uploadImposedCellsDevice({2, 2}, {1.0, 2.0}, n, imposed),
               std::runtime_error);
}

// =============================================================================
// The host replica is tied to production, so it cannot drift.
// =============================================================================

TEST(DeviceScalarTransportSemantics, ReplicaReproducesTheProductionSolve) {
  const Problem problem(MeshGeometry::createCartesian2D(12, 9, 1.0, 1.0));
  const Index n = problem.cellCount();
  const Real alpha = 0.7;
  const std::vector<ImposedCell> imposed = {ImposedCell{0, 4.0}, ImposedCell{(n / 2) + 3, 9.25},
                                            ImposedCell{n - 1, 0.5}};

  // hostReference() above duplicates solveRelaxedScalarTransport's assembly
  // because production exposes only the SOLVED FIELD and what has to be
  // compared is the SYSTEM. This test is what stops the duplicate from
  // drifting: solve the replica with the same solver and require the same
  // field. A floor far below every value keeps production's max() inert, so
  // the only thing under comparison is the system.
  const cfd::algebra::LinearSolverSettings settings;
  const ScalarField production = cfd::turbulence::solveRelaxedScalarTransport(
      problem.mesh, problem.phi, problem.massFlux, problem.gamma, problem.boundaries, problem.Su,
      problem.Sp, alpha, /*floorValue=*/-1.0e30, settings, {}, imposed);

  HostSystem replica = hostReference(problem, alpha, {}, imposed);
  Vector guess(n, 0.0);
  for (Index i = 0; i < n; ++i) guess[i] = problem.phi[i];
  const cfd::algebra::BiCGSTAB solver(settings);
  const auto solved =
      solver.solve(cfd::algebra::LinearSystem(std::move(replica.matrix), replica.rhs), guess);
  ASSERT_TRUE(solved.converged());

  // PRODUCTION HAS A POST-SOLVE STEP THIS REPLICA DOES NOT, and finding it here
  // is the point of the tie-back. solveRelaxedScalarTransport re-asserts every
  // imposed value AFTER the solve and after the floor, because a fixed row is a
  // boundary condition and an iterative solver only approaches it -- measured
  // here at 1.5e-10 on all three imposed cells. So an imposed cell's final
  // value does NOT come from the linear system, and a device k-epsilon
  // integration owes the same re-assertion after its own solve; fixing the row
  // is necessary and not sufficient.
  //
  // Applying that step makes the three imposed cells equal on both sides by
  // construction, so they stop discriminating -- the comparison below is
  // carried by the other cells, and the imposed ROWS themselves are already
  // pinned bitwise at system level by G1/G2 above.
  ScalarField replicaField(n, 0.0);
  for (Index i = 0; i < n; ++i) replicaField[i] = solved.solution[i];
  for (const ImposedCell& entry : imposed) replicaField[entry.cell] = entry.value;

  Index discriminatingCells = 0;
  for (Index i = 0; i < n; ++i) {
    const bool isImposed = std::any_of(imposed.begin(), imposed.end(),
                                       [i](const ImposedCell& e) { return e.cell == i; });
    if (!isImposed) ++discriminatingCells;
    EXPECT_EQ(replicaField[i], production[i])
        << "cell " << i
        << ": the host replica in this file no longer reproduces "
           "solveRelaxedScalarTransport, so every comparison here is measuring the wrong thing";
  }
  EXPECT_EQ(discriminatingCells, n - imposed.size())
      << "the tie-back would rest only on cells that agree by construction";
}
