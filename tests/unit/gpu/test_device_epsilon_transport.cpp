// P16-GPU-TURB Stage 5 -- the epsilon transport equation, CPU against GPU.
//
// WHAT MAKES EPSILON DIFFERENT FROM k
// -----------------------------------
// k has no imposed cells. Epsilon does: the wall treatment FIXES epsilon in
// every wall-adjacent cell, because the epsilon transport equation is not valid
// in a cell that spans the log layer. So epsilon is the equation that exercises
// `fixImposedRowsDevice`, and it is the equation with a post-solve step k does
// not have.
//
// THE HOST ORDER, which the device must match operation for operation:
//
//     assemble -> under-relax -> fix imposed rows -> solve -> FLOOR
//                                                          -> RE-ASSERT imposed
//
// THE LAST TWO ARE IN THAT ORDER AND IT IS LOAD-BEARING.
// solveRelaxedScalarTransport says why: "An imposed value is a boundary
// condition, not a solver output, so the floor must not move it. Re-asserted
// rather than assumed: a wall epsilon below the floor would otherwise be
// silently replaced by the floor, and the cell would stop satisfying the wall
// function that put it there."
//
// That defect is INVISIBLE unless a test supplies an imposed value BELOW the
// floor. With every imposed value above the floor, floor-then-reassert and
// reassert-then-floor give identical answers, and a suite full of realistic
// values would pass with the two steps swapped. ReversedPostSolveOrderIsDetected
// below is built around exactly that case.
//
// THE FROZEN GATE
//   5B  Gamma_epsilon        BITWISE; sigma_epsilon = 1.3 in production, so the
//                            wrong closure (mu+mu_t)/sigma is discriminable
//                            without contriving anything
//   5C  Su / Sp              BITWISE against computeEpsilonSourceTerms
//   5D  imposed rows         exact: unit diagonal, explicit zeros, RHS = value,
//                            unrelated rows untouched
//   5E  matrix + RHS         structure exact BOTH directions; values within
//                            P14's frozen 2 ulp
//   5F  solved epsilon       within P14's frozen 2.1e-12 relative L2, and the
//                            post-solve order reproduced
//
// No threshold is invented here.
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
using cfd::turbulence::ImposedCell;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

// Production defaults.
constexpr Real kDensity = 1.225;
constexpr Real kMolecularViscosity = 1.8e-5;
constexpr Real kSigmaEpsilon = 1.3;
constexpr Real kC1Epsilon = 1.44;
constexpr Real kC2Epsilon = 1.92;
constexpr Real kCMu = 0.09;
constexpr Real kKFloor = 1e-10;
constexpr Real kEpsilonFloor = 1e-10;
constexpr Real kEpsilonRelaxation = 0.7;

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
    field[i] = base + (slope * static_cast<Real>(i)) + (0.29 * static_cast<Real>(i % modulus));
  }
  return field;
}

struct EpsilonProblem {
  Mesh mesh;
  ScalarField k;
  ScalarField epsilon;
  ScalarField turbulentViscosity;
  ScalarField production;
  SurfaceField massFlux;
  BoundaryConditionSet boundaries;
  std::vector<ImposedCell> imposed;

  EpsilonProblem(Index nx, Index ny)
      : mesh(MeshGeometry::createCartesian2D(nx, ny, 1.0, 1.0)),
        k(varying(mesh.numberOfCells(), 0.38, 0.010, 5)),
        epsilon(varying(mesh.numberOfCells(), 0.24, 0.006, 3)),
        turbulentViscosity(mesh.numberOfCells(), 0.0),
        production(varying(mesh.numberOfCells(), 1.6, 0.08, 7)),
        massFlux(mesh.numberOfFaces(), 0.0) {
    const Index n = mesh.numberOfCells();
    turbulentViscosity = cfd::turbulence::computeTurbulentViscosityField(
        mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      massFlux[f] = (f % 3 == 0) ? -0.31 : 0.44;
    }
    boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.30));
    boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.18));
    boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
    boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
    // Wall cells: first row, an interior cell, and the last row -- the same
    // three-slot shape Stage 0 established, so an off-by-one at either end of
    // the index range is caught.
    imposed = {ImposedCell{0, 812.4}, ImposedCell{(n / 2) + 3, 1934.7}, ImposedCell{n - 1, 655.1}};
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
      first = "cell " + std::to_string(i) + " (" +
              std::to_string(ulpDistance(device[static_cast<std::size_t>(i)], host[i])) + " ulp)";
    }
  }
  EXPECT_EQ(mismatches, 0u) << what << ": " << mismatches << " differ. First: " << first;
}

