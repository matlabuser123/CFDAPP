// P12-NUM-006: system-level MMS for the momentum equations alone, the
// pressure-gradient mechanism, and the continuity / face-flux evaluation --
// each verified separately BEFORE the coupled SIMPLE solve
// (test_mms_simple.cpp), so a defect is localized to its layer.
//
// Momentum only: the production relaxed-momentum assembly SIMPLE uses
// (diffusion, convection with the selected scheme, the -V grad p source
// through the production gradient, the generic momentum source, exact
// Dirichlet velocity on every boundary face), with the EXACT pressure and
// the EXACT face mass flux (frozen) and the hand-derived continuous forcing
// f = rho (U.grad)U + grad p - mu lap U (ManufacturedFieldsTest.
// MomentumForcingExact), solved by Picard iteration on the lagged
// (deferred-correction) terms from U = 0.
#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <map>
#include <string>

#include "DistortedMesh.hpp"
#include "MMSCases.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/validation/ErrorNorms.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::validation::MMSLevel;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;
namespace mc = cfd::test::mmscase;
namespace mms = cfd::test::mms;

namespace {

std::string failures(const MMSStudy& study, const std::string& category) {
  std::string text;
  mc::categoryPassed(study, category, &text);
  return text;
}

// Expected order class per scheme: upwind convection is first order;
// central and linear-upwind (deferred correction, Sweby/van Leer limited,
// P12-NUM-001) with second-order diffusion and pressure gradient are second
// order. Bands: first-order class [0.8, 1.2]; second-order class
// [1.8, 2.4] (TVD limiting cannot exceed 2 asymptotically; the measured
// pre-asymptotic values approach 2 from above, e.g. 2.12 at 32/64/128).
struct Band {
  Real formal;
  Real lo;
  Real hi;
};
Band bandFor(ConvectionScheme scheme) {
  return scheme == ConvectionScheme::Upwind ? Band{1.0, 0.8, 1.2} : Band{2.0, 1.8, 2.4};
}

// One momentum-only study per scheme on 16/32/64/128, cached per process
// (UConverges and VConverges share it when run in one process).
const MMSStudy& momentumStudy(ConvectionScheme scheme) {
  static std::map<ConvectionScheme, MMSStudy> cache;
  if (auto it = cache.find(scheme); it != cache.end()) return it->second;
  MMSStudy study;
  study.name = "momentum_mms_" + mc::schemeName(scheme);
  study.description =
      "Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, "
      "exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0";
  study.manufacturedSolution =
      "mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; "
      "mms::pressure: p = cos(pi x) cos(pi y) + x y / 2";
  study.coefficients = {{"density", mc::kDensity}, {"viscosity", mc::kViscosity}};
  study.configuration = {
      {"assembly", "cfd::pressure_velocity::assembleRelaxedMomentumComponent (alpha = 1)"},
      {"convection", mc::schemeName(scheme)},
      {"gradient", "green_gauss"},
      {"velocity_bc", "exact Dirichlet (Inlet) on every boundary face"},
      {"pressure",
       "exact at cell centroids; exact Neumann (FixedGradient grad p . n) on every face"},
      {"face_mass_flux", "exact face integral of rho U.n, frozen"},
      {"forcing",
       "analytical f = rho (U.grad)U + grad p - mu lap U at cell centroids; never "
       "discrete_operator(exact_solution)"},
      {"linear_solver", "BiCGSTAB abs 1e-12 / rel 1e-10 with the P12-NUM-004 fallback"},
      {"picard_tolerance", "max velocity change < 1e-10"}};
  mc::MomentumOptions options;
  options.scheme = scheme;
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128})
    study.levels.push_back(mc::runMomentumLevel(n, options));
  if (mc::allLevelsAccepted(study)) {
    const Band band = bandFor(scheme);
    for (const std::string component : {"u", "v"}) {
      mc::addDecreaseGate(study, component, component);
      mc::addOrderGate(study, component, component, NormKind::L2, band.formal, band.lo, band.hi);
      mc::addOrderGate(study, component, component, NormKind::L1, band.formal, band.lo, band.hi);
      // Linf: the same class; the boundary-adjacent ring carries the
      // largest error, measured converging at the same rate.
      mc::addOrderGate(study, component, component, NormKind::Linf, band.formal, band.lo - 0.2,
                       band.hi);
    }
    mc::addOrder(study, "velocity", NormKind::L2, band.formal);
    mc::addOrder(study, "u_interior", NormKind::L2, band.formal);
    mc::addOrder(study, "u_boundary_ring", NormKind::L2, band.formal);
  }
  return cache.emplace(scheme, std::move(study)).first->second;
}

