// P12-MESH-006 gate G10.1: 2D bit identity of every quantity the 3D solver generalisation touches.
// Extends results/p12-mesh-005/tools/bitprobe.cpp (geometry and scalar operators, still hashed via
// that probe) with the pressure-velocity path. For each committed case (its own mesh, boundary
// conditions and solver settings, built by CaseBuilder) and two programmatic Cartesian meshes it hashes
// the exact IEEE bits of:
//   - relaxed U and V momentum systems (matrix values, RHS, diagonal) for upwind, central,
//     linear_upwind and quick, with and without the non-orthogonal correction (least squares), with
//     mixed velocity BC types (inlet, wall, moving wall, outlet, symmetry) and a momentum source;
//   - the velocity gradient (Green-Gauss and least squares);
//   - momentum response coefficients, the pressure-correction system (matrix, RHS, face coefficients,
//     explicit face flux; two-point and non-orthogonal, with a previous p'), corrected face flux and
//     corrected velocity;
//   - complete SIMPLE solves capped at 25 outer iterations (velocity, pressure, flux, the four residual
//     histories, iteration count, status) with the case's settings and three variants (quick,
//     least squares + 2 non-orthogonal corrections, linear_upwind + robustness normalization).
// Uses only APIs that exist in both the pre-MESH-006 and the MESH-006 trees; the two outputs must be
// byte-identical.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using discretization::ConvectionScheme;
using discretization::GradientScheme;

namespace {

struct Hash {
  std::uint64_t h = 14695981039346656037ULL;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ULL;
    }
  }
  void real(Real v) { bytes(&v, sizeof v); }
  void index(Index v) { bytes(&v, sizeof v); }
  // x and y only: the pre-MESH-006 2D fields have no z to hash; a 2D field's z is 0 in the new tree.
  void vec(const Vector2& v) {
    real(v.x);
    real(v.y);
  }
  void system(const algebra::LinearSystem& s) {
    const auto& A = s.matrix();
    for (Index k = 0; k < A.nonZeros(); ++k) real(A.valuesData()[k]);
    for (Index k = 0; k < A.nonZeros(); ++k) index(A.columnIndicesData()[k]);
    for (Index i = 0; i < s.rhs().size(); ++i) real(s.rhs()[i]);
  }
};

const char* statusName(pressure_velocity::SIMPLEStatus s) {
  switch (s) {
    case pressure_velocity::SIMPLEStatus::Converged: return "Converged";
    case pressure_velocity::SIMPLEStatus::MaxIterations: return "MaxIterations";
    case pressure_velocity::SIMPLEStatus::MomentumFailure: return "MomentumFailure";
    case pressure_velocity::SIMPLEStatus::PressureCorrectionFailure: return "PressureCorrectionFailure";
    case pressure_velocity::SIMPLEStatus::NonFiniteState: return "NonFiniteState";
    case pressure_velocity::SIMPLEStatus::InvalidConfiguration: return "InvalidConfiguration";
    case pressure_velocity::SIMPLEStatus::Cancelled: return "Cancelled";
    case pressure_velocity::SIMPLEStatus::Stagnated: return "Stagnated";
    case pressure_velocity::SIMPLEStatus::Diverging: return "Diverging";
  }
  return "?";
}

