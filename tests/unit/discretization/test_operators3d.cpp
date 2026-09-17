// P12-MESH-005 -- the production discretisation operators on the 3D
// Cartesian hexahedral mesh: exactness for fields the schemes must reproduce
// to round-off (results/p12-mesh-005/acceptance_gate.md, items C1-C5).
// The spatial ORDER of the same operators on a genuinely 3D smooth field is
// the refinement study in tests/integration/mms/test_mms_3d.cpp.
//
// Boundary data: one singleton patch per boundary face with that face's exact
// value (cfd::test::perFaceBoundaryMesh / makeExactBoundaries -- the same
// device every 2D operator test uses).

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ManufacturedFields.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/CompressibleMomentum.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/io/JSONWriter.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/TransientMomentum.hpp"
#include "cfd/solver/RestartSnapshot.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/KOmegaModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/WallDistance.hpp"
#include "cfd/viz/DerivedFields.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

using Field = std::function<Real(const Vector3&)>;

// The gate's meshes M1-M6 (M5: translated, non-unit, non-cubic cells).
struct Box {
  const char* name;
  Index nx, ny, nz;
  Real lx, ly, lz;
  Vector3 origin;
  [[nodiscard]] Mesh build() const {
    return MeshGeometry::createCartesian3D(nx, ny, nz, lx, ly, lz, origin);
  }
};

const std::vector<Box>& boxes() {
  static const std::vector<Box> all = {
      {"M1 1x1x1", 1, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M2 2x1x1", 2, 1, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M3 2x2x1", 2, 2, 1, 1.0, 1.0, 1.0, Vector3{}},
      {"M4 2x2x2", 2, 2, 2, 1.0, 1.0, 1.0, Vector3{}},
      {"M5 3x4x5 translated cuboid", 3, 4, 5, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1}},
      {"M6 8x8x8", 8, 8, 8, 1.0, 1.0, 1.0, Vector3{}},
  };
  return all;
}

const Box& box(std::size_t k) { return boxes()[k]; }

ScalarField sample(const Mesh& mesh, const Field& f) {
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}

// Exact outward normal-derivative (Neumann) data on every boundary face of a
// per-face mesh: FixedGradient(grad(phi) . n_out).
BoundaryConditionSet exactNeumann(const Mesh& mesh, const Vector3& gradient) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Face& face = mesh.face(patch.faceIds().front());
    const Vector3 n = face.areaVector() * (1.0 / face.area());
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(dot(gradient, n)));
  }
  return bcs;
}

// Mass flux of the gate's divergence-free linear velocity u = (1 + 0.5y,
// 1 + 0.5z, 1 + 0.5x): rho u(x_f) . Sf, exact for a linear field over a
// planar rectangle (the midpoint rule integrates linear functions exactly).
Vector3 gateVelocity(const Vector3& p) {
  return Vector3{1.0 + (0.5 * p.y), 1.0 + (0.5 * p.z), 1.0 + (0.5 * p.x)};
}
SurfaceField gateMassFlux(const Mesh& mesh, Real density) {
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = density * dot(gateVelocity(face.centroid()), face.areaVector());
  }
  return flux;
}

int axisOf(const Vector3& v) { return v.x != 0.0 ? 0 : (v.y != 0.0 ? 1 : 2); }

}  // namespace