void expectComponent(const std::string& component) {
  for (const auto scheme :
       {ConvectionScheme::Upwind, ConvectionScheme::Central, ConvectionScheme::LinearUpwind}) {
    const MMSStudy& study = momentumStudy(scheme);
    ASSERT_TRUE(mc::allLevelsAccepted(study)) << mc::schemeName(scheme);
    EXPECT_TRUE(mc::categoryPassed(study, component, nullptr)) << mc::schemeName(scheme) << ":\n"
                                                               << failures(study, component);
  }
}

}  // namespace

TEST(MomentumMMS, UConverges) {
  expectComponent("u");
  // Written once (this test only -- avoids concurrent writers under ctest -j).
  mc::writeStudy(momentumStudy(ConvectionScheme::Central), "momentum_mms");
  mc::writeStudy(momentumStudy(ConvectionScheme::Upwind), "momentum_mms_upwind");
  mc::writeStudy(momentumStudy(ConvectionScheme::LinearUpwind), "momentum_mms_linear_upwind");
}

TEST(MomentumMMS, VConverges) { expectComponent("v"); }

// The production pressure-gradient mechanism of the momentum equation
// (assemblePressureSourceContribution: -V_P grad(p)_P through the
// production gradient reconstruction and the pressure boundary conditions)
// applied to the exact pressure, compared with the analytical -V_P grad p
// -- x and y components, every grid, both gradient schemes. This checks
// the pressure-gradient forcing balance directly, not through a velocity
// error.
TEST(MomentumMMS, PressureGradientBalance) {
  MMSStudy study;
  study.name = "pressure_gradient_mms";
  study.description =
      "Production momentum pressure source -V grad(p) of the exact pressure vs the analytical "
      "-V grad p (per unit volume), x and y, Green-Gauss and least-squares";
  study.manufacturedSolution = "mms::pressure: p = cos(pi x) cos(pi y) + x y / 2";
  study.configuration = {{"assembly", "cfd::physics::assemblePressureSourceContribution"},
                         {"pressure_bc", "exact Neumann (FixedGradient grad p . n) on every face"}};
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128}) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(n, n, 1.0, 1.0);
    const auto pressureBoundaries = mms::makeExactPressureBoundaries(mesh);
    const ScalarField pressure = mms::sampleScalar(mesh, mms::pressure);
    MMSLevel level;
    level.name = std::to_string(n) + "x" + std::to_string(n);
    level.nx = n;
    level.ny = n;
    level.cells = mesh.numberOfCells();
    level.h = 1.0 / static_cast<Real>(n);
    level.solverStatus = "Evaluated";
    level.accepted = true;
    const auto ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
    const auto interior = cfd::validation::invertMask(ring);
    for (const auto scheme : {cfd::discretization::GradientScheme::GreenGauss,
                              cfd::discretization::GradientScheme::LeastSquares}) {
      const std::string tag = scheme == cfd::discretization::GradientScheme::GreenGauss
                                  ? "_green_gauss"
                                  : "_least_squares";
      for (const auto component :
           {cfd::physics::VelocityComponent::U, cfd::physics::VelocityComponent::V}) {
        const bool x = component == cfd::physics::VelocityComponent::U;
        cfd::algebra::Vector rhs(mesh.numberOfCells(), 0.0);
        cfd::physics::assemblePressureSourceContribution(mesh, pressure, pressureBoundaries,
                                                         component, rhs, scheme);
        ScalarField numeric(mesh.numberOfCells());
        ScalarField exact(mesh.numberOfCells());
        for (const auto& cell : mesh.cells()) {
          numeric[cell.id()] = rhs[cell.id()] / cell.volume();  // -(dp/dx_i) per unit volume
          const Vector2 g = mms::pressureGradient(cell.centroid());
          exact[cell.id()] = -(x ? g.x : g.y);
        }
        const std::string name = std::string(x ? "dpdx" : "dpdy") + tag;
        level.errors.emplace_back(name, cfd::validation::computeErrorNorms(mesh, numeric, exact));
        level.errors.emplace_back(name + "_interior", cfd::validation::computeErrorNorms(
                                                          mesh, numeric, exact, &interior));
        level.errors.emplace_back(name + "_boundary_ring",
                                  cfd::validation::computeErrorNorms(mesh, numeric, exact, &ring));
      }
    }
    study.levels.push_back(level);
  }
  // Interior: second order for both schemes and both directions.
  // Green-Gauss boundary ring: the paired quadratic fit uses the face value
  // p_P + g d reconstructed from the exact Neumann data, which is O(h^2)
  // accurate; its 1/h weight makes the ring gradient first order in Linf,
  // and a ring of O(n) cells with O(h) error gives global L2 ~ h^1.5 --
  // bands [0.8, 1.2] and [1.3, 1.7]. Least squares weights the boundary
  // data point with its displacement; no better than first order can be
  // guaranteed a priori (band >= 0.8), second order is what is measured.
  for (const std::string d : {"dpdx", "dpdy"}) {
    for (const std::string tag : {"_green_gauss", "_least_squares"}) {
      mc::addOrderGate(study, "balance", d + tag + "_interior", NormKind::L2, 2.0, 1.8, 2.3);
      mc::addDecreaseGate(study, "balance", d + tag);
    }
    mc::addOrderGate(study, "balance", d + "_green_gauss_boundary_ring", NormKind::Linf, 1.0, 0.8,
                     1.2);
    mc::addOrderGate(study, "balance", d + "_green_gauss", NormKind::L2, 1.5, 1.3, 1.7);
    mc::addOrderGate(study, "balance", d + "_least_squares_boundary_ring", NormKind::Linf, 1.0, 0.8,
                     2.3);
    mc::addOrderGate(study, "balance", d + "_least_squares", NormKind::L2, 2.0, 1.8, 2.3);
  }
  mc::writeStudy(study, "pressure_gradient_mms");
  EXPECT_TRUE(mc::categoryPassed(study, "balance", nullptr)) << failures(study, "balance");
}