// Operator-level probe with mixed velocity BC types assigned per patch (cycled in patch order).
std::uint64_t probeOperators(const mesh::Mesh& m) {
  const Index n = m.numberOfCells();
  fields::VectorField u(n);
  fields::ScalarField p(n);
  fields::ScalarField mu(n);
  fields::VectorField source(n);
  for (const auto& c : m.cells()) {
    const Vector2 x = c.centroid();
    u[c.id()] = Vector2{0.4 + std::sin(1.3 * x.x) * std::cos(0.7 * x.y), 0.2 * std::cos(0.9 * x.x + 0.3 * x.y)};
    p[c.id()] = 0.5 * x.x * x.x - 0.3 * x.y + 0.1 * std::sin(2.0 * x.x * x.y);
    mu[c.id()] = 0.01 + 0.002 * x.y * x.y;
    source[c.id()] = Vector2{0.3 * x.y, -0.2 * x.x};
  }
  fields::SurfaceField flux(m.numberOfFaces());
  for (const auto& face : m.faces()) flux[face.id()] = dot(Vector2{1.0, 0.45}, face.areaVector());

  boundary::BoundaryConditionSet vb;
  boundary::BoundaryConditionSet pb;
  Index k = 0;
  for (const auto& patch : m.boundaryPatches()) {
    switch (k % 5) {
      case 0: vb.set(m, patch.name(), std::make_unique<boundary::Inlet>(Vector2{0.7, 0.1})); break;
      case 1: vb.set(m, patch.name(), std::make_unique<boundary::Wall>()); break;
      case 2: vb.set(m, patch.name(), std::make_unique<boundary::MovingWall>(Vector2{0.9, 0.0})); break;
      case 3: vb.set(m, patch.name(), std::make_unique<boundary::Outlet>()); break;
      default: vb.set(m, patch.name(), std::make_unique<boundary::Symmetry>()); break;
    }
    if (k == 1) pb.set(m, patch.name(), std::make_unique<boundary::FixedValue>(0.25));
    else pb.set(m, patch.name(), std::make_unique<boundary::FixedGradient>(0.05 * static_cast<Real>(k)));
    ++k;
  }

  Hash h;
  for (const auto scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    const auto g = discretization::computeVelocityGradient(m, u, vb, scheme);
    for (Index i = 0; i < n; ++i) {
      h.vec(g.gradU[i]);
      h.vec(g.gradV[i]);
    }
  }
  fields::ScalarField previousU(n);
  fields::ScalarField previousV(n);
  for (Index i = 0; i < n; ++i) {
    previousU[i] = u[i].x;
    previousV[i] = u[i].y;
  }
  std::vector<physics::MomentumAssembly> last;
  for (const bool corrected : {false, true}) {
    for (const auto cs : {ConvectionScheme::Upwind, ConvectionScheme::Central,
                          ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
      last.clear();
      for (const auto comp : {physics::VelocityComponent::U, physics::VelocityComponent::V}) {
        auto a = pressure_velocity::assembleRelaxedMomentumComponent(
            m, u, p, flux, mu, vb, pb, comp, comp == physics::VelocityComponent::U ? previousU : previousV,
            0.7, nullptr, nullptr, cs, corrected ? GradientScheme::LeastSquares : GradientScheme::GreenGauss,
            corrected, nullptr, &source);
        h.system(a.system);
        for (Index i = 0; i < n; ++i) h.real(a.diagonal[i]);
        last.push_back(std::move(a));
      }
    }
  }
  const auto dU = pressure_velocity::computeMomentumResponseCoefficient(m, last[0].diagonal);
  const auto dV = pressure_velocity::computeMomentumResponseCoefficient(m, last[1].diagonal);
  for (Index i = 0; i < n; ++i) {
    h.real(dU[i]);
    h.real(dV[i]);
  }
  fields::ScalarField pPrev(n);
  for (Index i = 0; i < n; ++i) pPrev[i] = 1e-3 * std::sin(static_cast<Real>(i));
  for (const bool nonOrth : {false, true}) {
    pressure_velocity::PressureCorrectionOptions options;
    options.nonOrthogonal = nonOrth;
    options.gradientScheme = nonOrth ? GradientScheme::LeastSquares : GradientScheme::GreenGauss;
    options.previousPressureCorrection = nonOrth ? &pPrev : nullptr;
    const auto pc = pressure_velocity::assemblePressureCorrection(m, flux, dU, dV, 1.2, 0, pb, options);
    h.system(pc.system);
    for (Index f = 0; f < m.numberOfFaces(); ++f) {
      h.real(pc.faceCoefficient[f]);
      h.real(pc.explicitFaceFlux[f]);
    }
    const auto fluxNew = pressure_velocity::correctFaceMassFlux(m, flux, pc.faceCoefficient, pPrev,
                                                                nonOrth ? &pc.explicitFaceFlux : nullptr);
    for (Index f = 0; f < m.numberOfFaces(); ++f) h.real(fluxNew[f]);
    const auto uNew = pressure_velocity::correctVelocity(m, u, dU, dV, pPrev, pb, options.gradientScheme);
    for (Index i = 0; i < n; ++i) h.vec(uNew[i]);
  }
  return h.h;
}

void probeSolve(const std::string& label, const io::SimulationSetup& setup,
                pressure_velocity::SIMPLESettings settings) {
  settings.maxIterations = 25;
  const pressure_velocity::SIMPLE simple(settings, 0);
  const auto r = simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                              setup.initialVelocity, setup.initialPressure);
  Hash h;
  for (Index i = 0; i < r.velocity.size(); ++i) h.vec(r.velocity[i]);
  for (Index i = 0; i < r.pressure.size(); ++i) h.real(r.pressure[i]);
  for (Index f = 0; f < r.massFlux.size(); ++f) h.real(r.massFlux[f]);
  for (const auto* hist : {&r.uResidualHistory, &r.vResidualHistory, &r.pressureResidualHistory, &r.continuityHistory})
    for (const Real v : *hist) h.real(v);
  std::printf("    solve %-28s it %3llu %-24s %016llx\n", label.c_str(),
              static_cast<unsigned long long>(r.iterations), statusName(r.status),
              static_cast<unsigned long long>(h.h));
}

void probeCase(const std::string& name, const io::SimulationSetup& setup) {
  std::printf("%-40s cells %6llu operators %016llx\n", name.c_str(),
              static_cast<unsigned long long>(setup.mesh.numberOfCells()),
              static_cast<unsigned long long>(probeOperators(setup.mesh)));
  probeSolve("case settings", setup, setup.solverSettings);
  auto quick = setup.solverSettings;
  quick.convectionScheme = ConvectionScheme::QUICK;
  probeSolve("quick", setup, quick);
  auto corrected = setup.solverSettings;
  corrected.gradientScheme = GradientScheme::LeastSquares;
  corrected.nonOrthogonalCorrections = 2;
  probeSolve("least_squares + 2 corrections", setup, corrected);
  auto robust = setup.solverSettings;
  robust.convectionScheme = ConvectionScheme::LinearUpwind;
  robust.robustness.convergenceCriterion = solver::ConvergenceCriterion::Normalized;
  robust.robustness.adaptiveRelaxation.enabled = true;
  probeSolve("linear_upwind + normalized/adaptive", setup, robust);
}

}  // namespace

int main(int argc, char** argv) {
  for (const auto& [label, mesh] : {std::pair<std::string, mesh::Mesh>{"cartesian 4x3 (1 x 1)",
                                        mesh::MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0)},
                                    std::pair<std::string, mesh::Mesh>{"cartesian 13x7 (2.1 x 0.9)",
                                        mesh::MeshGeometry::createCartesian2D(13, 7, 2.1, 0.9)}}) {
    std::printf("%-40s cells %6llu operators %016llx\n", label.c_str(),
                static_cast<unsigned long long>(mesh.numberOfCells()),
                static_cast<unsigned long long>(probeOperators(mesh)));
  }
  for (int a = 1; a < argc; ++a) {
    const std::string dir = argv[a];
    const auto def = io::CaseReader().read(dir);
    const auto setup = io::CaseBuilder().build(def);
    probeCase(dir.substr(dir.find_last_of('/') + 1), setup);
  }
  return 0;
}
