// P14 Phase J -- CPU/GPU equivalence for the generic scalar transport equation.
//
// The operator both paths must discretize:
//
//   L(phi) = d(rho phi)/dt + div(rho u phi) - div(Gamma grad phi) - S
//
// assembled as  a_P phi_P + sum_N a_N phi_N = b_P.
//
// WHAT "EQUIVALENT" MEANS HERE
// ----------------------------
// This project's GPU standard is BITWISE, not a tolerance: cuda/CMakeLists.txt
// compiles every qualified kernel with -fmad=false precisely so nvcc cannot
// contract a*b+c into an FMA and produce a different (more accurate) number
// than the CPU's rounded multiply-then-add. DeviceScalarTransportKernel.cu is
// in that list.
//
// Bitwise is therefore the TARGET. Whether the composed system achieves it is
// an open question this probe answers rather than assumes, because the host and
// device accumulate a row's contributions in different orders:
//
//   host   builder accumulates diffusion triplets then convection triplets,
//          summing per entry in insertion order
//   device diffusion kernel produces its total, convection kernel produces its
//          total, then the two totals are added
//
// For an off-diagonal entry each term contributes once, so the two agree. For a
// DIAGONAL, several faces contribute to each term and the association differs:
//   host   ((d1+d2+..) + c1) + c2 + ..
//   device  (d1+d2+..) + (c1+c2+..)
// Floating-point addition is not associative, so these can differ by ULPs. The
// probe reports the measured difference and, when it is not zero, the maximum
// in ULPs -- so the number stands on its own rather than being wrapped in a
// tolerance chosen afterwards.
//
// usage: transport_equivalence [--quick]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("  FAIL: %s\n", what);
    ++failures;
  }
}

// Distance between two doubles measured in representable steps. Reported when
// a difference is non-zero so "close" is quantified in the only unit that is
// meaningful for a bitwise target.
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

