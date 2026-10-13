// P16-GPU-TURB Stage 4 -- the k transport equation, CPU against GPU.
//
// WHAT THE AUDIT SETTLED, AND WHY IT MATTERS HERE
// ----------------------------------------------
// KEpsilonModel::correct builds k like this, and the device must match it
// operation for operation:
//
//   Gamma_k = mu + mu_t/sigma_k              computeEffectiveDiffusivity
//   Su      = P_k                            computeKSourceTerms
//   Sp      = -rho*epsilon/max(k, kFloor)    computeKSourceTerms
//   solve   = assemble -> under-relax(0.7) -> solve -> floor
//
// THE DECISIVE FINDING: **k has NO imposed cells.** Only epsilon does. The
// model says so in its own words -- "Only epsilon is imposed. k keeps its own
// boundary condition and responds to the modified production -- imposing both
// over-constrains the wall cell." So the wall treatment reaches k through ONE
// channel only: the production field, whose wall-cell entries are replaced
// before Su is built.
//
// That is why this file passes an EMPTY imposed list and asserts the k row of
// a wall cell keeps its assembled coefficients. Reproducing epsilon's
// imposed-row treatment here would be a different equation.
//
// THE FROZEN GATE
//   4B  Gamma_k               BITWISE against computeEffectiveDiffusivity
//   4C  Su / Sp               BITWISE against computeKSourceTerms
//   4D  matrix + RHS          structure exact BOTH directions; values within
//                             P14's already-frozen 2 ulp (the assembly's
//                             association-order difference, measured and
//                             frozen there, not re-derived here)
//   4E  solved k              within P14's already-frozen 2.1e-12 relative L2
//   4F  wall semantics        no imposed rows for k; the wall enters via P_k
//
// Nothing in this file invents a threshold. 4B and 4C are cell-local algebra,
// so bitwise is the only defensible bar. 4D and 4E inherit P14's measured
// numbers because the assembly they exercise is P14's assembly.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"
#include "cfd/transport/ScalarTransport.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"

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

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

// KEpsilonConfig / KEpsilonCoefficients production defaults.
constexpr Real kDensity = 1.225;
constexpr Real kMolecularViscosity = 1.8e-5;
constexpr Real kSigmaK = 1.0;
constexpr Real kCMu = 0.09;
constexpr Real kKFloor = 1e-10;
constexpr Real kEpsilonFloor = 1e-10;
constexpr Real kRelaxation = 0.7;

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

ScalarField varying(Index n, Real base, Real slope, Index modulus) {
  ScalarField field(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    field[i] = base + (slope * static_cast<Real>(i)) + (0.31 * static_cast<Real>(i % modulus));
  }
  return field;
}

// The k-equation problem, with every field the production path would supply.
struct KProblem {
  Mesh mesh;
  ScalarField k;
  ScalarField epsilon;
  ScalarField turbulentViscosity;
  ScalarField production;
  SurfaceField massFlux;
  BoundaryConditionSet boundaries;

  explicit KProblem(Index nx, Index ny)
      : mesh(MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0)),
        k(varying(mesh.numberOfCells(), 0.42, 0.011, 5)),
        epsilon(varying(mesh.numberOfCells(), 0.27, 0.007, 3)),
        turbulentViscosity(mesh.numberOfCells(), 0.0),
        production(varying(mesh.numberOfCells(), 1.7, 0.09, 7)),
        massFlux(mesh.numberOfFaces(), 0.0) {
    const Index n = mesh.numberOfCells();
    // mu_t from the production relation, so Gamma_k is a realistic field
    // rather than something invented for the test.
    turbulentViscosity = cfd::turbulence::computeTurbulentViscosityField(
        mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      massFlux[f] = (f % 3 == 0) ? -0.28 : 0.41;  // both upwind branches
    }
    boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.4));
    boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.2));
    boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
    boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
    (void)n;
  }

  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }
};