// --- C1: interpolation --------------------------------------------------------
TEST(Operators3DTest, InterpolationIsExactForConstantAndLinearFields) {
  for (const std::size_t k : {std::size_t{4}, std::size_t{5}}) {
    SCOPED_TRACE(box(k).name);
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(box(k).build());
    ASSERT_EQ(mesh.dimension(), 3);

    const Field constant = [](const Vector3&) { return 3.7; };
    const auto constantFaces = cfd::discretization::interpolate(
        mesh, sample(mesh, constant), cfd::test::makeExactBoundaries(mesh, constant));
    for (const auto& face : mesh.faces()) EXPECT_NEAR(constantFaces[face.id()], 3.7, 1e-12);

    const Field linear = [](const Vector3& p) { return 1.0 + p.x - (2.0 * p.y) + (3.0 * p.z); };
    const auto faces = cfd::discretization::interpolate(
        mesh, sample(mesh, linear), cfd::test::makeExactBoundaries(mesh, linear));
    std::array<Index, 3> interiorPerAxis = {0, 0, 0};
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) {
        EXPECT_EQ(faces[face.id()], linear(face.centroid())) << "boundary face " << face.id();
      } else {
        ++interiorPerAxis[static_cast<std::size_t>(axisOf(face.areaVector()))];
        EXPECT_NEAR(faces[face.id()], linear(face.centroid()), 1e-12) << "face " << face.id();
      }
    }
    for (const Index count : interiorPerAxis) EXPECT_GT(count, 0U);  // x-, y- and z-faces

    // A three-component linear vector field on the interior faces.
    const auto vectorFn = [](const Vector3& p) {
      return Vector3{1.0 + p.x, 2.0 - p.y + p.z, (3.0 * p.z) - p.x};
    };
    VectorField vf(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) vf[cell.id()] = vectorFn(cell.centroid());
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) continue;
      const Vector3 value = cfd::discretization::interpolateInternalFace(mesh, face, vf);
      const Vector3 exact = vectorFn(face.centroid());
      EXPECT_NEAR(value.x, exact.x, 1e-12);
      EXPECT_NEAR(value.y, exact.y, 1e-12);
      EXPECT_NEAR(value.z, exact.z, 1e-12);
    }
  }
}

// --- C2: gradients --------------------------------------------------------------
TEST(Operators3DTest, GradientsAreExactForLinearFieldsInEveryCell) {
  struct Linear {
    const char* name;
    Vector3 gradient;
    Real offset;
  };
  const std::vector<Linear> fields = {{"constant", Vector3{0.0, 0.0, 0.0}, 2.5},
                                      {"x", Vector3{1.0, 0.0, 0.0}, 0.0},
                                      {"y", Vector3{0.0, 1.0, 0.0}, 0.0},
                                      {"z", Vector3{0.0, 0.0, 1.0}, 0.0},
                                      {"2x - 3y + 4z", Vector3{2.0, -3.0, 4.0}, 0.0}};
  for (const Box& b : boxes()) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(b.build());
    for (const auto& field : fields) {
      const Field phi = [&](const Vector3& p) { return field.offset + dot(field.gradient, p); };
      const ScalarField values = sample(mesh, phi);
      std::vector<std::pair<std::string, BoundaryConditionSet>> data;
      data.emplace_back("Dirichlet", cfd::test::makeExactBoundaries(mesh, phi));
      if (std::string(b.name).rfind("M5", 0) == 0 || std::string(b.name).rfind("M6", 0) == 0) {
        data.emplace_back("Neumann", exactNeumann(mesh, field.gradient));
      }
      for (const auto& [kind, bcs] : data) {
        for (const auto scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
          SCOPED_TRACE(std::string(b.name) + ", " + field.name + ", " + kind + ", " +
                       std::string(cfd::discretization::gradientSchemeName(scheme)));
          const VectorField g = cfd::discretization::gradient(mesh, values, bcs, scheme);
          for (const auto& cell : mesh.cells()) {
            EXPECT_NEAR(g[cell.id()].x, field.gradient.x, 1e-9) << "cell " << cell.id();
            EXPECT_NEAR(g[cell.id()].y, field.gradient.y, 1e-9) << "cell " << cell.id();
            EXPECT_NEAR(g[cell.id()].z, field.gradient.z, 1e-9) << "cell " << cell.id();
          }
        }
      }
    }
  }
}