struct HostSystem {
  SparseMatrix matrix;
  Vector rhs;
};

HostSystem hostEpsilonSystem(const EpsilonProblem& p, const ScalarField& gamma,
                             const ScalarField& Su, const ScalarField& Sp,
                             const std::vector<ImposedCell>& imposed) {
  const Index n = p.cellCount();
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  cfd::transport::assembleScalarDiffusionContribution(p.mesh, gamma, p.epsilon, p.boundaries,
                                                      builder, rhs, {});
  cfd::transport::assembleScalarConvectionContribution(p.mesh, p.massFlux, p.epsilon, p.boundaries,
                                                       builder, rhs);
  cfd::transport::applyImplicitScalarSource(p.mesh, builder, rhs, Su, Sp);
  Vector old(n, 0.0);
  for (Index i = 0; i < n; ++i) old[i] = p.epsilon[i];
  cfd::pressure_velocity::applyImplicitUnderRelaxation(builder, rhs, old, kEpsilonRelaxation);
  for (const ImposedCell& entry : imposed) {
    builder.fixRow(entry.cell);
    rhs[entry.cell] = entry.value;
  }
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
  [[nodiscard]] bool agrees() const { return structureOk && maxValueUlps <= 2 && maxRhsUlps <= 2; }
};

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

void uploadImposed(const std::vector<ImposedCell>& imposed, Index n,
                   cfd::gpu::DeviceImposedCells& out) {
  std::vector<Index> cells;
  std::vector<Real> values;
  for (const ImposedCell& e : imposed) {
    cells.push_back(e.cell);
    values.push_back(e.value);
  }
  cfd::gpu::uploadImposedCellsDevice(cells, values, n, out);
}

// Assembles epsilon on device in the host's order.
void assembleEpsilonOnDevice(const EpsilonProblem& p, cfd::gpu::DeviceDiffusionPlan& diffusionPlan,
                             cfd::gpu::DeviceConvectionPlan& convectionPlan,
                             const std::vector<ImposedCell>& imposed,
                             cfd::gpu::DeviceScalarTransportSystem& system) {
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

  cfd::gpu::DeviceBuffer<Real> deviceGamma;
  cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaEpsilon,
                                              deviceGamma);
  cfd::gpu::DeviceBuffer<Real> deviceSu;
  cfd::gpu::DeviceBuffer<Real> deviceSp;
  cfd::gpu::computeEpsilonSourceTermsDevice(kDensity, kC1Epsilon, kC2Epsilon, deviceK,
                                            deviceEpsilon, deviceProduction, kKFloor, deviceSu,
                                            deviceSp);

  const cfd::gpu::DeviceTransportTimeState steady;
  cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, deviceVolumes,
                                          deviceEpsilon, deviceFlux, deviceGamma, deviceSu,
                                          deviceSp, /*nonOrthogonalEnabled=*/false, steady, system);
  cfd::gpu::applyImplicitUnderRelaxationDevice(deviceEpsilon, kEpsilonRelaxation, system);
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);
  cfd::gpu::fixImposedRowsDevice(deviceImposed, system);
}

}  // namespace

// =============================================================================
// 5B -- the effective diffusivity, at the production sigma_epsilon.
// =============================================================================