std::vector<Real> download(const cfd::gpu::DeviceBuffer<Real>& buffer, Index n) {
  std::vector<Real> host(static_cast<std::size_t>(n), 0.0);
  buffer.downloadTo(host.data(), n);
  return host;
}

void expectBitwise(const ScalarField& host, const std::vector<Real>& device, const char* what) {
  ASSERT_EQ(static_cast<std::size_t>(host.size()), device.size()) << what;
  Index mismatches = 0;
  std::string first;
  for (Index i = 0; i < host.size(); ++i) {
    if (device[static_cast<std::size_t>(i)] == host[i]) continue;
    ++mismatches;
    if (first.empty()) {
      first = "cell " + std::to_string(i) + ": host " + std::to_string(host[i]) + " device " +
              std::to_string(device[static_cast<std::size_t>(i)]) + " (" +
              std::to_string(ulpDistance(device[static_cast<std::size_t>(i)], host[i])) + " ulp)";
    }
  }
  EXPECT_EQ(mismatches, 0u) << what << ": " << mismatches << " cells differ. First: " << first;
}

// --- the CPU k system -------------------------------------------------------
//
// The body of solveRelaxedScalarTransport up to the solve, with the k
// equation's operands. Tied to production by KSolveMatchesProduction below.
struct HostSystem {
  SparseMatrix matrix;
  Vector rhs;
};

HostSystem hostKSystem(const KProblem& p, const ScalarField& gammaK, const ScalarField& Su,
                       const ScalarField& Sp) {
  const Index n = p.cellCount();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  cfd::transport::assembleScalarDiffusionContribution(p.mesh, gammaK, p.k, p.boundaries, builder,
                                                      rhs, {});
  cfd::transport::assembleScalarConvectionContribution(p.mesh, p.massFlux, p.k, p.boundaries,
                                                       builder, rhs);
  cfd::transport::applyImplicitScalarSource(p.mesh, builder, rhs, Su, Sp);
  Vector kOld(n, 0.0);
  for (Index i = 0; i < n; ++i) kOld[i] = p.k[i];
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, kOld, kRelaxation);
  // NO fixRow: k has no imposed cells.
  return HostSystem{builder.build(), rhs};
}

struct DeviceSnapshot {
  std::vector<Index> rowOffsets;
  std::vector<Index> columnIndices;
  std::vector<Real> values;
  std::vector<Real> rhs;
};

DeviceSnapshot snapshot(const cfd::gpu::DeviceScalarTransportSystem& system, Index n) {
  DeviceSnapshot out;
  out.rowOffsets.resize(static_cast<std::size_t>(n) + 1);
  system.rowOffsets.downloadTo(out.rowOffsets.data(), n + 1);
  out.columnIndices.resize(static_cast<std::size_t>(system.columnIndices.size()));
  system.columnIndices.downloadTo(out.columnIndices.data(), system.columnIndices.size());
  out.values.resize(static_cast<std::size_t>(system.values.size()));
  system.values.downloadTo(out.values.data(), system.values.size());
  out.rhs.resize(static_cast<std::size_t>(n));
  system.rhs.downloadTo(out.rhs.data(), n);
  return out;
}

struct Comparison {
  bool structureOk = true;
  long long maxValueUlps = 0;
  long long maxRhsUlps = 0;
  Index compared = 0;
  Index bitwise = 0;
  std::string firstDifference;
};

