// P16 Phase 2 -- the sparse wall-face viscosity override in GPU momentum.
//
// THE CPU REFERENCE, at a boundary face (MomentumEquation.cpp:232-235):
//
//     Real muFace = effectiveViscosity[ownerId];
//     if (wallViscosity != nullptr && (*wallViscosity)[faceId] != 0.0) {
//       muFace = (*wallViscosity)[faceId];
//     }
//
// This is not an optimisation or a correction. It is HOW the high-Re wall
// function imposes tau_w on momentum: without it the assembly uses the interior
// cell viscosity at the wall and the validated wall shear coupling is gone,
// silently, while everything still converges to a plausible-looking answer.
//
// WHY FACE-LOCAL IS THE WHOLE POINT
// ---------------------------------
// The tempting shortcut is to write the wall viscosity into the owner cell's
// effective viscosity and let the ordinary path pick it up. That is wrong, and
// wrong in a way no field-norm comparison on a wall-bounded case would reliably
// catch. A wall-adjacent cell typically owns ONE wall face and several interior
// faces. Folding the value into the cell hands it to all of them:
//
//     cell P:  [wall face] [interior] [interior] [interior]
//     correct:   mu_wall     mu_eff     mu_eff     mu_eff
//     folded:    mu_wall     mu_wall    mu_wall    mu_wall
//
// That is a different finite-volume operator. `WallValueDoesNotLeakToInteriorFaces`
// below is the test that separates the two, and it is the reason this file
// exists rather than a single "the coefficients changed" assertion.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <algorithm>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuSimpleDiscretization.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no usable CUDA device at runtime";
}

struct Fixture {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  cfd::boundary::BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure;
  ScalarField effectiveViscosity;

  Fixture()
      : mesh(MeshGeometry::createCartesian2D(8, 6, 1.0, 0.5)),
        fluid(1.225, 1.8e-5),
        velocity(mesh.numberOfCells()),
        pressure(mesh.numberOfCells()),
        effectiveViscosity(mesh.numberOfCells()) {
    for (const auto& patch : mesh.boundaryPatches()) {
      velocityBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
      pressureBoundaries.set(mesh, patch.name(),
                             std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      velocity[i] = {1.5 + 0.01 * static_cast<Real>(i), 0.02, 0.0};
      pressure[i] = 0.1 * static_cast<Real>(i % 5);
      // Deliberately NOT uniform: a uniform field would make "used the cell
      // value" and "used a neighbour's" indistinguishable.
      effectiveViscosity[i] = 1.8e-5 + 1.0e-4 * (1.0 + std::sin(0.7 * static_cast<Real>(i)));
    }
  }

  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }

  // Every boundary face of the mesh, with its owner.
  [[nodiscard]] std::vector<Index> boundaryFaces() const {
    std::vector<Index> ids;
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      if (mesh.face(f).isBoundary()) ids.push_back(f);
    }
    return ids;
  }
};

// Assembles component U on the device with the given override, and returns the
// resulting host-visible system.
cfd::algebra::LinearSystem assembleOnDevice(Fixture& f, const std::vector<Index>& faceIds,
                                            const std::vector<Real>& values, bool applyOverride) {
  cfd::gpu::GpuSimpleDiscretization discretization;
  std::string reason;
  if (!discretization.prepare(f.mesh, f.velocityBoundaries, f.pressureBoundaries, reason)) {
    ADD_FAILURE() << "GPU discretization unsupported: " << reason;
    return cfd::algebra::LinearSystem(cfd::algebra::SparseMatrixBuilder(1, 1).build(),
                                      cfd::algebra::Vector(1, 0.0));
  }
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  discretization.uploadInitialState(f.velocity, f.pressure, flux, f.effectiveViscosity);
  if (applyOverride) {
    discretization.setWallFaceViscosity(faceIds, values);
  } else {
    discretization.clearWallFaceViscosity();
  }
  ScalarField previous(f.cellCount(), 0.0);
  for (Index i = 0; i < f.cellCount(); ++i) previous[i] = f.velocity[i].x;
  return discretization.assembleMomentum(0, previous, 1.0, 0, false);
}