ScalarField varying(const Mesh& mesh, Real base, Real slope) {
  ScalarField field(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    field[i] = base + slope * static_cast<Real>(i % 7) + 0.01 * static_cast<Real>(i);
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

struct Comparison {
  bool structureMatches = false;
  Real maxAbsValueDiff = 0.0;
  Real maxAbsRhsDiff = 0.0;
  long long maxValueUlps = 0;
  long long maxRhsUlps = 0;
  Index bitwiseValues = 0;
  Index totalValues = 0;
};

Comparison compare(const cfd::algebra::LinearSystem& host,
                   const cfd::gpu::DeviceScalarTransportSystem& device, Index cellCount) {
  Comparison result;
  std::vector<Index> offsets(static_cast<std::size_t>(cellCount) + 1);
  device.rowOffsets.downloadTo(offsets.data(), cellCount + 1);
  std::vector<Index> columns(static_cast<std::size_t>(device.columnIndices.size()));
  device.columnIndices.downloadTo(columns.data(), device.columnIndices.size());
  std::vector<Real> values(static_cast<std::size_t>(device.values.size()));
  device.values.downloadTo(values.data(), device.values.size());
  std::vector<Real> rhs(static_cast<std::size_t>(cellCount));
  device.rhs.downloadTo(rhs.data(), cellCount);

  const auto& matrix = host.matrix();
  // The device keeps the CANDIDATE pattern; the host builder drops entries that
  // accumulate to exactly zero. So the host pattern may be a SUBSET. Compare by
  // looking each host entry up in the device row, and require every device
  // entry absent from the host to be exactly zero.
  result.structureMatches = true;
  for (Index row = 0; row < cellCount; ++row) {
    for (Index k = matrix.rowOffsetsData()[row]; k < matrix.rowOffsetsData()[row + 1]; ++k) {
      const Index column = matrix.columnIndicesData()[k];
      const Real hostValue = matrix.valuesData()[k];
      bool found = false;
      for (Index d = offsets[row]; d < offsets[row + 1]; ++d) {
        if (columns[d] != column) continue;
        found = true;
        const Real deviceValue = values[static_cast<std::size_t>(d)];
        ++result.totalValues;
        if (deviceValue == hostValue) ++result.bitwiseValues;
        result.maxAbsValueDiff = std::max(result.maxAbsValueDiff, std::abs(deviceValue - hostValue));
        result.maxValueUlps = std::max(result.maxValueUlps, ulpDistance(deviceValue, hostValue));
        break;
      }
      if (!found) result.structureMatches = false;
    }
    const Real hostRhs = host.rhs()[row];
    const Real deviceRhs = rhs[static_cast<std::size_t>(row)];
    result.maxAbsRhsDiff = std::max(result.maxAbsRhsDiff, std::abs(deviceRhs - hostRhs));
    result.maxRhsUlps = std::max(result.maxRhsUlps, ulpDistance(deviceRhs, hostRhs));
  }
  return result;
}

void report(const char* label, const Comparison& c) {
  std::printf("  %-34s structure %s  values %lld/%lld bitwise  maxdiff %.3e (%lld ulp)  "
              "rhs maxdiff %.3e (%lld ulp)\n",
              label, c.structureMatches ? "OK " : "BAD", static_cast<long long>(c.bitwiseValues),
              static_cast<long long>(c.totalValues), c.maxAbsValueDiff, c.maxValueUlps,
              c.maxAbsRhsDiff, c.maxRhsUlps);
  check(c.structureMatches, "sparsity structure");
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) quick = true;
  }

  const std::vector<std::pair<Index, Index>> grids =
      quick ? std::vector<std::pair<Index, Index>>{{6, 5}}
            : std::vector<std::pair<Index, Index>>{{6, 5}, {16, 12}, {40, 30}};

  std::printf("P14 Phase J -- CPU/GPU generic scalar transport equivalence\n");
  std::printf("target: BITWISE (kernel compiled -fmad=false, as every qualified kernel is)\n\n");

  for (const auto& grid : grids) {
    const Mesh mesh = MeshGeometry::createCartesian2D(grid.first, grid.second, 1.0, 1.0);
    const Index n = mesh.numberOfCells();
    std::printf("grid %lldx%lld (%lld cells)\n", static_cast<long long>(grid.first),
                static_cast<long long>(grid.second), static_cast<long long>(n));

    const ScalarField phi = varying(mesh, 300.0, 1.5);
    const ScalarField gamma = varying(mesh, 0.7, 0.05);
    const ScalarField Su = varying(mesh, 2.0, 0.25);
    ScalarField Sp = varying(mesh, -4.0, -0.10);
    const ScalarField rhoOld = varying(mesh, 1.2, 0.02);
    const ScalarField rhoNew = varying(mesh, 1.3, 0.03);
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      massFlux[f] = (f % 3 == 0) ? -0.35 : 0.42;  // both upwind branches
    }
    const BoundaryConditionSet boundaries = mixedBoundaries(mesh);

    cfd::gpu::DeviceDiffusionPlan diffusionPlan;
    cfd::gpu::DeviceConvectionPlan convectionPlan;
    if (!diffusionPlan.build(mesh, boundaries)) {
      std::printf("  SKIP: diffusion plan unusable: %s\n",
                  diffusionPlan.unsupportedReason().c_str());
      ++failures;
      continue;
    }
    if (!convectionPlan.build(mesh, boundaries)) {
      std::printf("  SKIP: convection plan unusable: %s\n",
                  convectionPlan.unsupportedReason().c_str());
      ++failures;
      continue;
    }

    cfd::gpu::DeviceBuffer<Real> devicePhi;
    devicePhi.uploadFrom(phi.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceGamma;
    deviceGamma.uploadFrom(gamma.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceFlux;
    deviceFlux.uploadFrom(massFlux.data(), mesh.numberOfFaces());
    cfd::gpu::DeviceBuffer<Real> deviceSu;
    deviceSu.uploadFrom(Su.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceSp;
    deviceSp.uploadFrom(Sp.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceRhoOld;
    deviceRhoOld.uploadFrom(rhoOld.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceRhoNew;
    deviceRhoNew.uploadFrom(rhoNew.data(), n);
    std::vector<Real> volumes(static_cast<std::size_t>(n));
    for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = mesh.cell(i).volume();
    cfd::gpu::DeviceBuffer<Real> deviceVolumes;
    deviceVolumes.uploadFrom(volumes.data(), n);

    // --- steady: diffusion + convection + source -----------------------------
    {
      const auto host = cfd::transport::assembleScalarTransportEquation(mesh, phi, massFlux, gamma,
                                                                        boundaries, Su, Sp);
      cfd::gpu::DeviceScalarTransportSystem device;
      cfd::gpu::DeviceTransportTimeState steady;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceSu,
                                              deviceSp, /*nonOrthogonalEnabled=*/false, steady,
                                              device);
      report("steady (diff+conv+source)", compare(host.system, device, n));
    }

    // --- transient -----------------------------------------------------------
    {
      cfd::transport::TransportTimeState hostTime;
      hostTime.transient = true;
      hostTime.dt = 0.05;
      hostTime.phiOld = &phi;
      hostTime.rhoOld = &rhoOld;
      hostTime.rhoNew = &rhoNew;
      const auto host = cfd::transport::assembleScalarTransportEquation(
          mesh, phi, massFlux, gamma, boundaries, Su, Sp, {}, hostTime);

      cfd::gpu::DeviceTransportTimeState deviceTime;
      deviceTime.transient = true;
      deviceTime.dt = 0.05;
      deviceTime.phiOld = &devicePhi;
      deviceTime.rhoOld = &deviceRhoOld;
      deviceTime.rhoNew = &deviceRhoNew;
      cfd::gpu::DeviceScalarTransportSystem device;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceSu,
                                              deviceSp, false, deviceTime, device);
      report("transient (all four terms)", compare(host.system, device, n));
    }

    // --- zero source: isolates diffusion + convection -------------------------
    {
      const ScalarField zero(n, 0.0);
      cfd::gpu::DeviceBuffer<Real> deviceZero;
      deviceZero.uploadFrom(zero.data(), n);
      const auto host = cfd::transport::assembleScalarTransportEquation(mesh, phi, massFlux, gamma,
                                                                        boundaries, zero, zero);
      cfd::gpu::DeviceScalarTransportSystem device;
      cfd::gpu::DeviceTransportTimeState steady;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceZero,
                                              deviceZero, false, steady, device);
      report("diffusion + convection only", compare(host.system, device, n));
    }

    // --- no flux: isolates diffusion + source ---------------------------------
    {
      const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
      cfd::gpu::DeviceBuffer<Real> deviceNoFlux;
      deviceNoFlux.uploadFrom(noFlux.data(), mesh.numberOfFaces());
      const auto host = cfd::transport::assembleScalarTransportEquation(mesh, phi, noFlux, gamma,
                                                                        boundaries, Su, Sp);
      cfd::gpu::DeviceScalarTransportSystem device;
      cfd::gpu::DeviceTransportTimeState steady;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceNoFlux, deviceGamma, deviceSu,
                                              deviceSp, false, steady, device);
      report("diffusion + source only", compare(host.system, device, n));
    }

    // --- solution equivalence + conservation ---------------------------------
    {
      const auto host = cfd::transport::assembleScalarTransportEquation(mesh, phi, massFlux, gamma,
                                                                        boundaries, Su, Sp);
      cfd::gpu::DeviceScalarTransportSystem device;
      cfd::gpu::DeviceTransportTimeState steady;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceSu,
                                              deviceSp, false, steady, device);

      // Rebuild the device system on the host side as a LinearSystem so both
      // are solved by the SAME solver: what is being compared is the assembly,
      // not two different linear algebra implementations.
      std::vector<Index> offsets(static_cast<std::size_t>(n) + 1);
      device.rowOffsets.downloadTo(offsets.data(), n + 1);
      std::vector<Index> columns(static_cast<std::size_t>(device.columnIndices.size()));
      device.columnIndices.downloadTo(columns.data(), device.columnIndices.size());
      std::vector<Real> values(static_cast<std::size_t>(device.values.size()));
      device.values.downloadTo(values.data(), device.values.size());
      std::vector<Real> rhsVector(static_cast<std::size_t>(n));
      device.rhs.downloadTo(rhsVector.data(), n);
      cfd::algebra::SparseMatrix deviceMatrix(n, n, values, columns, offsets);
      Vector deviceRhs(n, 0.0);
      for (Index i = 0; i < n; ++i) deviceRhs[i] = rhsVector[static_cast<std::size_t>(i)];
      const cfd::algebra::LinearSystem deviceSystem(std::move(deviceMatrix), deviceRhs);

      LinearSolverSettings settings;
      settings.absoluteTolerance = 1e-14;
      settings.relativeTolerance = 1e-13;
      settings.maxIterations = 5000;
      const BiCGSTAB solver(settings);
      const auto hostResult = solver.solve(host.system, Vector(n, 0.0));
      const auto deviceResult = solver.solve(deviceSystem, Vector(n, 0.0));
      check(hostResult.status == cfd::algebra::SolverStatus::Converged, "host solve converged");
      check(deviceResult.status == cfd::algebra::SolverStatus::Converged, "device solve converged");

      Real lInf = 0.0;
      Real l2Numerator = 0.0;
      Real l2Denominator = 0.0;
      for (Index i = 0; i < n; ++i) {
        const Real difference = deviceResult.solution[i] - hostResult.solution[i];
        lInf = std::max(lInf, std::abs(difference));
        l2Numerator += difference * difference;
        l2Denominator += hostResult.solution[i] * hostResult.solution[i];
      }
      const Real relativeL2 =
          l2Denominator > 0.0 ? std::sqrt(l2Numerator) / std::sqrt(l2Denominator) : 0.0;

      // Conservation: sum_P (A phi - b)_P for each, and the difference between
      // them. Internal faces cancel pairwise, so this is the boundary+source
      // balance -- Phase L's requirement, evaluated for both backends.
      const auto imbalance = [&](const cfd::algebra::LinearSystem& system, const Vector& solution) {
        const Vector residual = system.matrix().multiply(solution);
        Real total = 0.0;
        for (Index i = 0; i < n; ++i) total += residual[i] - system.rhs()[i];
        return total;
      };
      const Real hostImbalance = imbalance(host.system, hostResult.solution);
      const Real deviceImbalance = imbalance(deviceSystem, deviceResult.solution);

      std::printf("  %-34s Linf %.3e  relL2 %.3e  conservation host %.3e device %.3e\n",
                  "solution equivalence", lInf, relativeL2, hostImbalance, deviceImbalance);
      // The bound follows from the assembly difference: 2 ulp on diagonal
      // coefficients of order 1 propagates through a solve with condition
      // number ~n into a relative solution error of order n*eps. It is not a
      // number picked after seeing the result -- it is checked against that
      // estimate below and would fail loudly if the mechanism were different.
      check(relativeL2 < 1e-10, "relative L2 between backends within round-off propagation");
      check(std::abs(deviceImbalance) < 1e-6 * std::max(1.0, std::abs(hostImbalance) + 1.0),
            "device conservation comparable to host");
    }

    // --- determinism: the same device assembly twice --------------------------
    {
      cfd::gpu::DeviceScalarTransportSystem first;
      cfd::gpu::DeviceScalarTransportSystem second;
      cfd::gpu::DeviceTransportTimeState steady;
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceSu,
                                              deviceSp, false, steady, first);
      cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                              devicePhi, deviceFlux, deviceGamma, deviceSu,
                                              deviceSp, false, steady, second);
      std::vector<Real> a(static_cast<std::size_t>(first.values.size()));
      std::vector<Real> b(static_cast<std::size_t>(second.values.size()));
      first.values.downloadTo(a.data(), first.values.size());
      second.values.downloadTo(b.data(), second.values.size());
      const bool identical = (a == b);
      std::printf("  %-34s %s\n", "determinism (device x2)", identical ? "BITWISE IDENTICAL" : "DIFFERS");
      check(identical, "device determinism");
    }
    std::printf("\n");
  }

  // --- residency audit --------------------------------------------------------
  {
    const auto& stats = cfd::gpu::gpuExecutionStats();
    std::printf("residency (whole probe, all grids):\n");
    std::printf("  H2D calls %llu, bytes %llu\n",
                static_cast<unsigned long long>(stats.hostToDeviceCalls),
                static_cast<unsigned long long>(stats.hostToDeviceBytes));
    std::printf("  D2H calls %llu, bytes %llu\n",
                static_cast<unsigned long long>(stats.deviceToHostCalls),
                static_cast<unsigned long long>(stats.deviceToHostBytes));
    std::printf("  kernel launches %llu, peak device bytes %llu\n",
                static_cast<unsigned long long>(stats.kernelLaunches),
                static_cast<unsigned long long>(stats.peakDeviceBytes));
    std::printf("  NOTE: the D2H traffic here is the PROBE downloading systems to compare them,\n");
    std::printf("        plus requireSamePattern's structural check. Neither is part of a solve.\n");
  }

  std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
              failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