// Bidirectional, as established in Stage 0: the host builder drops exact
// zeros, so the host pattern is a SUBSET, and every device entry with no host
// counterpart must itself be exactly zero.
Comparison compareSystems(const HostSystem& host, const DeviceSnapshot& device, Index n) {
  Comparison c;
  const SparseMatrix& m = host.matrix;
  for (Index row = 0; row < n; ++row) {
    const std::size_t begin = device.rowOffsets[static_cast<std::size_t>(row)];
    const std::size_t end = device.rowOffsets[static_cast<std::size_t>(row) + 1];
    for (Index kk = m.rowOffsetsData()[row]; kk < m.rowOffsetsData()[row + 1]; ++kk) {
      const Index column = m.columnIndicesData()[kk];
      bool found = false;
      for (std::size_t d = begin; d < end; ++d) {
        if (device.columnIndices[d] != column) continue;
        found = true;
        ++c.compared;
        const Real hv = m.valuesData()[kk];
        const Real dv = device.values[d];
        if (dv == hv) ++c.bitwise;
        const long long u = ulpDistance(dv, hv);
        if (u > c.maxValueUlps) c.maxValueUlps = u;
        if (u != 0 && c.firstDifference.empty()) {
          c.firstDifference = "row " + std::to_string(row) + " col " + std::to_string(column) +
                              ": " + std::to_string(u) + " ulp";
        }
        break;
      }
      if (!found) {
        c.structureOk = false;
        if (c.firstDifference.empty()) {
          c.firstDifference = "host entry (" + std::to_string(row) + "," + std::to_string(column) +
                              ") missing on device";
        }
      }
    }
    for (std::size_t d = begin; d < end; ++d) {
      const Index column = device.columnIndices[d];
      bool inHost = false;
      for (Index kk = m.rowOffsetsData()[row]; kk < m.rowOffsetsData()[row + 1]; ++kk) {
        if (m.columnIndicesData()[kk] == column) {
          inHost = true;
          break;
        }
      }
      if (!inHost && device.values[d] != 0.0) {
        c.structureOk = false;
        if (c.firstDifference.empty()) {
          c.firstDifference = "device-only entry (" + std::to_string(row) + "," +
                              std::to_string(column) + ") is not zero";
        }
      }
    }
    const long long ru = ulpDistance(device.rhs[static_cast<std::size_t>(row)], host.rhs[row]);
    if (ru > c.maxRhsUlps) c.maxRhsUlps = ru;
  }
  return c;
}

// Assembles the k system on device exactly as the host orders it.
void assembleKOnDevice(const KProblem& p, cfd::gpu::DeviceDiffusionPlan& diffusionPlan,
                       cfd::gpu::DeviceConvectionPlan& convectionPlan,
                       cfd::gpu::DeviceScalarTransportSystem& system,
                       cfd::gpu::DeviceBuffer<Real>& deviceGammaK,
                       cfd::gpu::DeviceBuffer<Real>& deviceSu,
                       cfd::gpu::DeviceBuffer<Real>& deviceSp) {
  const Index n = p.cellCount();
  cfd::gpu::DeviceBuffer<Real> deviceK;
  deviceK.uploadFrom(p.k.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
  deviceEpsilon.uploadFrom(p.epsilon.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(p.turbulentViscosity.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceProduction;
  deviceProduction.uploadFrom(p.production.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceFlux;
  deviceFlux.uploadFrom(p.massFlux.data(), p.mesh.numberOfFaces());
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = p.mesh.cell(i).volume();
  cfd::gpu::DeviceBuffer<Real> deviceVolumes;
  deviceVolumes.uploadFrom(volumes.data(), n);

  cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaK,
                                              deviceGammaK);
  cfd::gpu::computeKSourceTermsDevice(kDensity, deviceK, deviceEpsilon, deviceProduction, kKFloor,
                                      deviceSu, deviceSp);

  const cfd::gpu::DeviceTransportTimeState steady;
  cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes, deviceK,
                                          deviceFlux, deviceGammaK, deviceSu, deviceSp,
                                          /*nonOrthogonalEnabled=*/false, steady, system);
  cfd::gpu::applyImplicitUnderRelaxationDevice(deviceK, kRelaxation, system);
  // No imposed rows: k has none.
}

}  // namespace

// =============================================================================
// 4B -- the effective diffusivity.
// =============================================================================

TEST(DeviceKTransport, EffectiveDiffusivityIsBitwise) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const KProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(p.turbulentViscosity.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceGamma;
  cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaK, deviceGamma);

  const ScalarField hostGamma = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaK);
  expectBitwise(hostGamma, download(deviceGamma, n), "Gamma_k");

  // The closure is mu + mu_t/sigma, NOT (mu + mu_t)/sigma. With sigma_k = 1
  // those coincide, so the discriminating check uses a sigma that is not 1 --
  // otherwise this gate would pass for a kernel implementing the wrong one.
  const Real sigmaEpsilon = 1.3;
  cfd::gpu::DeviceBuffer<Real> deviceGammaEps;
  cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, sigmaEpsilon,
                                              deviceGammaEps);
  const ScalarField hostGammaEps = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, sigmaEpsilon);
  expectBitwise(hostGammaEps, download(deviceGammaEps, n), "Gamma with sigma != 1");

  const std::vector<Real> wrongClosure = download(deviceGammaEps, n);
  Index differsFromWrongClosure = 0;
  for (Index i = 0; i < n; ++i) {
    const Real wrong = (kMolecularViscosity + p.turbulentViscosity[i]) / sigmaEpsilon;
    if (wrong != wrongClosure[static_cast<std::size_t>(i)]) ++differsFromWrongClosure;
  }
  EXPECT_EQ(differsFromWrongClosure, n)
      << "the kernel agrees with (mu+mu_t)/sigma, so this gate cannot tell the two closures apart";
}