TEST(DeviceEpsilonTransport, EffectiveDiffusivityIsBitwiseAtProductionSigma) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  deviceMuT.uploadFrom(p.turbulentViscosity.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceGamma;
  cfd::gpu::computeEffectiveDiffusivityDevice(kMolecularViscosity, deviceMuT, kSigmaEpsilon,
                                              deviceGamma);

  const ScalarField hostGamma = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaEpsilon);
  expectBitwise(hostGamma, download(deviceGamma, n), "Gamma_epsilon");

  // sigma_epsilon = 1.3 in production, so unlike sigma_k = 1 the wrong closure
  // is discriminable without contriving a value.
  const std::vector<Real> device = download(deviceGamma, n);
  Index differsFromWrongClosure = 0;
  for (Index i = 0; i < n; ++i) {
    const Real wrong = (kMolecularViscosity + p.turbulentViscosity[i]) / kSigmaEpsilon;
    if (wrong != device[static_cast<std::size_t>(i)]) ++differsFromWrongClosure;
  }
  EXPECT_EQ(differsFromWrongClosure, n)
      << "the kernel agrees with (mu+mu_t)/sigma_epsilon at the PRODUCTION sigma";
}

// =============================================================================
// 5C -- the source coefficients.
// =============================================================================

TEST(DeviceEpsilonTransport, SourceCoefficientsAreBitwiseIncludingFlooredCells) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  // Cells where the k floor BINDS inside epsOverK.
  p.k[6] = 1e-15;
  p.k[21] = 0.0;
  p.k[n - 2] = -0.5;

  cfd::gpu::DeviceBuffer<Real> deviceK;
  deviceK.uploadFrom(p.k.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
  deviceEpsilon.uploadFrom(p.epsilon.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceProduction;
  deviceProduction.uploadFrom(p.production.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceSu;
  cfd::gpu::DeviceBuffer<Real> deviceSp;
  cfd::gpu::computeEpsilonSourceTermsDevice(kDensity, kC1Epsilon, kC2Epsilon, deviceK,
                                            deviceEpsilon, deviceProduction, kKFloor, deviceSu,
                                            deviceSp);

  const auto host = cfd::turbulence::computeEpsilonSourceTerms(
      kDensity, kC1Epsilon, kC2Epsilon, p.k, p.epsilon, p.production, kKFloor);
  expectBitwise(host.Su, download(deviceSu, n), "Su_epsilon");
  expectBitwise(host.Sp, download(deviceSp, n), "Sp_epsilon");

  Index floored = 0;
  for (Index i = 0; i < n; ++i) {
    if (p.k[i] < kKFloor) ++floored;
  }
  EXPECT_GT(floored, 0u) << "no cell trips the k floor inside epsOverK";

  // The wall-modified production must reach epsilon's Su. Changing production
  // in one cell must change Su there and nowhere else.
  const std::vector<Real> before = download(deviceSu, n);
  ScalarField wallModified = p.production;
  wallModified[9] *= 2.5;
  cfd::gpu::DeviceBuffer<Real> deviceProduction2;
  deviceProduction2.uploadFrom(wallModified.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceSu2;
  cfd::gpu::DeviceBuffer<Real> deviceSp2;
  cfd::gpu::computeEpsilonSourceTermsDevice(kDensity, kC1Epsilon, kC2Epsilon, deviceK,
                                            deviceEpsilon, deviceProduction2, kKFloor, deviceSu2,
                                            deviceSp2);
  const std::vector<Real> after = download(deviceSu2, n);
  for (Index i = 0; i < n; ++i) {
    if (i == 9) {
      EXPECT_NE(after[static_cast<std::size_t>(i)], before[static_cast<std::size_t>(i)])
          << "wall-modified production did not reach epsilon's Su";
    } else {
      EXPECT_EQ(after[static_cast<std::size_t>(i)], before[static_cast<std::size_t>(i)])
          << "cell " << i << " changed but its production did not";
    }
  }
  // Sp must NOT depend on production.
  EXPECT_EQ(download(deviceSp2, n), download(deviceSp, n))
      << "Sp changed with production, but the destruction term does not contain P_k";
}

// =============================================================================
// 5D / 5E -- imposed rows and the complete system.
// =============================================================================

TEST(DeviceEpsilonTransport, MatrixRhsAndImposedRowsMatchTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries)) << diffusionPlan.unsupportedReason();
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries)) << convectionPlan.unsupportedReason();

  cfd::gpu::DeviceScalarTransportSystem system;
  assembleEpsilonOnDevice(p, diffusionPlan, convectionPlan, p.imposed, system);
  const DeviceSnapshot device = snapshot(system, n);

  const ScalarField gamma = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaEpsilon);
  const auto sources = cfd::turbulence::computeEpsilonSourceTerms(
      kDensity, kC1Epsilon, kC2Epsilon, p.k, p.epsilon, p.production, kKFloor);
  const HostSystem host = hostEpsilonSystem(p, gamma, sources.Su, sources.Sp, p.imposed);

  const Comparison c = compareSystems(host, device, n);
  EXPECT_TRUE(c.structureOk) << c.firstDifference;
  ASSERT_GT(c.compared, 0u);
  EXPECT_LE(c.maxValueUlps, 2) << c.firstDifference;
  EXPECT_LE(c.maxRhsUlps, 2);
  std::printf("  epsilon matrix: %lld/%lld bitwise, max value %lld ulp, max rhs %lld ulp\n",
              static_cast<long long>(c.bitwise), static_cast<long long>(c.compared), c.maxValueUlps,
              c.maxRhsUlps);

  // 5D explicitly: the imposed rows' shape and values.
  for (const ImposedCell& entry : p.imposed) {
    bool sawDiagonal = false;
    for (std::size_t d = device.rowOffsets[static_cast<std::size_t>(entry.cell)];
         d < device.rowOffsets[static_cast<std::size_t>(entry.cell) + 1]; ++d) {
      if (device.columnIndices[d] == entry.cell) {
        sawDiagonal = true;
        EXPECT_EQ(device.values[d], 1.0) << "imposed row " << entry.cell << " diagonal";
      } else {
        EXPECT_EQ(device.values[d], 0.0) << "imposed row " << entry.cell << " kept an off-diagonal";
      }
    }
    EXPECT_TRUE(sawDiagonal);
    EXPECT_EQ(device.rhs[static_cast<std::size_t>(entry.cell)], entry.value)
        << "imposed row " << entry.cell << " RHS is not the wall epsilon value";
  }
}

