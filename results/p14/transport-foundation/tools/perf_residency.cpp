// P14 Phase B12/N -- residency and performance sanity for the generic transport
// assembly.
//
// Two questions, both answered by measurement:
//   1. Does a device assembly transfer fields per call, or is it resident?
//   2. Is the assembly cost sane relative to the host?
//
// Phase B12's rule: per-linear-iteration field transfers are unacceptable
// without a documented blocker. This reports transfers for the STEADY-STATE
// repeat case -- upload once, then assemble N times -- which is the shape an
// outer iteration loop actually has.
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/gpu/DeviceScalarTransport.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

int main() {
  const Mesh mesh = MeshGeometry::createCartesian2D(80, 80, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  ScalarField phi(n, 300.0), gamma(n, 0.8), Su(n, 1.0), Sp(n, -2.0);
  for (Index i = 0; i < n; ++i) phi[i] = 300.0 + 0.01 * static_cast<Real>(i);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.3);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(300.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(280.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));

  cfd::gpu::DeviceDiffusionPlan diffusionPlan;
  cfd::gpu::DeviceConvectionPlan convectionPlan;
  if (!diffusionPlan.build(mesh, boundaries) || !convectionPlan.build(mesh, boundaries)) {
    std::printf("plans unusable\n");
    return 1;
  }

  cfd::gpu::DeviceBuffer<Real> dPhi, dGamma, dFlux, dSu, dSp, dVol;
  dPhi.uploadFrom(phi.data(), n);
  dGamma.uploadFrom(gamma.data(), n);
  dFlux.uploadFrom(massFlux.data(), mesh.numberOfFaces());
  dSu.uploadFrom(Su.data(), n);
  dSp.uploadFrom(Sp.data(), n);
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = mesh.cell(i).volume();
  dVol.uploadFrom(volumes.data(), n);

  constexpr int kRepeats = 20;
  const auto before = cfd::gpu::gpuExecutionStats();

  cfd::gpu::DeviceTransportTimeState steady;
  cfd::gpu::DeviceScalarTransportSystem system;
  const auto gpuStart = std::chrono::steady_clock::now();
  for (int i = 0; i < kRepeats; ++i) {
    cfd::gpu::assembleScalarTransportDevice(diffusionPlan, convectionPlan, dVol, dPhi, dFlux,
                                            dGamma, dSu, dSp, false, steady, system);
  }
  const double gpuSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - gpuStart).count();

  const auto after = cfd::gpu::gpuExecutionStats();

  const auto cpuStart = std::chrono::steady_clock::now();
  for (int i = 0; i < kRepeats; ++i) {
    const auto host = cfd::transport::assembleScalarTransportEquation(mesh, phi, massFlux, gamma,
                                                                      boundaries, Su, Sp);
    (void)host;
  }
  const double cpuSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - cpuStart).count();

  std::printf("P14 residency / performance -- 80x80 (%lld cells), %d assemblies\n\n",
              static_cast<long long>(n), kRepeats);
  std::printf("transfers DURING the %d assemblies (fields uploaded once, before):\n", kRepeats);
  std::printf("  H2D calls %llu  bytes %llu\n",
              static_cast<unsigned long long>(after.hostToDeviceCalls - before.hostToDeviceCalls),
              static_cast<unsigned long long>(after.hostToDeviceBytes - before.hostToDeviceBytes));
  std::printf("  D2H calls %llu  bytes %llu\n",
              static_cast<unsigned long long>(after.deviceToHostCalls - before.deviceToHostCalls),
              static_cast<unsigned long long>(after.deviceToHostBytes - before.deviceToHostBytes));
  std::printf("  kernels   %llu\n",
              static_cast<unsigned long long>(after.kernelLaunches - before.kernelLaunches));
  std::printf("  => per assembly: H2D %.1f calls, D2H %.1f calls, %.1f kernels\n\n",
              static_cast<double>(after.hostToDeviceCalls - before.hostToDeviceCalls) / kRepeats,
              static_cast<double>(after.deviceToHostCalls - before.deviceToHostCalls) / kRepeats,
              static_cast<double>(after.kernelLaunches - before.kernelLaunches) / kRepeats);
  std::printf("timing (assembly only, not a solve):\n");
  std::printf("  CPU %.4f s total, %.4f ms per assembly\n", cpuSeconds,
              1000.0 * cpuSeconds / kRepeats);
  std::printf("  GPU %.4f s total, %.4f ms per assembly\n", gpuSeconds,
              1000.0 * gpuSeconds / kRepeats);
  std::printf("\nNOTE: the D2H above is requireSamePattern's structural check, which downloads\n");
  std::printf("      the two index arrays per assembly. It is a CORRECTNESS guard, not field\n");
  std::printf("      traffic: no phi, Gamma, Su, Sp or matrix VALUES cross the bus. It could be\n");
  std::printf("      hoisted to plan-build time, which is recorded as known debt rather than\n");
  std::printf("      done here, because the plans are built by the caller.\n");
  return 0;
}