TEST(Operators3DTest, LeastSquaresPrimitiveSolvesTheThreeByThreeSystem) {
  const Vector3 g{0.7, -1.3, 2.1};
  const std::vector<Vector3> displacements = {Vector3{0.3, 0.0, 0.1}, Vector3{-0.2, 0.25, 0.0},
                                              Vector3{0.05, -0.3, 0.2}, Vector3{0.0, 0.1, -0.4},
                                              Vector3{-0.1, -0.1, -0.1}};
  std::vector<Real> differences;
  for (const auto& d : displacements) differences.push_back(dot(g, d));
  const auto result = cfd::discretization::solveLeastSquaresGradient(displacements, differences);
  ASSERT_TRUE(result.wellConditioned);
  EXPECT_NEAR(result.gradient.x, g.x, 1e-12);
  EXPECT_NEAR(result.gradient.y, g.y, 1e-12);
  EXPECT_NEAR(result.gradient.z, g.z, 1e-12);

  // Coplanar 3D displacements (all in the plane x + y + z = 0): rank 2, the
  // gradient component along (1, 1, 1) is undetermined -> ill-conditioned.
  const std::vector<Vector3> coplanar = {Vector3{1.0, -1.0, 0.0}, Vector3{0.0, 1.0, -1.0},
                                         Vector3{-1.0, 0.0, 1.0}, Vector3{2.0, -1.0, -1.0}};
  const auto flat = cfd::discretization::solveLeastSquaresGradient(
      coplanar, std::vector<Real>{0.1, 0.2, 0.3, 0.4});
  EXPECT_FALSE(flat.wellConditioned);
  EXPECT_TRUE(flat.gradient == Vector3{});
}

// --- C3: diffusion ----------------------------------------------------------------
TEST(Operators3DTest, ExplicitDiffusionIsExactForAQuadraticField) {
  // phi = x^2 + 2y^2 + 3z^2 + xy - yz + 2xz - x + 1: Laplacian 2 + 4 + 6 = 12.
  const Field phi = [](const Vector3& p) {
    return (p.x * p.x) + (2.0 * p.y * p.y) + (3.0 * p.z * p.z) + (p.x * p.y) - (p.y * p.z) +
           (2.0 * p.x * p.z) - p.x + 1.0;
  };
  const Real gamma = 1.7;
  const std::vector<Box> meshes = {
      {"6x5x4 on M5's domain", 6, 5, 4, 1.5, 0.7, 2.3, Vector3{-1.2, 0.4, 3.1}}, box(5)};
  for (const Box& b : meshes) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(b.build());
    const ScalarField result = cfd::discretization::diffusion(
        mesh, sample(mesh, phi), gamma, cfd::test::makeExactBoundaries(mesh, phi));
    Real volumeSum = 0.0;
    Real domain = 0.0;
    for (const auto& cell : mesh.cells()) {
      EXPECT_NEAR(result[cell.id()], gamma * 12.0, 1e-8) << "cell " << cell.id();
      volumeSum += result[cell.id()] * cell.volume();
      domain += cell.volume();
    }
    // Telescoping (clarified C3): interior fluxes cancel pairwise, so the sum is
    // the boundary integral Gamma * closed-integral(dphi/dn) = Gamma * 12 * |Omega|.
    const Real boundaryIntegral = gamma * 12.0 * (b.lx * b.ly * b.lz);
    EXPECT_LE(std::abs(volumeSum - boundaryIntegral), 1e-10 * boundaryIntegral);
    EXPECT_LE(std::abs(domain - (b.lx * b.ly * b.lz)), 1e-12 * domain);
  }
}

TEST(Operators3DTest, ExplicitDiffusionWithZeroFluxBoundariesConservesGlobally) {
  // C3 conservation clause, evaluated as the 2D test ZeroFluxBoundaryConservesGlobally
  // (acceptance_gate.md, clarification): a constant field, FixedGradient(0) on all six sides.
  const Mesh mesh = box(4).build();
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  const ScalarField result =
      cfd::discretization::diffusion(mesh, ScalarField(mesh.numberOfCells(), 5.0), 3.0, bcs);
  Real sum = 0.0;
  for (const auto& cell : mesh.cells()) sum += result[cell.id()] * cell.volume();
  EXPECT_EQ(sum, 0.0);
}