TEST(DeviceKTransport, EffectiveDiffusivityFailsClosedLikeTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 8;
  std::vector<Real> muT(static_cast<std::size_t>(n), 1.0);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  cfd::gpu::DeviceBuffer<Real> gamma;

  deviceMuT.uploadFrom(muT.data(), n);
  EXPECT_THROW(cfd::gpu::computeEffectiveDiffusivityDevice(0.0, deviceMuT, kSigmaK, gamma),
               std::runtime_error);
  EXPECT_THROW(
      cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, 0.0, gamma),
      std::runtime_error);

  // A negative mu_t would make an anti-diffusive matrix that still solves.
  muT[3] = -1e-6;
  deviceMuT.uploadFrom(muT.data(), n);
  EXPECT_THROW(
      cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaK, gamma),
      std::runtime_error);

  muT[3] = std::nan("");
  deviceMuT.uploadFrom(muT.data(), n);
  EXPECT_THROW(
      cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaK, gamma),
      std::runtime_error);
}

// =============================================================================
// 4C -- the source coefficients, before any assembly.
// =============================================================================

TEST(DeviceKTransport, SourceCoefficientsAreBitwise) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  KProblem p(12, 9);
  const Index n = p.cellCount();

  // Include cells where the kFloor BINDS, so the guarded denominator is
  // exercised rather than assumed.
  p.k[4] = 1e-14;
  p.k[17] = 0.0;
  p.k[n - 1] = -2.0;

  cfd::gpu::DeviceBuffer<Real> deviceK;
  deviceK.uploadFrom(p.k.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
  deviceEpsilon.uploadFrom(p.epsilon.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceProduction;
  deviceProduction.uploadFrom(p.production.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceSu;
  cfd::gpu::DeviceBuffer<Real> deviceSp;
  cfd::gpu::computeKSourceTermsDevice(kDensity, deviceK, deviceEpsilon, deviceProduction, kKFloor,
                                      deviceSu, deviceSp);

  const auto hostSources =
      cfd::turbulence::computeKSourceTerms(kDensity, p.k, p.epsilon, p.production, kKFloor);
  expectBitwise(hostSources.Su, download(deviceSu, n), "Su");
  expectBitwise(hostSources.Sp, download(deviceSp, n), "Sp");

  Index floored = 0;
  for (Index i = 0; i < n; ++i) {
    if (p.k[i] < kKFloor) ++floored;
  }
  EXPECT_GT(floored, 0u) << "no cell trips the k floor, so the guarded denominator is untested";

  // Su is P_k VERBATIM -- if the kernel recomputed it, the wall treatment's
  // replacement would be silently discarded.
  const std::vector<Real> deviceSuValues = download(deviceSu, n);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(deviceSuValues[static_cast<std::size_t>(i)], p.production[i])
        << "Su at cell " << i << " is not the production field it was given";
  }
}

// =============================================================================
// 4D -- matrix and RHS.
// =============================================================================

TEST(DeviceKTransport, MatrixStructureCoefficientsAndRhsMatchTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const KProblem p(12, 9);
  const Index n = p.cellCount();

  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries)) << diffusionPlan.unsupportedReason();
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries)) << convectionPlan.unsupportedReason();

  cfd::gpu::DeviceScalarTransportSystem system;
  cfd::gpu::DeviceBuffer<Real> deviceGammaK;
  cfd::gpu::DeviceBuffer<Real> deviceSu;
  cfd::gpu::DeviceBuffer<Real> deviceSp;
  assembleKOnDevice(p, diffusionPlan, convectionPlan, system, deviceGammaK, deviceSu, deviceSp);

  const ScalarField gammaK = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaK);
  const auto sources =
      cfd::turbulence::computeKSourceTerms(kDensity, p.k, p.epsilon, p.production, kKFloor);
  const HostSystem host = hostKSystem(p, gammaK, sources.Su, sources.Sp);

  const DeviceSnapshot device = snapshot(system, n);
  const Comparison c = compareSystems(host, device, n);

  EXPECT_TRUE(c.structureOk) << "sparsity differs: " << c.firstDifference;
  ASSERT_GT(c.compared, 0u) << "nothing was compared";
  // P14's frozen bound for this assembly. Not re-derived here, not loosened.
  EXPECT_LE(c.maxValueUlps, 2) << "matrix coefficients: " << c.firstDifference;
  EXPECT_LE(c.maxRhsUlps, 2) << "rhs";
  std::printf("  k matrix: %lld/%lld bitwise, max value %lld ulp, max rhs %lld ulp\n",
              static_cast<long long>(c.bitwise), static_cast<long long>(c.compared), c.maxValueUlps,
              c.maxRhsUlps);
}

