// P12-NUM-006: system-level MMS for scalar advection-diffusion -- the
// production ThermalSolver (EnergyEquation assembly: convection, diffusion,
// the per-cell volumetric source, exact Dirichlet data on every boundary
// face, BiCGSTAB inside the outer Picard loop), started from phi = 0.
//
// Manufactured solution (ManufacturedFields.hpp, mms::scalar /
// mms::advectingVelocity): phi = sin(pi x) sin(pi y) + x^2 y/2 + 1/4 advected
// by U = (1, 1/2) + curl(psi) (real inflow on the left/bottom, outflow on
// the right/top). The source Q = rho cp U.grad(phi) - k lap(phi) is the
// hand-derived continuous forcing (verified independently by
// ManufacturedFieldsTest.ScalarForcingExact), sampled at cell centroids;
// the face mass flux is the exact face integral of rho U.n.
//
// Convection scheme: production scalar transport (thermal/species) has
// first-order upwind convection only -- the P12-NUM-001 higher-order
// schemes are wired into the momentum equations, where MomentumMMS verifies
// them. The expected order here is therefore 1 (formal order of upwind).
#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <string>

#include "MMSCases.hpp"

using cfd::Index;
using cfd::Real;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;
namespace mc = cfd::test::mmscase;

namespace {

std::string failures(const MMSStudy& study, const std::string& category) {
  std::string text;
  mc::categoryPassed(study, category, &text);
  return text;
}

}  // namespace

TEST(ScalarMMS, AdvectionDiffusionConverges) {
  MMSStudy study;
  study.name = "scalar_advection_diffusion_mms";
  study.description =
      "Steady scalar advection-diffusion, production ThermalSolver (cp div(rho U phi) - k lap(phi) "
      "= "
      "Q), five systematically refined Cartesian grids";
  study.manufacturedSolution =
      "mms::scalar: phi = sin(pi x) sin(pi y) + x^2 y / 2 + 1/4 on [0,1]^2; advecting U = (1, 0.5) "
      "+ "
      "(psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi";
  study.coefficients = {{"density", mc::kScalarDensity},
                        {"specific_heat", mc::kScalarSpecificHeat},
                        {"conductivity", mc::kScalarConductivity}};
  study.configuration = {
      {"solver", "cfd::thermal::ThermalSolver (per-cell volumetric source overload)"},
      {"convection", "upwind (the only scheme of production scalar transport)"},
      {"diffusion", "two-point central (Cartesian: orthogonal)"},
      {"boundary_conditions", "exact Dirichlet phi on every boundary face (FixedValue per face)"},
      {"face_mass_flux", "exact face integral of rho U.n (rho (U0.Sf + psi(B) - psi(A)))"},
      {"forcing",
       "analytical Q = rho cp U.grad(phi) - k lap(phi) at cell centroids; never "
       "discrete_operator(exact_solution)"},
      {"initial_condition", "phi = 0"},
      {"tolerances", "outer max change 1e-10; BiCGSTAB abs 1e-9 / rel 1e-8"}};
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128, 256}) {
    study.levels.push_back(mc::runScalarLevel(n, mc::ScalarOptions{}));
  }
  ASSERT_TRUE(mc::allLevelsAccepted(study)) << study.levels.back().rejectionReason;
  mc::addGate(study, "scalar: every solve Converged", true,
              "ThermalStatus::Converged on all 5 grids");
  mc::addDecreaseGate(study, "scalar", "phi");
  // Upwind convection is formally first order; the measured orders
  // approach 1 from below (the O(h^2) diffusion/boundary contributions have
  // the opposite sign at coarse h). Band [0.8, 1.2]: first-order class,
  // excluding both a stalled (< 0.8) and a spuriously higher-order result.
  mc::addOrderGate(study, "scalar", "phi", NormKind::L1, 1.0, 0.8, 1.2);
  mc::addOrderGate(study, "scalar", "phi", NormKind::L2, 1.0, 0.8, 1.2);
  mc::addOrderGate(study, "scalar", "phi", NormKind::Linf, 1.0, 0.8, 1.2);
  // Consistency: the error extrapolates to (almost) zero -- the scheme
  // converges to the exact solution, not to a nearby wrong one.
  const auto& l2 = study.orders[1];
  const Real extrapolated = l2.triplets.back().extrapolated21.value_or(1.0);
  const Real fineL2 = study.error(study.levels.size() - 1, "phi").l2;
  mc::addGate(
      study, "scalar: extrapolated L2 error |E_ext| <= 0.25 E_fine",
      std::abs(extrapolated) <= 0.25 * fineL2,
      "E_ext " + mc::format("%.3e", extrapolated) + ", E_fine " + mc::format("%.3e", fineL2));
  // Boundary investigation: the Dirichlet-pinned boundary ring is MORE
  // accurate than the interior (its error converges at ~2nd order); the
  // global order is set by the interior upwind error, not by the boundary.
  mc::addOrderGate(study, "boundary", "phi_boundary_ring", NormKind::L2, 2.0, 1.7, 2.3);
  mc::addOrderGate(study, "boundary", "phi_interior", NormKind::L2, 1.0, 0.8, 1.2);
  mc::writeStudy(study, "scalar_advection_diffusion_mms");

  EXPECT_TRUE(mc::categoryPassed(study, "scalar", nullptr)) << failures(study, "scalar");
  EXPECT_TRUE(mc::categoryPassed(study, "boundary", nullptr)) << failures(study, "boundary");
}