// Momentum on distorted meshes: uncorrected two-point diffusion (and
// Green-Gauss) is inconsistent on a skewed mesh -> first order; the
// P12-NUM-003 correction with the least-squares gradient restores second
// order and the Cartesian error level (central convection).
TEST(MomentumMMS, DistortedMesh) {
  MMSStudy study;
  study.name = "distorted_mesh_momentum_mms";
  study.description =
      "Momentum-only MMS (central convection) on Cartesian vs distorted (0.25 h) meshes, "
      "uncorrected "
      "(Green-Gauss) vs non-orthogonal-corrected (least-squares); level status/iterations are the "
      "corrected distorted solve";
  study.manufacturedSolution = "mms::velocity / mms::pressure (as momentum_mms)";
  study.coefficients = {{"density", mc::kDensity},
                        {"viscosity", mc::kViscosity},
                        {"distortion_fraction_of_h", mc::kDistortion}};
  // 16..128: on 16/32/64 alone the corrected u order was 2.39 (reduction
  // factors 5.04, 4.36 -- pre-asymptotic from above, at the band edge);
  // the 128 level puts the finest triplet in the asymptotic range.
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128}) {
    mc::MomentumOptions cartesian;
    cartesian.scheme = ConvectionScheme::Central;
    mc::MomentumOptions uncorrected = cartesian;
    uncorrected.distorted = true;
    mc::MomentumOptions corrected = uncorrected;
    corrected.nonOrthogonalCorrection = true;
    const MMSLevel c = mc::runMomentumLevel(n, cartesian);
    const MMSLevel u = mc::runMomentumLevel(n, uncorrected);
    MMSLevel level = mc::runMomentumLevel(n, corrected);
    ASSERT_TRUE(c.accepted && u.accepted && level.accepted);
    std::vector<std::pair<std::string, cfd::validation::ErrorNorms>> errors;
    for (const auto& [name, norms] : c.errors) errors.emplace_back(name + "_cartesian", norms);
    for (const auto& [name, norms] : u.errors)
      errors.emplace_back(name + "_distorted_uncorrected", norms);
    for (const auto& [name, norms] : level.errors)
      errors.emplace_back(name + "_distorted_corrected", norms);
    level.errors = std::move(errors);
    level.runtimeSeconds += c.runtimeSeconds + u.runtimeSeconds;
    study.levels.push_back(level);
  }
  for (const std::string component : {"u", "v"}) {
    mc::addOrderGate(study, "distorted", component + "_distorted_corrected", NormKind::L2, 2.0, 1.8,
                     2.4);
    mc::addOrderGate(study, "distorted", component + "_distorted_uncorrected", NormKind::L2, 1.0,
                     0.7, 1.3);
    const std::size_t fine = study.levels.size() - 1;
    const Real cart = study.error(fine, component + "_cartesian").l2;
    const Real corr = study.error(fine, component + "_distorted_corrected").l2;
    const Real unc = study.error(fine, component + "_distorted_uncorrected").l2;
    mc::addGate(study,
                "distorted: " + component + " corrected L2 within 10% of Cartesian (finest grid)",
                std::abs(corr - cart) <= 0.1 * cart,
                mc::format("corrected %.4e", corr) + mc::format(", Cartesian %.4e", cart));
    mc::addGate(study, "distorted: " + component + " uncorrected L2 >= 3x corrected (finest grid)",
                unc >= 3.0 * corr, mc::format("ratio %.1f", unc / corr));
  }
  mc::writeStudy(study, "distorted_mesh_momentum_mms");
  EXPECT_TRUE(mc::categoryPassed(study, "distorted", nullptr)) << failures(study, "distorted");
}