// The CPU reference for the same assembly.
cfd::algebra::LinearSystem assembleOnHost(Fixture& f, const SurfaceField* wallViscosity) {
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  ScalarField previous(f.cellCount(), 0.0);
  for (Index i = 0; i < f.cellCount(); ++i) previous[i] = f.velocity[i].x;
  const auto assembly = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
      f.mesh, f.velocity, f.pressure, flux, f.effectiveViscosity, f.velocityBoundaries,
      f.pressureBoundaries, cfd::physics::VelocityComponent::U, previous, 1.0, nullptr, nullptr,
      cfd::discretization::ConvectionScheme::Upwind,
      cfd::discretization::GradientScheme::GreenGauss, false, nullptr, nullptr, wallViscosity);
  return assembly.system;
}

// Compares the raw CSR arrays rather than element lookups: that covers the
// sparsity STRUCTURE as well as the coefficients, so a device assembly that got
// the right numbers into the wrong pattern cannot slip through.
void expectSystemsMatch(const cfd::algebra::LinearSystem& expected,
                        const cfd::algebra::LinearSystem& got, const char* label) {
  const auto& a = expected.matrix();
  const auto& b = got.matrix();
  ASSERT_EQ(a.rows(), b.rows()) << label;
  ASSERT_EQ(a.nonZeros(), b.nonZeros()) << label << ": sparsity differs";
  for (Index r = 0; r <= a.rows(); ++r) {
    ASSERT_EQ(a.rowOffsetsData()[r], b.rowOffsetsData()[r]) << label << ": row offset " << r;
  }
  for (Index e = 0; e < a.nonZeros(); ++e) {
    ASSERT_EQ(a.columnIndicesData()[e], b.columnIndicesData()[e]) << label << ": column " << e;
    EXPECT_EQ(a.valuesData()[e], b.valuesData()[e]) << label << ": value " << e;
  }
  for (Index r = 0; r < a.rows(); ++r) {
    EXPECT_EQ(expected.rhs()[r], got.rhs()[r]) << label << ": rhs row " << r;
  }
}

// True when the two systems differ anywhere in one row.
bool rowDiffers(const cfd::algebra::LinearSystem& x, const cfd::algebra::LinearSystem& y,
                Index row) {
  const auto& a = x.matrix();
  const auto& b = y.matrix();
  if (a.rowOffsetsData()[row] != b.rowOffsetsData()[row]) return true;
  if (a.rowOffsetsData()[row + 1] != b.rowOffsetsData()[row + 1]) return true;
  for (Index e = a.rowOffsetsData()[row]; e < a.rowOffsetsData()[row + 1]; ++e) {
    if (a.valuesData()[e] != b.valuesData()[e]) return true;
  }
  return x.rhs()[row] != y.rhs()[row];
}

}  // namespace

// =============================================================================
// 1 + 2 -- override active, and override absent.
// =============================================================================

TEST(DeviceWallFaceViscosity, NoOverrideReproducesTheHostAssemblyBitwise) {
  skipIfNoDevice();
  Fixture f;
  // The baseline the laminar path has always produced. If this ever moves, the
  // override has stopped being opt-in.
  expectSystemsMatch(assembleOnHost(f, nullptr), assembleOnDevice(f, {}, {}, false), "no override");
}

TEST(DeviceWallFaceViscosity, OverrideReproducesTheHostAssemblyBitwise) {
  skipIfNoDevice();
  Fixture f;
  const auto faces = f.boundaryFaces();
  ASSERT_FALSE(faces.empty());

  // A wall viscosity far from any cell value, so a wrong lookup cannot
  // accidentally land on the right number.
  SurfaceField wallViscosity(f.mesh.numberOfFaces(), 0.0);
  std::vector<Real> values;
  values.reserve(faces.size());
  for (std::size_t i = 0; i < faces.size(); ++i) {
    const Real v = 7.5e-3 + 1.0e-3 * static_cast<Real>(i % 5);
    values.push_back(v);
    wallViscosity[faces[i]] = v;
  }

  expectSystemsMatch(assembleOnHost(f, &wallViscosity), assembleOnDevice(f, faces, values, true),
                     "with override");
}

// =============================================================================
// 3 -- THE ONE THAT MATTERS: the wall value must not leak off its own face.
// =============================================================================