// =============================================================================
// 4E -- the solved field, and the floor.
// =============================================================================

TEST(DeviceKTransport, SolvedKMatchesTheProductionSolve) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const KProblem p(12, 9);
  const Index n = p.cellCount();

  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries)) << diffusionPlan.unsupportedReason();
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries)) << convectionPlan.unsupportedReason();

  cfd::gpu::DeviceScalarTransportSystem system;
  cfd::gpu::DeviceBuffer<Real> deviceGammaK;
  cfd::gpu::DeviceBuffer<Real> deviceSu;
  cfd::gpu::DeviceBuffer<Real> deviceSp;
  assembleKOnDevice(p, diffusionPlan, convectionPlan, system, deviceGammaK, deviceSu, deviceSp);
  const DeviceSnapshot device = snapshot(system, n);

  // Rebuild the device system on the host solver, so the comparison isolates
  // the ASSEMBLY rather than mixing in a second solver implementation.
  SparseMatrixBuilder deviceBuilder(n, n);
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = device.rowOffsets[static_cast<std::size_t>(row)];
         d < device.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      deviceBuilder.add(row, device.columnIndices[d], device.values[d]);
    }
  }
  Vector deviceRhs(n, 0.0);
  for (Index i = 0; i < n; ++i) deviceRhs[i] = device.rhs[static_cast<std::size_t>(i)];

  const cfd::algebra::LinearSolverSettings settings;
  const cfd::algebra::BiCGSTAB solver(settings);
  Vector guess(n, 0.0);
  for (Index i = 0; i < n; ++i) guess[i] = p.k[i];
  const auto deviceSolve =
      solver.solve(cfd::algebra::LinearSystem(deviceBuilder.build(), deviceRhs), guess);
  ASSERT_TRUE(deviceSolve.converged());

  ScalarField deviceK(n, 0.0);
  for (Index i = 0; i < n; ++i) deviceK[i] = deviceSolve.solution[i];
  // The host order ends with the floor, so the device order must too.
  for (Index i = 0; i < n; ++i) deviceK[i] = std::max(deviceK[i], kKFloor);

  // THE PRODUCTION CPU REFERENCE -- solveRelaxedScalarTransport itself, with
  // the k equation's operands and no imposed cells.
  const ScalarField gammaK = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaK);
  const auto sources =
      cfd::turbulence::computeKSourceTerms(kDensity, p.k, p.epsilon, p.production, kKFloor);
  const ScalarField hostK = cfd::turbulence::solveRelaxedScalarTransport(
      p.mesh, p.k, p.massFlux, gammaK, p.boundaries, sources.Su, sources.Sp, kRelaxation, kKFloor,
      settings, {});

  Real numerator = 0.0;
  Real denominator = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real diff = deviceK[i] - hostK[i];
    numerator += diff * diff;
    denominator += hostK[i] * hostK[i];
  }
  const Real relativeL2 = std::sqrt(numerator) / std::sqrt(denominator);
  // P14's measured, frozen bound. Not chosen after seeing this result.
  EXPECT_LE(relativeL2, 2.1e-12) << "solved k differs by relative L2 " << relativeL2;
  std::printf("  solved k relative L2 = %.3e\n", relativeL2);
}