// The production continuity / face-flux evaluation on the exact,
// divergence-free velocity: physics::calculateMassFlux (linear
// interpolation of the exact cell-centroid velocity, exact Inlet values at
// boundary faces) -> physics::evaluateContinuity. The discrete divergence
// is not zero (interpolation error) but converges; the exact face-integrated
// flux gives zero per cell to round-off; the global imbalance vanishes
// (zero normal wall velocity).
TEST(ContinuityMMS, DivergenceFreeField) {
  MMSStudy study;
  study.name = "continuity_mms";
  study.description =
      "Production face-flux interpolation (calculateMassFlux) and continuity evaluation "
      "(evaluateContinuity) of the exact divergence-free velocity";
  study.manufacturedSolution = "mms::velocity (streamfunction curl; div U = 0 analytically)";
  study.coefficients = {{"density", mc::kDensity}};
  study.configuration = {
      {"face_velocity", "linear interpolation (internal), exact Inlet value (boundary)"},
      {"divergence", "cell mass imbalance / (rho V)"},
      {"face_flux", "(F_interpolated - F_exact) / (rho A) vs exact face integral"}};
  const cfd::physics::FluidProperties fluid(mc::kDensity, mc::kViscosity);
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128}) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(n, n, 1.0, 1.0);
    const auto velocityBoundaries = mms::makeExactVelocityBoundaries(mesh);
    const VectorField velocity = mms::sampleVector(mesh, mms::velocity);
    const SurfaceField flux =
        cfd::physics::calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
    SurfaceField exactFlux(mesh.numberOfFaces());
    for (const auto& face : mesh.faces())
      exactFlux[face.id()] = mc::kDensity * mms::exactVortexVolumeFlux(face);
    const auto continuity = cfd::physics::evaluateContinuity(mesh, flux);
    const auto exactContinuity = cfd::physics::evaluateContinuity(mesh, exactFlux);
    ScalarField divergence(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      divergence[cell.id()] = continuity.cellImbalance[cell.id()] / (mc::kDensity * cell.volume());
    }
    SurfaceField normalVelocity(mesh.numberOfFaces());
    SurfaceField exactNormalVelocity(mesh.numberOfFaces());
    for (const auto& face : mesh.faces()) {
      normalVelocity[face.id()] = flux[face.id()] / (mc::kDensity * face.area());
      exactNormalVelocity[face.id()] = exactFlux[face.id()] / (mc::kDensity * face.area());
    }
    MMSLevel level;
    level.name = std::to_string(n) + "x" + std::to_string(n);
    level.nx = n;
    level.ny = n;
    level.cells = mesh.numberOfCells();
    level.h = 1.0 / static_cast<Real>(n);
    level.solverStatus = "Evaluated";
    level.accepted = true;
    level.massImbalance = std::abs(continuity.globalNetFlux);
    const auto ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
    const auto interior = cfd::validation::invertMask(ring);
    level.errors.emplace_back("divergence", cfd::validation::computeErrorNorms(mesh, divergence));
    level.errors.emplace_back("divergence_interior",
                              cfd::validation::computeErrorNorms(mesh, divergence, &interior));
    level.errors.emplace_back("divergence_boundary_ring",
                              cfd::validation::computeErrorNorms(mesh, divergence, &ring));
    level.errors.emplace_back("face_flux", cfd::validation::computeFaceErrorNorms(
                                               mesh, normalVelocity, exactNormalVelocity,
                                               cfd::validation::FaceSelection::Internal));
    level.diagnostics = {{"global_mass_imbalance", continuity.globalNetFlux},
                         {"exact_flux_max_cell_imbalance", exactContinuity.maxCellImbalance},
                         {"exact_flux_global_imbalance", exactContinuity.globalNetFlux}};
    mc::addGate(study, "continuity: exact face flux divergence-free per cell at " + level.name,
                exactContinuity.maxCellImbalance <= 1e-15,
                mc::format("max |imbalance| %.2e", exactContinuity.maxCellImbalance));
    mc::addGate(study, "continuity: global mass imbalance ~ 0 at " + level.name,
                std::abs(continuity.globalNetFlux) <= 1e-14,
                mc::format("%.2e (zero normal wall velocity)", continuity.globalNetFlux));
    study.levels.push_back(level);
  }
  // Interpolated face velocity: O(h^2) internal-face error; its cell
  // divergence is second order in the interior, first order (Linf) in the
  // ring where one face is exact and the opposite one interpolated.
  mc::addOrderGate(study, "continuity", "face_flux", NormKind::L2, 2.0, 1.8, 2.3);
  mc::addOrderGate(study, "continuity", "divergence_interior", NormKind::L2, 2.0, 1.8, 2.3);
  mc::addOrderGate(study, "continuity", "divergence", NormKind::L1, 2.0, 1.5, 2.3);
  mc::addDecreaseGate(study, "continuity", "divergence");
  mc::addOrder(study, "divergence_boundary_ring", NormKind::Linf, 1.0);
  mc::writeStudy(study, "continuity_mms");
  EXPECT_TRUE(mc::categoryPassed(study, "continuity", nullptr)) << failures(study, "continuity");
}