// --- C4: convection ----------------------------------------------------------------
TEST(Operators3DTest, DivergenceFreeMassFluxBalancesInEveryCell) {
  for (const Box& b : boxes()) {
    SCOPED_TRACE(b.name);
    const Mesh mesh = b.build();
    const SurfaceField flux = gateMassFlux(mesh, 1.3);
    for (const auto& cell : mesh.cells()) {
      Real net = 0.0;
      Real total = 0.0;
      for (const Index faceId : cell.faceIds()) {
        const Real f = flux[faceId];
        net += (mesh.face(faceId).owner() == cell.id()) ? f : -f;
        total += std::abs(f);
      }
      EXPECT_LE(std::abs(net), 1e-13 * total) << "cell " << cell.id();
    }
  }
}

TEST(Operators3DTest, ConvectionPreservesAConstantWithEveryScheme) {
  for (const std::size_t k : {std::size_t{4}, std::size_t{5}}) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(box(k).build());
    const Field constant = [](const Vector3&) { return 3.7; };
    const SurfaceField flux = gateMassFlux(mesh, 1.3);
    for (const auto scheme : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                              ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
      SCOPED_TRACE(std::string(box(k).name) + ", " +
                   std::string(cfd::discretization::convectionSchemeName(scheme)));
      const ScalarField result =
          cfd::discretization::convection(mesh, sample(mesh, constant), flux,
                                          cfd::test::makeExactBoundaries(mesh, constant), scheme);
      for (const auto& cell : mesh.cells()) EXPECT_LE(std::abs(result[cell.id()]), 1e-10);
    }
  }
}

TEST(Operators3DTest, HigherOrderConvectionIsExactForALinearFieldAwayFromTheBoundary) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(box(5).build());
  const Real density = 1.3;
  const Vector3 gradient{1.0, -2.0, 3.0};
  const Field phi = [&](const Vector3& p) { return 1.0 + dot(gradient, p); };
  const SurfaceField flux = gateMassFlux(mesh, density);
  const auto band = cfd::validation::boundaryAdjacentCells(mesh, 2);
  Index interior = 0;
  for (const bool inBand : band) interior += inBand ? 0 : 1;
  ASSERT_EQ(interior, 64U);  // 4 x 4 x 4 of the 8 x 8 x 8 cells
  for (const auto scheme :
       {ConvectionScheme::Central, ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    SCOPED_TRACE(std::string(cfd::discretization::convectionSchemeName(scheme)));
    const ScalarField result = cfd::discretization::convection(
        mesh, sample(mesh, phi), flux, cfd::test::makeExactBoundaries(mesh, phi), scheme);
    for (const auto& cell : mesh.cells()) {
      if (band[cell.id()]) continue;
      const Real exact = density * dot(gateVelocity(cell.centroid()), gradient);
      EXPECT_NEAR(result[cell.id()], exact, 1e-9) << "cell " << cell.id();
    }
  }
}