TEST(DeviceEpsilonTransport, DroppingOrCorruptingAnImposedRowIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  cfd::gpu::DeviceScalarTransportSystem system;
  assembleEpsilonOnDevice(p, diffusionPlan, convectionPlan, p.imposed, system);
  const DeviceSnapshot device = snapshot(system, n);

  const ScalarField gamma = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaEpsilon);
  const auto sources = cfd::turbulence::computeEpsilonSourceTerms(
      kDensity, kC1Epsilon, kC2Epsilon, p.k, p.epsilon, p.production, kKFloor);

  ASSERT_TRUE(
      compareSystems(hostEpsilonSystem(p, gamma, sources.Su, sources.Sp, p.imposed), device, n)
          .agrees())
      << "the control's own baseline disagrees";

  for (std::size_t drop = 0; drop < p.imposed.size(); ++drop) {
    std::vector<ImposedCell> reduced;
    for (std::size_t i = 0; i < p.imposed.size(); ++i) {
      if (i != drop) reduced.push_back(p.imposed[i]);
    }
    EXPECT_FALSE(
        compareSystems(hostEpsilonSystem(p, gamma, sources.Su, sources.Sp, reduced), device, n)
            .agrees())
        << "dropping imposed cell " << p.imposed[drop].cell << " was not detected";
  }

  for (std::size_t corrupt = 0; corrupt < p.imposed.size(); ++corrupt) {
    std::vector<ImposedCell> altered = p.imposed;
    altered[corrupt].value += 1e-6;
    EXPECT_FALSE(
        compareSystems(hostEpsilonSystem(p, gamma, sources.Su, sources.Sp, altered), device, n)
            .agrees())
        << "corrupting imposed cell " << p.imposed[corrupt].cell << "'s value was not detected";
  }
}

// =============================================================================
// 5F -- the solved field, and the post-solve order.
// =============================================================================

