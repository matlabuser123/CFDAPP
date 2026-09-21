// GPU-DISC-001E gate -- the shared CUDA boundary-condition layer vs the CPU.
//
// Isolated: this drives the device evaluators directly, with no equation, no
// operator and no assembly, so a failure localises to the BC layer itself.
//
// Compared per boundary face, bitwise:
//   * scalar boundaryValue at the straight-line owner-to-face distance;
//   * scalar boundaryValue at an ALTERNATIVE per-face distance (the gradient's
//     oblique normal distance);
//   * vector boundaryValue, all three components;
//   * the ghost/mirror value upwindBoundaryFaceValue builds, for inflow AND
//     outflow, including exactly-zero flux;
//   * the condition TYPE (what the pressure-correction path dispatches on);
//   * prescribesBoundaryValue.
//
// usage: boundary_condition_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/boundary/WallOmega.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceBoundaryConditions.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::gpu::DeviceBoundaryConditions;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;
int cases = 0;
std::size_t comparisons = 0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

Mesh q16() {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      vertices.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                                 y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, vertices);
}

Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

// ---------------------------------------------------------------------------
// Boundary-condition sets -- every supported type
// ---------------------------------------------------------------------------

using Maker = std::unique_ptr<cfd::boundary::BoundaryCondition> (*)(std::size_t);