// Distorted meshes (DistortedMesh.hpp, vertex perturbation 0.25 h): the
// same solve, diffusion-dominated (k = 5, Pe ~ 0.5) so that the
// non-orthogonal DIFFUSION error is measurable next to the upwind one.
// Uncorrected two-point diffusion is inconsistent on a skewed mesh; the
// P12-NUM-003 correction (least-squares gradient) must restore the
// Cartesian accuracy.
TEST(ScalarMMS, DistortedMeshNonOrthogonalCorrection) {
  MMSStudy study;
  study.name = "distorted_mesh_mms";
  study.description =
      "Scalar advection-diffusion MMS (diffusion-dominated, k = 5) on Cartesian vs distorted (0.25 "
      "h) "
      "meshes, with and without the P12-NUM-003 non-orthogonal correction; level status/iterations "
      "are the corrected distorted solve";
  study.manufacturedSolution = "mms::scalar (as scalar_advection_diffusion_mms)";
  study.coefficients = {{"density", mc::kScalarDensity},
                        {"specific_heat", mc::kScalarSpecificHeat},
                        {"conductivity", mc::kScalarDiffusiveConductivity},
                        {"distortion_fraction_of_h", mc::kDistortion}};
  study.configuration = {
      {"solver", "cfd::thermal::ThermalSolver"},
      {"convection", "upwind"},
      {"correction", "ThermalSolverSettings::nonOrthogonal, least-squares gradient"},
      {"forcing", "analytical, at cell centroids"}};
  for (const Index n : std::initializer_list<Index>{16, 32, 64, 128}) {
    mc::ScalarOptions cartesian;
    cartesian.conductivity = mc::kScalarDiffusiveConductivity;
    mc::ScalarOptions uncorrected = cartesian;
    uncorrected.distorted = true;
    mc::ScalarOptions corrected = uncorrected;
    corrected.nonOrthogonalCorrection = true;
    const auto c = mc::runScalarLevel(n, cartesian);
    const auto u = mc::runScalarLevel(n, uncorrected);
    auto level = mc::runScalarLevel(n, corrected);
    ASSERT_TRUE(c.accepted && u.accepted && level.accepted)
        << c.rejectionReason << u.rejectionReason << level.rejectionReason;
    const auto renamed = [](const cfd::validation::MMSLevel& from, const std::string& suffix) {
      std::vector<std::pair<std::string, cfd::validation::ErrorNorms>> out;
      for (const auto& [name, norms] : from.errors) out.emplace_back(name + suffix, norms);
      return out;
    };
    std::vector<std::pair<std::string, cfd::validation::ErrorNorms>> errors =
        renamed(c, "_cartesian");
    for (auto& e : renamed(u, "_distorted_uncorrected")) errors.push_back(e);
    for (auto& e : renamed(level, "_distorted_corrected")) errors.push_back(e);
    level.errors = std::move(errors);
    level.diagnostics = {{"iterations_cartesian", static_cast<Real>(c.iterations)},
                         {"iterations_distorted_uncorrected", static_cast<Real>(u.iterations)}};
    level.runtimeSeconds += c.runtimeSeconds + u.runtimeSeconds;
    study.levels.push_back(level);
  }
  mc::addDecreaseGate(study, "distorted", "phi_distorted_corrected");
  mc::addDecreaseGate(study, "distorted", "phi_distorted_uncorrected");
  // At this low Peclet number the sequence is first order with a strong
  // opposite-sign h^2 term (reduction factors 1.42, 1.75, 1.88 fit
  // E = a h (1 - 7.2 h) to 1 %), so the three-grid order on 16..128 is
  // ~0.67 for the Cartesian mesh itself -- the first-order evidence is
  // ScalarMMS.AdvectionDiffusionConverges (p 0.93). The claim here is that
  // the correction reproduces the Cartesian convergence: the same observed
  // order (within 0.05) and the same reduction factors (within 2 %).
  const auto& cartesianOrder = mc::addOrder(study, "phi_cartesian", NormKind::L2, 1.0);
  const Real pCartesian = cartesianOrder.finestOrder().value_or(-1.0);
  const auto cartesianFactors = cartesianOrder.reductionFactors;
  const auto& correctedOrder = mc::addOrder(study, "phi_distorted_corrected", NormKind::L2, 1.0);
  const Real pCorrected = correctedOrder.finestOrder().value_or(-2.0);
  mc::addGate(
      study, "distorted: corrected observed order equals Cartesian within 0.05",
      std::abs(pCorrected - pCartesian) <= 0.05,
      mc::format("corrected %.3f", pCorrected) + mc::format(", Cartesian %.3f", pCartesian));
  bool factorsMatch = cartesianFactors.size() == correctedOrder.reductionFactors.size();
  std::string factorText;
  for (std::size_t k = 0; factorsMatch && k < cartesianFactors.size(); ++k) {
    const Real c = cartesianFactors[k].value_or(0.0);
    const Real d = correctedOrder.reductionFactors[k].value_or(0.0);
    factorsMatch = std::abs(d - c) <= 0.02 * c;
    factorText += mc::format(" %.3f", d) + mc::format("/%.3f", c);
  }
  mc::addGate(study, "distorted: corrected reduction factors equal Cartesian within 2%",
              factorsMatch, "corrected/Cartesian:" + factorText);
  mc::addOrder(study, "phi_distorted_uncorrected", NormKind::L2, 1.0);
  for (std::size_t k = 0; k < study.levels.size(); ++k) {
    const Real cart = study.error(k, "phi_cartesian").l2;
    const Real corr = study.error(k, "phi_distorted_corrected").l2;
    const Real unc = study.error(k, "phi_distorted_uncorrected").l2;
    // Corrected distorted within 5 % of the Cartesian error on every grid
    // (measured <= 1.6 %); uncorrected clearly worse on the two finest
    // grids (measured 1.55x).
    mc::addGate(
        study, "distorted: corrected L2 within 5% of Cartesian at " + study.levels[k].name,
        std::abs(corr - cart) <= 0.05 * cart,
        "corrected " + mc::format("%.4e", corr) + ", Cartesian " + mc::format("%.4e", cart));
    if (k + 2 >= study.levels.size()) {
      mc::addGate(study, "distorted: uncorrected L2 >= 1.3x Cartesian at " + study.levels[k].name,
                  unc >= 1.3 * cart,
                  "uncorrected " + mc::format("%.4e", unc) + " = " +
                      mc::format("%.2f", unc / cart) + "x Cartesian");
    }
  }
  mc::writeStudy(study, "distorted_mesh_mms");
  EXPECT_TRUE(mc::categoryPassed(study, "distorted", nullptr)) << failures(study, "distorted");
}