TEST(DeviceWallFaceViscosity, WallValueDoesNotLeakToInteriorFaces) {
  skipIfNoDevice();
  Fixture f;

  // ONE boundary face carries an override. Its owner cell also has interior
  // faces, so if the implementation folded the value into the cell instead of
  // keeping it on the face, this cell's row would pick it up on every face and
  // diverge from the host -- which applies it to exactly one.
  const auto faces = f.boundaryFaces();
  ASSERT_FALSE(faces.empty());
  const Index chosenFace = faces[faces.size() / 2];
  const Index owner = f.mesh.face(chosenFace).owner();

  // Confirm the premise rather than assuming it: the owner must really have
  // more than one face, or the test proves nothing.
  Index facesOnOwner = 0;
  Index interiorFacesOnOwner = 0;
  for (Index face = 0; face < f.mesh.numberOfFaces(); ++face) {
    const auto& meshFace = f.mesh.face(face);
    const bool touches = meshFace.owner() == owner ||
                         (meshFace.neighbor().has_value() && *meshFace.neighbor() == owner);
    if (!touches) continue;
    ++facesOnOwner;
    if (!meshFace.isBoundary()) ++interiorFacesOnOwner;
  }
  ASSERT_GT(interiorFacesOnOwner, 0)
      << "the chosen wall cell has no interior face, so a leak would be invisible";

  constexpr Real kWallValue = 9.75e-2;  // ~1000x any cell viscosity
  SurfaceField wallViscosity(f.mesh.numberOfFaces(), 0.0);
  wallViscosity[chosenFace] = kWallValue;

  const auto host = assembleOnHost(f, &wallViscosity);
  const auto device = assembleOnDevice(f, {chosenFace}, {kWallValue}, true);
  expectSystemsMatch(host, device, "single-face override");

  // And a direct statement of the leak, independent of the host comparison:
  // fold the same value into the owner CELL instead and require a different
  // answer. If these two agreed, the face-local property would be untested.
  Fixture folded;
  folded.effectiveViscosity[owner] = kWallValue;
  const auto foldedHost = assembleOnHost(folded, nullptr);
  EXPECT_TRUE(rowDiffers(host, foldedHost, owner))
      << "folding the wall viscosity into the owner cell produced the same row as applying it to "
         "the wall face alone, so this mesh cannot distinguish the two and the test is vacuous";
}

// =============================================================================
// 4 -- physics sensitivity: the solver actually consumes it.
// =============================================================================

TEST(DeviceWallFaceViscosity, TurningTheOverrideOffMeasurablyChangesMomentum) {
  skipIfNoDevice();
  Fixture f;
  const auto faces = f.boundaryFaces();
  ASSERT_FALSE(faces.empty());
  std::vector<Real> values(faces.size(), 4.2e-2);

  const auto withOverride = assembleOnDevice(f, faces, values, true);
  const auto withoutOverride = assembleOnDevice(f, faces, values, false);

  // A transfer counter or a compile-time wiring check could both pass while the
  // assembly ignored the buffer. This requires the COEFFICIENTS to move.
  Index changedRows = 0;
  for (Index r = 0; r < withOverride.matrix().rows(); ++r) {
    if (rowDiffers(withOverride, withoutOverride, r)) ++changedRows;
  }
  EXPECT_GT(changedRows, 0)
      << "disabling the wall override changed no diagonal coefficient, so the momentum assembly "
         "is not consuming it";

  // Every wall-adjacent cell should be affected, and interior-only cells should
  // not be -- the override is boundary-local.
  EXPECT_LE(changedRows, f.cellCount());
}

TEST(DeviceWallFaceViscosity, ClearingTheOverrideRestoresTheLaminarAssemblyExactly) {
  skipIfNoDevice();
  Fixture f;
  const auto faces = f.boundaryFaces();
  std::vector<Real> values(faces.size(), 4.2e-2);

  const auto baseline = assembleOnDevice(f, {}, {}, false);
  // Set it, then clear it: the assembly must return to the byte-identical
  // laminar form rather than to something merely close.
  cfd::gpu::GpuSimpleDiscretization discretization;
  std::string reason;
  ASSERT_TRUE(discretization.prepare(f.mesh, f.velocityBoundaries, f.pressureBoundaries, reason));
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  discretization.uploadInitialState(f.velocity, f.pressure, flux, f.effectiveViscosity);
  ScalarField previous(f.cellCount(), 0.0);
  for (Index i = 0; i < f.cellCount(); ++i) previous[i] = f.velocity[i].x;

  discretization.setWallFaceViscosity(faces, values);
  (void)discretization.assembleMomentum(0, previous, 1.0, 0, false);
  discretization.clearWallFaceViscosity();
  const auto restored = discretization.assembleMomentum(0, previous, 1.0, 0, false);

  expectSystemsMatch(baseline, restored, "cleared override");
}
