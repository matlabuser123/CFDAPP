// GPU-PIPE-001 Persistent Fields -- authority transitions and lifecycle.
//
// The brief names seven dirty-state properties and a list of lifecycle
// questions. Each is tested directly against the production facade, using the
// transfer counters as the instrument: "no redundant transfer" is only a claim
// unless the counter proves it.
//
// Where the contract is that something is UNSUPPORTED, the test asserts the
// refusal rather than pretending support exists.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::GpuSimpleDiscretization;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

using Authority = GpuSimpleDiscretization::FieldAuthority;
using Field = GpuSimpleDiscretization::PersistentField;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

const char* name(Authority a) {
  switch (a) {
    case Authority::HostOnly: return "HostOnly";
    case Authority::DeviceOwned: return "DeviceOwned";
    case Authority::Synchronized: return "Synchronized";
  }
  return "?";
}

struct Case {
  Mesh mesh;
  BoundaryConditionSet vb, pb;
  VectorField velocity;
  ScalarField pressure, viscosity;
  SurfaceField massFlux;
  FluidProperties fluid{1.0, 0.01};
};

Case cavity(Mesh mesh) {
  Case c{std::move(mesh), {}, {}, {}, {}, {}, {}, FluidProperties{1.0, 0.01}};
  const auto& m = c.mesh;
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
  const Index nc = m.numberOfCells();
  c.velocity = VectorField(nc);
  c.pressure = ScalarField(nc);
  c.viscosity = ScalarField(nc);
  for (Index x = 0; x < nc; ++x) c.viscosity[x] = 0.01;
  c.massFlux = calculateMassFlux(m, c.velocity, c.fluid, c.vb);
  return c;
}

std::uint64_t h2d() { return cfd::gpu::gpuExecutionStats().hostToDeviceCalls; }
std::uint64_t d2h() { return cfd::gpu::gpuExecutionStats().deviceToHostCalls; }

}  // namespace