TEST(DeviceEpsilonTransport, SolvedEpsilonMatchesTheProductionSolve) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  cfd::gpu::DeviceScalarTransportSystem system;
  assembleEpsilonOnDevice(p, diffusionPlan, convectionPlan, p.imposed, system);
  const DeviceSnapshot device = snapshot(system, n);

  SparseMatrixBuilder builder(n, n);
  for (Index row = 0; row < n; ++row) {
    for (std::size_t d = device.rowOffsets[static_cast<std::size_t>(row)];
         d < device.rowOffsets[static_cast<std::size_t>(row) + 1]; ++d) {
      builder.add(row, device.columnIndices[d], device.values[d]);
    }
  }
  Vector rhs(n, 0.0);
  for (Index i = 0; i < n; ++i) rhs[i] = device.rhs[static_cast<std::size_t>(i)];

  const cfd::algebra::LinearSolverSettings settings;
  const cfd::algebra::BiCGSTAB solver(settings);
  Vector guess(n, 0.0);
  for (Index i = 0; i < n; ++i) guess[i] = p.epsilon[i];
  const auto solved = solver.solve(cfd::algebra::LinearSystem(builder.build(), rhs), guess);
  ASSERT_TRUE(solved.converged());

  // The HOST ORDER: floor, then re-assert.
  cfd::gpu::DeviceBuffer<Real> deviceSolved;
  std::vector<Real> solvedValues(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) solvedValues[static_cast<std::size_t>(i)] = solved.solution[i];
  deviceSolved.uploadFrom(solvedValues.data(), n);
  cfd::gpu::applyScalarFloorDevice(kEpsilonFloor, deviceSolved);
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(p.imposed, n, deviceImposed);
  cfd::gpu::reassertImposedValuesDevice(deviceImposed, deviceSolved);
  const std::vector<Real> deviceEpsilon = download(deviceSolved, n);

  const ScalarField gamma = cfd::turbulence::computeEffectiveDiffusivity(
      kMolecularViscosity, p.turbulentViscosity, kSigmaEpsilon);
  const auto sources = cfd::turbulence::computeEpsilonSourceTerms(
      kDensity, kC1Epsilon, kC2Epsilon, p.k, p.epsilon, p.production, kKFloor);
  const ScalarField hostEpsilon = cfd::turbulence::solveRelaxedScalarTransport(
      p.mesh, p.epsilon, p.massFlux, gamma, p.boundaries, sources.Su, sources.Sp,
      kEpsilonRelaxation, kEpsilonFloor, settings, {}, p.imposed);

  Real numerator = 0.0;
  Real denominator = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real diff = deviceEpsilon[static_cast<std::size_t>(i)] - hostEpsilon[i];
    numerator += diff * diff;
    denominator += hostEpsilon[i] * hostEpsilon[i];
  }
  const Real relativeL2 = std::sqrt(numerator) / std::sqrt(denominator);
  EXPECT_LE(relativeL2, 2.1e-12) << "solved epsilon relative L2 " << relativeL2;
  std::printf("  solved epsilon relative L2 = %.3e\n", relativeL2);

  // The imposed cells must hold EXACTLY the wall value on both arms -- the
  // solver only approaches it (1.5e-10 in Stage 0's measurement).
  for (const ImposedCell& entry : p.imposed) {
    EXPECT_EQ(deviceEpsilon[static_cast<std::size_t>(entry.cell)], entry.value)
        << "device imposed cell " << entry.cell << " was not re-asserted";
    EXPECT_EQ(hostEpsilon[entry.cell], entry.value)
        << "host imposed cell " << entry.cell << " was not re-asserted";
  }
}