TEST(DeviceKTransport, TheFloorIsAppliedAndBinds) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 6;
  std::vector<Real> values = {-1.0, 0.0, 1e-30, kKFloor, 0.5, -1e-20};
  cfd::gpu::DeviceBuffer<Real> field;
  field.uploadFrom(values.data(), n);
  cfd::gpu::applyScalarFloorDevice(kKFloor, field);
  const std::vector<Real> after = download(field, n);

  Index bound = 0;
  for (Index i = 0; i < n; ++i) {
    const Real expected = std::max(values[static_cast<std::size_t>(i)], kKFloor);
    EXPECT_EQ(after[static_cast<std::size_t>(i)], expected) << "cell " << i;
    if (values[static_cast<std::size_t>(i)] < kKFloor) ++bound;
  }
  EXPECT_GT(bound, 0u) << "the floor never bound, so this test would pass without it";
}

// =============================================================================
// 4F -- wall semantics: k has NO imposed rows.
// =============================================================================

TEST(DeviceKTransport, KHasNoImposedRowsAndTheWallEntersThroughProduction) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  KProblem p(12, 9);
  const Index n = p.cellCount();
  const Index wallCell = 3;

  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  cfd::gpu::DeviceScalarTransportSystem baseline;
  cfd::gpu::DeviceBuffer<Real> g1, su1, sp1;
  assembleKOnDevice(p, diffusionPlan, convectionPlan, baseline, g1, su1, sp1);
  const DeviceSnapshot before = snapshot(baseline, n);

  // A wall cell's k row must keep its assembled coefficients: it is NOT a unit
  // diagonal. If this ever becomes {1, 0, 0, ...} someone has applied epsilon's
  // imposed-row treatment to k, which the model explicitly rejects as
  // over-constraining the wall cell.
  Index offDiagonalsInWallRow = 0;
  for (std::size_t d = before.rowOffsets[static_cast<std::size_t>(wallCell)];
       d < before.rowOffsets[static_cast<std::size_t>(wallCell) + 1]; ++d) {
    if (before.columnIndices[d] == wallCell) {
      EXPECT_NE(before.values[d], 1.0) << "the k row of a wall cell has a unit diagonal";
    } else if (before.values[d] != 0.0) {
      ++offDiagonalsInWallRow;
    }
  }
  EXPECT_GT(offDiagonalsInWallRow, 0u)
      << "the k row of cell " << wallCell << " has no live neighbour coupling, which is what an "
      << "imposed row looks like";

  // The wall treatment reaches k through ONE channel: the production field.
  // Changing it there must change the k system.
  p.production[wallCell] *= 3.5;
  cfd::gpu::DeviceScalarTransportSystem modified;
  cfd::gpu::DeviceBuffer<Real> g2, su2, sp2;
  assembleKOnDevice(p, diffusionPlan, convectionPlan, modified, g2, su2, sp2);
  const DeviceSnapshot after = snapshot(modified, n);

  EXPECT_NE(after.rhs[static_cast<std::size_t>(wallCell)],
            before.rhs[static_cast<std::size_t>(wallCell)])
      << "replacing the wall cell's production did not change the k RHS, so the wall treatment "
         "cannot reach k at all";
  EXPECT_EQ(after.values, before.values)
      << "the production replacement changed the MATRIX; it is an explicit source and must only "
         "reach the RHS";
}