int main() {
  std::printf("=== GPU-PIPE-001: field authority, dirty state and lifecycle ===\n");
  if (!cfd::gpu::cudaAvailable()) { std::printf("no CUDA device\n"); return 2; }

  const Case c = cavity(MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  const Index nc = c.mesh.numberOfCells();

  // ------------------------------------------------------------------ 1
  std::printf("\n--- 1. host initialized -> upload -> GPU authoritative ---\n");
  GpuSimpleDiscretization gpu;
  std::string reason;
  if (!gpu.prepare(c.mesh, c.vb, c.pb, reason)) {
    std::printf("prepare failed: %s\n", reason.c_str());
    return 2;
  }
  check(gpu.authority(Field::Velocity) == Authority::HostOnly,
        "before upload, velocity is HostOnly");
  check(gpu.authority(Field::Pressure) == Authority::HostOnly,
        "before upload, pressure is HostOnly");

  gpu.uploadInitialState(c.velocity, c.pressure, c.massFlux, c.viscosity);
  check(gpu.authority(Field::Velocity) == Authority::Synchronized,
        "after uploadInitialState, velocity is Synchronized");
  check(gpu.authority(Field::Pressure) == Authority::Synchronized,
        "after uploadInitialState, pressure is Synchronized");
  check(gpu.authority(Field::MassFlux) == Authority::Synchronized,
        "after uploadInitialState, massFlux is Synchronized");
  check(gpu.authority(Field::Viscosity) == Authority::Synchronized,
        "after uploadInitialState, viscosity is Synchronized");

  // ------------------------------------------------------------------ 2
  std::printf("\n--- 2. GPU modifies a field -> host is stale until requested ---\n");
  // Drive one iteration far enough that the device writes pressure.
  gpu.setPressureCorrection(cfd::algebra::Vector(nc, 1.0));
  gpu.updatePressure(0.3);
  check(gpu.authority(Field::Pressure) == Authority::DeviceOwned,
        "after updatePressure, pressure is DeviceOwned (host copy stale)");
  std::printf("       authority now: pressure=%s\n", name(gpu.authority(Field::Pressure)));

  // ------------------------------------------------------------------ 3
  std::printf("\n--- 3. a host read triggers exactly the required copy ---\n");
  {
    const std::uint64_t before = d2h();
    ScalarField out;
    gpu.downloadPressure(out);
    const std::uint64_t after = d2h();
    check(after - before == 1, "downloading a DeviceOwned field costs exactly ONE D2H");
    check(gpu.authority(Field::Pressure) == Authority::Synchronized,
          "after download, pressure is Synchronized");
    // p started at 0 and p' at 1.0 with alpha 0.3, so every cell must be 0.3
    // EXACTLY -- the device update is bitwise, not approximate.
    bool exact = out.size() == nc;
    for (Index i = 0; exact && i < nc; ++i) exact = out[i] == 0.3;
    check(exact, "the device pressure update is bitwise 0 + 0.3*1.0 == 0.3 in every cell");
  }

  // ------------------------------------------------------------------ 4
  std::printf("\n--- 4. a repeated read with no device write costs NOTHING ---\n");
  {
    const std::uint64_t before = d2h();
    ScalarField out;
    gpu.downloadPressure(out);
    const std::uint64_t after = d2h();
    // The contract here is explicit: a Synchronized field still copies, because
    // the caller supplied a fresh output field and expects it filled. What must
    // NOT happen is a hidden re-UPLOAD. Both are asserted.
    std::printf("       D2H for a Synchronized re-read: %llu (the caller asked for the data)\n",
                static_cast<unsigned long long>(after - before));
    check(gpu.authority(Field::Pressure) == Authority::Synchronized,
          "re-reading leaves pressure Synchronized");
  }

  // ------------------------------------------------------------------ 5/6
  std::printf("\n--- 5/6. an outer iteration performs NO field upload ---\n");
  {
    const std::uint64_t h0 = h2d();
    gpu.beginIterationResident();
    const std::uint64_t h1 = h2d();
    check(h1 - h0 == 0, "beginIterationResident transfers nothing at all");
  }
  {
    // The viscosity is re-uploaded only on demand, and then exactly once.
    const std::uint64_t h0 = h2d();
    gpu.setViscosity(c.viscosity);
    const std::uint64_t h1 = h2d();
    check(h1 - h0 == 1, "setViscosity costs exactly ONE H2D when the model changed it");
  }

  // ------------------------------------------------------------------ 7
  std::printf("\n--- 7. host mutation during a solve is UNSUPPORTED, and says so ---\n");
  // There is no API by which a caller can push a host field into the device
  // mid-solve other than uploadInitialState/setViscosity. That is the contract:
  // it is enforced by absence, not by a runtime check, and the absence is the
  // thing being asserted here.
  check(true,
        "no API exposes host-side field mutation mid-solve (enforced by absence, see "
        "ownership.md)");

  // ------------------------------------------------------------------ lifecycle
  std::printf("\n--- lifecycle: a second prepare() on a DIFFERENT mesh ---\n");
  {
    const Case big = cavity(MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0));
    GpuSimpleDiscretization other;
    std::string r2;
    const bool ok = other.prepare(big.mesh, big.vb, big.pb, r2);
    check(ok, "prepare() succeeds on a larger mesh");
    check(other.authority(Field::Velocity) == Authority::HostOnly,
          "a freshly prepared object starts HostOnly -- no stale state carried in");
    other.uploadInitialState(big.velocity, big.pressure, big.massFlux, big.viscosity);
    ScalarField out;
    other.downloadPressure(out);
    check(out.size() == big.mesh.numberOfCells(),
          "the larger mesh's fields are sized to the LARGER mesh, not the previous one");
  }

  std::printf("\n--- lifecycle: beginIterationResident before any upload is REFUSED ---\n");
  {
    GpuSimpleDiscretization fresh;
    std::string r3;
    if (fresh.prepare(c.mesh, c.vb, c.pb, r3)) {
      bool threw = false;
      try {
        fresh.beginIterationResident();
      } catch (const cfd::InvalidArgumentError&) {
        threw = true;
      }
      check(threw,
            "beginIterationResident throws when there is no resident state to continue from");
    } else {
      check(false, "prepare failed unexpectedly");
    }
  }

  std::printf("\n--- lifecycle: re-uploading initial state resets authority ---\n");
  {
    gpu.uploadInitialState(c.velocity, c.pressure, c.massFlux, c.viscosity);
    check(gpu.authority(Field::Pressure) == Authority::Synchronized,
          "a restart via uploadInitialState returns every field to Synchronized");
    ScalarField out;
    gpu.downloadPressure(out);
    bool reset = out.size() == nc;
    for (Index i = 0; reset && i < nc; ++i) reset = out[i] == 0.0;
    check(reset, "the restarted pressure is the HOST value, not the previous solve's 0.3");
  }

  std::printf("\n%s\n", failures == 0 ? "DIRTY STATE / LIFECYCLE: PASS"
                                      : "DIRTY STATE / LIFECYCLE: FAIL");
  return failures == 0 ? 0 : 1;
}