// --- C5 (P12-MESH-006 gate G1.3): which components take a 3D mesh ---------------------
// MESH-005 guarded every component that was two-dimensional by construction. MESH-006
// generalized the incompressible momentum / pressure-correction / velocity-gradient path to
// three components (u, v, w); those components now ACCEPT a 3D mesh -- and refuse one when
// the W-specific input they need (the w response coefficient, the w gradient) is missing.
// Every component that is still two-dimensional keeps refusing a 3D mesh explicitly.
TEST(Operators3DTest, ThreeDimensionalCapableComponentsAcceptA3DMesh) {
  const Mesh mesh = box(3).build();  // 2 x 2 x 2
  const Index n = mesh.numberOfCells();
  BoundaryConditionSet walls;
  BoundaryConditionSet pressure;
  for (const auto& patch : mesh.boundaryPatches()) {
    walls.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
    pressure.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  const VectorField velocity(n, Vector3{0.1, 0.2, 0.3});
  const ScalarField scalar(n, 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  cfd::algebra::Vector rhs(n, 0.0);
  for (const auto component :
       {cfd::physics::VelocityComponent::U, cfd::physics::VelocityComponent::V,
        cfd::physics::VelocityComponent::W}) {
    cfd::algebra::SparseMatrixBuilder builder(n, n);
    EXPECT_NO_THROW(cfd::physics::assembleDiffusionContribution(mesh, 1.0, velocity, walls,
                                                                component, builder, rhs));
    EXPECT_NO_THROW(cfd::physics::assembleDiffusionContribution(mesh, scalar, velocity, walls,
                                                                component, builder, rhs, true));
    EXPECT_NO_THROW(cfd::physics::assembleConvectionContribution(
        mesh, flux, velocity, walls, component, builder, rhs, ConvectionScheme::LinearUpwind));
    EXPECT_NO_THROW(
        cfd::physics::assemblePressureSourceContribution(mesh, scalar, pressure, component, rhs));
    EXPECT_NO_THROW(
        cfd::physics::assembleMomentumSourceContribution(mesh, velocity, component, rhs));
  }
  const auto gradient = cfd::discretization::computeVelocityGradient(mesh, velocity, walls);
  EXPECT_EQ(gradient.gradW.size(), n);
  const cfd::algebra::Vector diagonal(n, 2.0);
  const ScalarField d = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diagonal);
  EXPECT_NO_THROW((void)cfd::pressure_velocity::assemblePressureCorrection(mesh, flux, d, d, 1.0, 0,
                                                                           pressure, {}, &d));
  EXPECT_NO_THROW((void)cfd::pressure_velocity::correctFaceMassFlux(mesh, flux, flux, scalar));
  EXPECT_NO_THROW((void)cfd::pressure_velocity::correctVelocity(
      mesh, velocity, d, d, scalar, pressure, GradientScheme::GreenGauss, &d));
  const Face& face = mesh.face(1);  // an internal x-face of the 2 x 2 x 2 mesh
  EXPECT_NO_THROW((void)cfd::discretization::interpolateInternalFaceSkewCorrected(
      mesh, face, velocity, gradient.gradU, gradient.gradV, &gradient.gradW));

  // The W inputs are required on a 3D mesh.
  const auto expectNeedsW = [](const std::function<void()>& call) {
    try {
      call();
      ADD_FAILURE() << "accepted a 3D mesh without its w input";
    } catch (const InvalidArgumentError& e) {
      EXPECT_NE(std::string(e.what()).find("3D"), std::string::npos) << e.what();
    }
  };
  expectNeedsW([&] {
    (void)cfd::pressure_velocity::assemblePressureCorrection(mesh, flux, d, d, 1.0, 0, pressure);
  });
  expectNeedsW([&] {
    (void)cfd::pressure_velocity::correctVelocity(mesh, velocity, d, d, scalar, pressure);
  });
  expectNeedsW([&] {
    (void)cfd::discretization::interpolateInternalFaceSkewCorrected(mesh, face, velocity, velocity,
                                                                    velocity);
  });
}

TEST(Operators3DTest, TwoDimensionalOnlyComponentsRefuseA3DMesh) {
  const Mesh mesh = box(3).build();  // 2 x 2 x 2
  const Index n = mesh.numberOfCells();
  BoundaryConditionSet walls;
  BoundaryConditionSet pressure;
  for (const auto& patch : mesh.boundaryPatches()) {
    walls.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
    pressure.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  const VectorField velocity(n);
  const ScalarField scalar(n, 1.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  const cfd::physics::FluidProperties fluid(1.0, 1.0);
  const auto u = cfd::physics::VelocityComponent::U;
  const auto expectRefused = [](const std::function<void()>& call, const std::string& component) {
    try {
      call();
      ADD_FAILURE() << component << " accepted a 3D mesh";
    } catch (const InvalidArgumentError& e) {
      EXPECT_NE(std::string(e.what()).find(component), std::string::npos) << e.what();
      EXPECT_NE(std::string(e.what()).find("two-dimensional"), std::string::npos) << e.what();
    }
  };

  // The two-component convenience wrapper of the momentum equation.
  expectRefused(
      [&] {
        (void)cfd::physics::assembleMomentum(mesh, velocity, scalar, flux, fluid, walls, pressure);
      },
      "assembleMomentum");
  // Turbulence (2D production term / wall distance).
  expectRefused([&] { (void)cfd::turbulence::computeWallDistance(mesh, walls); },
                "computeWallDistance");
  expectRefused(
      [&] {
        cfd::turbulence::KEpsilonConfig config;
        config.initialK = 1e-3;
        config.initialEpsilon = 1e-4;
        (void)cfd::turbulence::KEpsilonModel(mesh, fluid, walls, pressure, pressure, config);
      },
      "KEpsilonModel");
  expectRefused(
      [&] {
        cfd::turbulence::KOmegaConfig config;
        config.initialK = 1e-3;
        config.initialOmega = 1.0;
        (void)cfd::turbulence::KOmegaModel(mesh, fluid, walls, pressure, pressure, config);
      },
      "KOmegaModel");
  expectRefused(
      [&] {
        cfd::turbulence::SSTConfig config;
        config.initialK = 1e-3;
        config.initialOmega = 1.0;
        (void)cfd::turbulence::SSTModel(mesh, fluid, walls, pressure, pressure, config);
      },
      "computeWallDistance");  // SST's wall distance is its first 2D-only step
  expectRefused([&] { (void)cfd::viz::vorticity2D(mesh, velocity); }, "vorticity2D");
  // Transient (PISO) momentum.
  expectRefused(
      [&] {
        (void)cfd::pressure_velocity::assembleTransientMomentumComponent(
            mesh, velocity, scalar, flux, fluid, walls, pressure, u, scalar, 0.1);
      },
      "assembleTransientMomentumComponent");
  expectRefused(
      [&] {
        const cfd::pressure_velocity::PISO piso(mesh, fluid, walls, pressure,
                                                cfd::pressure_velocity::PISOSettings{});
        (void)piso.solveTimeStep(cfd::solver::TransientState{velocity, scalar, flux}, 0.1);
      },
      "PISO");
  // Compressible solver and its components.
  const cfd::compressible::ThermodynamicProperties gas(287.0, 1005.0);
  expectRefused(
      [&] {
        const cfd::compressible::CompressibleSIMPLE solver(
            cfd::compressible::CompressibleSIMPLESettings{}, gas, 1e5, 0);
        (void)solver.solve(mesh, 1e-5, walls, pressure, ScalarField(n, 300.0), nullptr, velocity,
                           scalar, scalar);
      },
      "CompressibleSIMPLE");
  expectRefused(
      [&] {
        (void)cfd::compressible::assembleCompressibleMomentumComponent(
            mesh, velocity, scalar, scalar, flux, scalar, scalar, 1e-5, walls, pressure, u, 0.1);
      },
      "assembleCompressibleMomentumComponent");
  expectRefused(
      [&] {
        (void)cfd::compressible::calculateCompressibleMassFlux(mesh, velocity, scalar, walls,
                                                               scalar, pressure, 1e5, gas,
                                                               ScalarField(n, 300.0), nullptr);
      },
      "calculateCompressibleMassFlux");

  // The 2D result writers and restart (3D results use CSVWriter::writeFields3D /
  // VTKWriter::writeCellFields; JSONWriter::writeMetadata has a 3D branch).
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = velocity;
  result.pressure = scalar;
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_mesh006_guard";
  expectRefused([&] { cfd::io::VTKWriter::writeSolution(dir / "s.vtk", mesh, result); },
                "VTKWriter::writeSolution");
  expectRefused([&] { cfd::io::CSVWriter::writeFields(dir / "f.csv", mesh, result); },
                "CSVWriter::writeFields");
  expectRefused(
      [&] {
        (void)cfd::solver::makeRestartSnapshot(
            mesh, cfd::solver::TransientState{velocity, scalar, flux}, 0.0, 0.1, 0);
      },
      "makeRestartSnapshot");
  expectRefused([&] { cfd::solver::validateRestartSnapshot(cfd::solver::RestartSnapshot{}, mesh); },
                "validateRestartSnapshot");
}