// =============================================================================
// Non-vacuity -- the gate must detect a deliberate perturbation.
// =============================================================================

TEST(DeviceKTransport, ThePerturbedSystemIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const KProblem p(12, 9);
  const Index n = p.cellCount();

  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  cfd::gpu::DeviceScalarTransportSystem system;
  cfd::gpu::DeviceBuffer<Real> gammaK, su, sp;
  assembleKOnDevice(p, diffusionPlan, convectionPlan, system, gammaK, su, sp);
  DeviceSnapshot device = snapshot(system, n);

  const ScalarField hostGammaK = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaK);
  const auto sources =
      cfd::turbulence::computeKSourceTerms(kDensity, p.k, p.epsilon, p.production, kKFloor);
  const HostSystem host = hostKSystem(p, hostGammaK, sources.Su, sources.Sp);

  ASSERT_LE(compareSystems(host, device, n).maxValueUlps, 2) << "the control's baseline disagrees";

  // One coefficient, one ulp. If the gate cannot see this it cannot see a real
  // discretization error either.
  DeviceSnapshot perturbed = device;
  perturbed.values[5] = std::nextafter(perturbed.values[5], 1e300);
  EXPECT_GT(compareSystems(host, perturbed, n).maxValueUlps, 0)
      << "a one-ulp coefficient perturbation was not detected";

  // One RHS entry.
  DeviceSnapshot perturbedRhs = device;
  perturbedRhs.rhs[7] += 1e-9;
  EXPECT_GT(compareSystems(host, perturbedRhs, n).maxRhsUlps, 0)
      << "an RHS perturbation was not detected";

  // A stale off-diagonal in a row the host would have dropped -- the defect
  // the bidirectional structure check exists for.
  DeviceSnapshot stale = device;
  for (std::size_t d = 0; d < stale.values.size(); ++d) {
    if (stale.values[d] == 0.0) {
      stale.values[d] = 1e-3;
      break;
    }
  }
  const Comparison staleComparison = compareSystems(host, stale, n);
  EXPECT_TRUE(!staleComparison.structureOk || staleComparison.maxValueUlps > 0)
      << "a non-zero device-only entry was not detected";
}

TEST(DeviceKTransport, TheSameInputsProduceTheSameBytesTwice) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const KProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  DeviceSnapshot first;
  for (int pass = 0; pass < 2; ++pass) {
    cfd::gpu::DeviceScalarTransportSystem system;
    cfd::gpu::DeviceBuffer<Real> gammaK, su, sp;
    assembleKOnDevice(p, diffusionPlan, convectionPlan, system, gammaK, su, sp);
    const DeviceSnapshot current = snapshot(system, n);
    if (pass == 0) {
      first = current;
    } else {
      EXPECT_EQ(first.rowOffsets, current.rowOffsets);
      EXPECT_EQ(first.columnIndices, current.columnIndices);
      EXPECT_EQ(first.values, current.values);
      EXPECT_EQ(first.rhs, current.rhs);
    }
  }
}