TEST(DeviceEpsilonTransport, ReversedPostSolveOrderIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // THE DISCRIMINATING CASE. A wall epsilon BELOW the floor is legal, and it is
  // the only configuration in which floor-then-reassert and reassert-then-floor
  // differ. With realistic wall values (hundreds to thousands) the two orders
  // are indistinguishable, and a suite built only from those would pass with
  // the steps swapped.
  const Index n = 6;
  const Real floorValue = 1e-4;
  const std::vector<ImposedCell> imposed = {ImposedCell{1, 1e-9},    // BELOW the floor
                                            ImposedCell{4, 500.0}};  // above it

  std::vector<Real> solved = {0.2, 1e-30, 0.4, 1e-20, 0.6, 0.8};
  cfd::gpu::DeviceImposedCells deviceImposed;
  uploadImposed(imposed, n, deviceImposed);

  // The HOST order.
  cfd::gpu::DeviceBuffer<Real> correct;
  correct.uploadFrom(solved.data(), n);
  cfd::gpu::applyScalarFloorDevice(floorValue, correct);
  cfd::gpu::reassertImposedValuesDevice(deviceImposed, correct);
  const std::vector<Real> correctOrder = download(correct, n);

  // The REVERSED order, built on purpose.
  cfd::gpu::DeviceBuffer<Real> reversed;
  reversed.uploadFrom(solved.data(), n);
  cfd::gpu::reassertImposedValuesDevice(deviceImposed, reversed);
  cfd::gpu::applyScalarFloorDevice(floorValue, reversed);
  const std::vector<Real> reversedOrder = download(reversed, n);

  EXPECT_NE(correctOrder, reversedOrder)
      << "the two post-solve orders agree, so this test cannot tell them apart and the ordering "
         "claim has no evidence";

  // And specifically: the below-floor imposed value survives in the correct
  // order and is destroyed in the reversed one.
  EXPECT_EQ(correctOrder[1], 1e-9) << "the floor overwrote an imposed wall value";
  EXPECT_EQ(reversedOrder[1], floorValue)
      << "the reversed order did not actually damage the imposed value, so it is not a control";

  // The imposed value ABOVE the floor is identical either way -- which is
  // exactly why it cannot serve as the discriminator.
  EXPECT_EQ(correctOrder[4], reversedOrder[4]);

  // Non-imposed cells are floored the same way in both.
  for (const Index cell : {Index{0}, Index{2}, Index{3}, Index{5}}) {
    EXPECT_EQ(correctOrder[cell], reversedOrder[cell]) << "cell " << cell;
  }
}

// =============================================================================
// 5G -- provenance, and determinism.
// =============================================================================

TEST(DeviceEpsilonTransport, ProductionTraceEntriesRemainNotAttempted) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  auto& trace = cfd::gpu::turbulenceBackendTrace();
  trace.reset();

  const EpsilonProblem p(12, 9);
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));
  cfd::gpu::DeviceScalarTransportSystem system;
  assembleEpsilonOnDevice(p, diffusionPlan, convectionPlan, p.imposed, system);

  // Assembling epsilon from a TEST HARNESS is not production execution.
  // KEpsilonModel::correct is still an unconditional host call and the resident
  // SIMPLE loop still refuses every model but laminar, so recording
  // EpsilonAssembly/EpsilonSolve here would attribute CUDA execution to a
  // shipped path that has not performed it. They flip in the integration stage.
  for (const auto pending :
       {cfd::gpu::TurbulenceOperation::EpsilonAssembly, cfd::gpu::TurbulenceOperation::EpsilonSolve,
        cfd::gpu::TurbulenceOperation::KAssembly, cfd::gpu::TurbulenceOperation::KSolve,
        cfd::gpu::TurbulenceOperation::WallTreatment}) {
    EXPECT_EQ(trace.backendFor(pending), cfd::gpu::ExecutionBackend::NotAttempted)
        << cfd::gpu::turbulenceOperationName(pending)
        << " was recorded, but no production path executed it";
  }
}

TEST(DeviceEpsilonTransport, TheSameInputsProduceTheSameBytesTwice) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const EpsilonProblem p(12, 9);
  const Index n = p.cellCount();
  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  ASSERT_TRUE(diffusionPlan.build(p.mesh, p.boundaries));
  ASSERT_TRUE(convectionPlan.build(p.mesh, p.boundaries));

  DeviceSnapshot first;
  for (int pass = 0; pass < 2; ++pass) {
    cfd::gpu::DeviceScalarTransportSystem system;
    assembleEpsilonOnDevice(p, diffusionPlan, convectionPlan, p.imposed, system);
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