std::unique_ptr<cfd::boundary::BoundaryCondition> makeFixedValue(std::size_t i) {
  return std::make_unique<cfd::boundary::FixedValue>(1.0 + (0.5 * static_cast<Real>(i)));
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeFixedGradient(std::size_t i) {
  return std::make_unique<cfd::boundary::FixedGradient>(0.25 - (0.1 * static_cast<Real>(i)));
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeFixedTemperature(std::size_t i) {
  return std::make_unique<cfd::boundary::FixedTemperature>(300.0 + (7.5 * static_cast<Real>(i)));
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeHeatFlux(std::size_t i) {
  return std::make_unique<cfd::boundary::HeatFlux>(1500.0 - (120.0 * static_cast<Real>(i)), 0.6);
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeAdiabatic(std::size_t) {
  return std::make_unique<cfd::boundary::Adiabatic>();
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeWallOmega(std::size_t) {
  return std::make_unique<cfd::boundary::WallOmega>(1.5e-5, 0.075);
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeWall(std::size_t) {
  return std::make_unique<cfd::boundary::Wall>();
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeMovingWall(std::size_t i) {
  return std::make_unique<cfd::boundary::MovingWall>(
      Vector3{1.0 + (0.1 * static_cast<Real>(i)), -0.2, 0.05});
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeInlet(std::size_t) {
  return std::make_unique<cfd::boundary::Inlet>(Vector3{0.8, 0.3, -0.1});
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeOutlet(std::size_t) {
  return std::make_unique<cfd::boundary::Outlet>();
}
std::unique_ptr<cfd::boundary::BoundaryCondition> makeSymmetry(std::size_t) {
  return std::make_unique<cfd::boundary::Symmetry>();
}

struct SetCase {
  const char* name;
  std::vector<Maker> makers;  // cycled over the patches
};

BoundaryConditionSet buildSet(const Mesh& mesh, const SetCase& setCase) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), setCase.makers[i % setCase.makers.size()](i));
    ++i;
  }
  return set;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

struct Coverage {
  int scalarConstant = 0, scalarShift = 0, scalarAffine = 0;
  int vectorConstant = 0, vectorIdentity = 0, vectorSymmetry = 0;
  int typesSeen[11] = {0};
  int ghostInflow = 0, ghostOutflow = 0, ghostZeroFlux = 0;
  int altDistanceFaces = 0;
};
Coverage coverage;

void runCase(const std::string& meshName, const Mesh& mesh, const SetCase& setCase, bool print) {
  const BoundaryConditionSet boundaries = buildSet(mesh, setCase);
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();

  // An alternative per-face distance, so the second encoding is genuinely
  // different from the primary rather than a copy.
  std::vector<Real> alternative(nf, 0.0);
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    const Real d = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    alternative[f] = d * 0.7;  // a distinct, positive distance
    ++coverage.altDistanceFaces;
  }

  DeviceBoundaryConditions conditions;
  if (!conditions.build(mesh, boundaries, alternative)) {
    ++cases;
    ++failures;
    std::printf("  FAIL %-16s %-24s unusable: %s\n", meshName.c_str(), setCase.name,
                conditions.unsupportedReason().c_str());
    return;
  }

  ScalarField phi(nc);
  std::vector<Real> ux(nc), uy(nc), uz(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    phi[c] = 290.0 + (20.0 * x.x) - (7.5 * x.y) + (3.25 * x.z);
    ux[c] = (2.0 * x.x) - (0.5 * x.y) + 0.25;
    uy[c] = (1.5 * x.y) + (0.75 * x.z) - 0.3;
    uz[c] = (0.6 * x.z) - (0.2 * x.x) + 0.1;
  }
  // Mixed flux, including exact +0.0 and -0.0, so both ghost branches and the
  // `>= 0.0` predicate are exercised.
  std::vector<Real> flux(nf, 0.0);
  for (Index f = 0; f < nf; ++f) {
    if (f % 5 == 0) flux[f] = 0.0;
    else if (f % 5 == 1) flux[f] = -0.0;
    else flux[f] = (f % 2 == 0) ? 0.6 : -0.9;
  }

  cfd::gpu::DeviceMesh deviceMesh;
  deviceMesh.upload(mesh);
  cfd::gpu::DeviceBuffer<Real> dPhi, dUx, dUy, dUz, dFlux;
  dPhi.uploadFrom(phi.data(), nc);
  dUx.uploadFrom(ux.data(), nc);
  dUy.uploadFrom(uy.data(), nc);
  dUz.uploadFrom(uz.data(), nc);
  dFlux.uploadFrom(flux.data(), nf);

  cfd::gpu::DeviceBoundaryEvaluation gpu;
  cfd::gpu::evaluateBoundaryConditionsDevice(conditions, deviceMesh, dPhi, dUx, dUy, dUz, dFlux,
                                             gpu);

  const auto pullR = [&](const cfd::gpu::DeviceBuffer<Real>& b) {
    std::vector<Real> h(static_cast<std::size_t>(b.size()));
    if (!h.empty()) b.downloadTo(h.data(), b.size());
    return h;
  };
  const auto pullI = [&](const cfd::gpu::DeviceBuffer<Index>& b) {
    std::vector<Index> h(static_cast<std::size_t>(b.size()));
    if (!h.empty()) b.downloadTo(h.data(), b.size());
    return h;
  };
  const auto gScalar = pullR(gpu.scalarValue), gAlt = pullR(gpu.alternativeScalarValue);
  const auto gVx = pullR(gpu.vectorX), gVy = pullR(gpu.vectorY), gVz = pullR(gpu.vectorZ);
  const auto gGhost = pullR(gpu.ghostValue);
  const auto gType = pullI(gpu.type), gPrescribes = pullI(gpu.prescribesValue);

  std::size_t dScalar = 0, dAlt = 0, dVector = 0, dGhost = 0, dType = 0, dPrescribes = 0;
  Real maxAbs = 0.0, scale = 0.0;

  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (!face.isBoundary()) continue;
    const Index owner = face.owner();
    const Real d = MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
    const Vector3 normal = MeshGeometry::unitNormal(face);
    const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);

    // type and prescribesValue
    if (gType[f] != static_cast<Index>(bc.type())) ++dType;
    coverage.typesSeen[static_cast<int>(bc.type())] = 1;
    const Index wantPrescribes =
        cfd::discretization::prescribesBoundaryValue(bc.type()) ? 1 : 0;
    if (gPrescribes[f] != wantPrescribes) ++dPrescribes;
    ++comparisons;

    if (const auto* s = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition*>(&bc)) {
      const Real want = s->boundaryValue(phi[owner], d);
      if (!sameBits(want, gScalar[f])) ++dScalar;
      maxAbs = std::max(maxAbs, std::abs(want - gScalar[f]));
      scale = std::max(scale, std::abs(want));

      const Real wantAlt = s->boundaryValue(phi[owner], alternative[f]);
      if (!sameBits(wantAlt, gAlt[f])) ++dAlt;
      maxAbs = std::max(maxAbs, std::abs(wantAlt - gAlt[f]));

      // ghost / mirror, against the production function itself
      const Real wantGhost = cfd::discretization::upwindBoundaryFaceValue(mesh, face, phi, flux[f],
                                                                          *s);
      if (!sameBits(wantGhost, gGhost[f])) ++dGhost;
      maxAbs = std::max(maxAbs, std::abs(wantGhost - gGhost[f]));
      if (flux[f] > 0.0) ++coverage.ghostOutflow;
      else if (flux[f] < 0.0) ++coverage.ghostInflow;
      else ++coverage.ghostZeroFlux;
      comparisons += 3;
    }

    if (const auto* v = dynamic_cast<const cfd::boundary::VectorBoundaryCondition*>(&bc)) {
      const Vector3 want = v->boundaryValue(Vector3{ux[owner], uy[owner], uz[owner]}, d, normal);
      if (!sameBits(want.x, gVx[f])) ++dVector;
      if (!sameBits(want.y, gVy[f])) ++dVector;
      if (!sameBits(want.z, gVz[f])) ++dVector;
      maxAbs = std::max(maxAbs, std::abs(want.x - gVx[f]));
      maxAbs = std::max(maxAbs, std::abs(want.y - gVy[f]));
      maxAbs = std::max(maxAbs, std::abs(want.z - gVz[f]));
      scale = std::max(scale, std::abs(want.x));
      comparisons += 3;
    }
  }

  coverage.scalarConstant += conditions.scalarConstantFaces() > 0 ? 1 : 0;
  coverage.scalarShift += conditions.scalarShiftFaces() > 0 ? 1 : 0;
  coverage.scalarAffine += conditions.scalarAffineFaces() > 0 ? 1 : 0;
  coverage.vectorConstant += conditions.vectorConstantFaces() > 0 ? 1 : 0;
  coverage.vectorIdentity += conditions.vectorIdentityFaces() > 0 ? 1 : 0;
  coverage.vectorSymmetry += conditions.vectorSymmetryFaces() > 0 ? 1 : 0;

  ++cases;
  const bool ok = dScalar == 0 && dAlt == 0 && dVector == 0 && dGhost == 0 && dType == 0 &&
                  dPrescribes == 0;
  if (!ok) ++failures;
  if (print || !ok) {
    std::printf("  %s %-16s %-24s scalar[d=%zu] alt[d=%zu] vector[d=%zu] ghost[d=%zu] "
                "type[d=%zu] prescribes[d=%zu] maxAbs=%-10.3g scale=%.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), setCase.name, dScalar, dAlt, dVector,
                dGhost, dType, dPrescribes, maxAbs, scale);
  }
}

void runMesh(const std::string& name, const Mesh& mesh, bool print) {
  const std::vector<SetCase> sets = {
      {"all FixedValue", {makeFixedValue}},
      {"all FixedGradient", {makeFixedGradient}},
      {"all FixedTemperature", {makeFixedTemperature}},
      {"all HeatFlux", {makeHeatFlux}},
      {"all Adiabatic", {makeAdiabatic}},
      {"all WallOmega", {makeWallOmega}},
      {"scalar mix (6 types)",
       {makeFixedValue, makeFixedGradient, makeFixedTemperature, makeHeatFlux, makeAdiabatic,
        makeWallOmega}},
      {"all Wall", {makeWall}},
      {"all MovingWall", {makeMovingWall}},
      {"all Inlet", {makeInlet}},
      {"all Outlet", {makeOutlet}},
      {"all Symmetry", {makeSymmetry}},
      {"vector mix (5 types)", {makeWall, makeMovingWall, makeInlet, makeOutlet, makeSymmetry}},
      {"Dirichlet+Neumann mix", {makeFixedValue, makeFixedGradient}},
      {"wall+outlet+symmetry", {makeWall, makeOutlet, makeSymmetry}},
  };
  for (const auto& setCase : sets) runCase(name, mesh, setCase, print);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001E: shared CUDA boundary-condition layer vs CPU (bitwise) ===\n");
  std::printf("isolated -- device evaluators driven directly, no equation or operator\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 4", warped3D(4), false);
  } else {
    runMesh("cartesian2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0), true);
    runMesh("cartesian2d 20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0), false);
    runMesh("distorted q16", q16(), false);
    runMesh("cartesian3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0), false);
    runMesh("warped 3d 4", warped3D(4), false);
  }

  std::printf("\n=== coverage ===\n");
  const char* typeNames[11] = {"FixedValue", "FixedGradient",    "Wall",     "MovingWall",
                               "Inlet",      "Outlet",           "Symmetry", "FixedTemperature",
                               "HeatFlux",   "Adiabatic",        "WallOmega"};
  int typesCovered = 0;
  for (int t = 0; t < 11; ++t) {
    std::printf("  type %-18s %s\n", typeNames[t], coverage.typesSeen[t] ? "covered" : "MISSING");
    typesCovered += coverage.typesSeen[t];
  }
  std::printf("  scalar forms const/shift/affine   %d / %d / %d (sets reaching each)\n",
              coverage.scalarConstant, coverage.scalarShift, coverage.scalarAffine);
  std::printf("  vector forms const/identity/symm  %d / %d / %d\n", coverage.vectorConstant,
              coverage.vectorIdentity, coverage.vectorSymmetry);
  std::printf("  ghost inflow/outflow/zero-flux    %d / %d / %d\n", coverage.ghostInflow,
              coverage.ghostOutflow, coverage.ghostZeroFlux);
  std::printf("  faces with an alternative distance %d\n", coverage.altDistanceFaces);
  std::printf("  total scalar comparisons          %zu\n", comparisons);

  if (typesCovered != 11) {
    ++failures;
    std::printf("  FAIL only %d of 11 boundary-condition types were exercised\n", typesCovered);
  }
  if (coverage.ghostInflow == 0 || coverage.ghostOutflow == 0 || coverage.ghostZeroFlux == 0) {
    ++failures;
    std::printf("  FAIL the ghost-value branches were not all exercised\n");
  }
  if (coverage.vectorSymmetry == 0 || coverage.vectorIdentity == 0) {
    ++failures;
    std::printf("  FAIL a vector evaluation form was never exercised\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "BOUNDARY CONDITION EQUIVALENCE: PASS (bitwise)"
                                    : "BOUNDARY CONDITION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
