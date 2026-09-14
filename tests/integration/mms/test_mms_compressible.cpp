// P12-NUM-006: system-level MMS for CompressibleSIMPLE (P12-COMP-002) --
// isothermal ideal gas, closed box, from rest.
//
// Manufactured state (ManufacturedFields.hpp, mms::compressible*): gauge
// pressure p = cos(pi x) cos(pi y) + x y/2, density from the production
// EOS rho = (P_ref + p)/(R T0) (P_ref = R T0 = 20: rho between 0.95 and 1.08),
// mass flux rho U = curl(psi) (the vortex field) so that the compressible
// continuity div(rho U) = 0 holds exactly while div U != 0. The forcing
// f = (m.grad)U + grad p - mu lap U is hand-derived (quotient rule; checked
// by ManufacturedFieldsTest.CompressibleForcingExact) and enters through
// CompressibleSIMPLE::setMomentumSource.
//
// Coverage (exact): compressible continuity (the EOS-coupled pressure-
// correction equation), compressible momentum (compressible mass flux in
// the convection term, pseudo-transient terms vanishing at convergence),
// EOS consistency of the converged state. NOT covered, by scope: the
// energy equation (temperature is a fixed input of CompressibleSIMPLE),
// the mu/3 grad(div U) stress term (not implemented -- the forcing
// manufactures the implemented mu lap U operator), higher-order convection
// (the compressible momentum assembly is upwind-only), high Mach.
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

TEST(CompressibleSimpleMMS, SystemConvergence) {
  MMSStudy study;
  study.name = "compressible_simple_mms";
  study.description =
      "CompressibleSIMPLE MMS, isothermal ideal gas in a closed box, from rest; upwind convection "
      "(the only scheme of the compressible momentum assembly)";
  study.manufacturedSolution =
      "p = cos(pi x) cos(pi y) + x y / 2 (gauge); rho = (P_ref + p)/(R T0); rho U = (psi_y, "
      "-psi_x), "
      "psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi";
  study.coefficients = {{"reference_pressure", mc::kCompressibleReferencePressure},
                        {"gas_constant", mc::kGasConstant},
                        {"temperature", mc::kIsothermalTemperature},
                        {"viscosity", mc::kViscosity}};
  study.configuration = {
      {"solver", "cfd::compressible::CompressibleSIMPLE + setMomentumSource"},
      {"convection", "upwind"},
      {"relaxation",
       "velocity 0.7, pressure 0.3; pseudo time step 1 (CompressibleSIMPLE defaults)"},
      {"velocity_bc", "exact Dirichlet U = m/rho (Inlet) on every boundary face"},
      {"pressure_bc", "exact Neumann FixedGradient(grad p . n) per face"},
      {"pressure_level",
       "closed compressible box: the absolute level is a datum; reference cell 0 "
       "is pinned at its initial value = exact gauge pressure at its centroid "
       "(initial pressure uniform at that value, velocity at rest)"},
      {"forcing", "analytical f = (m.grad)U + grad p - mu lap U at cell centroids"},
      {"not_covered",
       "energy equation, mu/3 grad(div U) stress term, higher-order convection, "
       "high Mach"}};
  for (const Index n : std::initializer_list<Index>{8, 16, 32})
    study.levels.push_back(mc::runCompressibleLevel(n));
  for (const auto& level : study.levels)
    ASSERT_TRUE(level.accepted) << level.name << ": " << level.rejectionReason;

  // First-order class (upwind convection), the bands of the incompressible
  // SIMPLE upwind study (pre-asymptotic on 8/16/32).
  for (const std::string c : {"u", "v"}) {
    mc::addDecreaseGate(study, "compressible", c);
    mc::addOrderGate(study, "compressible", c, NormKind::L2, 1.0, 0.55, 1.3);
  }
  mc::addDecreaseGate(study, "compressible", "p");
  mc::addOrderGate(study, "compressible", "p", NormKind::L2, 1.0, 0.8, 1.6);
  // "p" carries the absolute level, set by ONE datum at the (corner)
  // reference cell -- it inherits that cell's local discretization error
  // (measured reduction factors 1.76, 1.67). The field shape, p modulo
  // gauge, converges like the incompressible upwind pressure (2.34, 2.09).
  mc::addDecreaseGate(study, "compressible", "p_gauge");
  mc::addOrderGate(study, "compressible", "p_gauge", NormKind::L2, 1.0, 0.8, 1.6);
  mc::addOrder(study, "density", NormKind::L2, 1.0);
  mc::addOrder(study, "mass_flux", NormKind::L2, 1.0);
  for (std::size_t k = 0; k < study.levels.size(); ++k) {
    const auto& level = study.levels[k];
    // EOS consistency: the converged density IS the EOS of the converged
    // pressure (to round-off), so the density error equals the pressure
    // error / (R T0) exactly (rho is linear in p).
    const Real eos = [&] {
      for (const auto& [name, value] : level.diagnostics) {
        if (name == "eos_max_relative_deviation") return value;
      }
      return 1.0;
    }();
    mc::addGate(study, "compressible: EOS consistency at " + level.name, eos <= 1e-14,
                mc::format("max |rho - EOS(p)|/rho = %.2e", eos));
    const Real rhoL2 = study.error(k, "density").l2;
    const Real pL2 = study.error(k, "p").l2;
    const Real rt = mc::kGasConstant * mc::kIsothermalTemperature;
    mc::addGate(study, "compressible: density error = pressure error / (R T0) at " + level.name,
                std::abs(rhoL2 - (pL2 / rt)) <= 1e-12 * std::max(1.0, pL2),
                mc::format("rho L2 %.4e", rhoL2) + mc::format(", p L2 / RT %.4e", pL2 / rt));
    const auto& c = study.error(k, "continuity");
    mc::addGate(
        study,
        "compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at " + level.name,
        c.linf <= 1e-5, mc::format("Linf %.2e", c.linf));
    mc::addGate(study, "compressible: global mass imbalance <= 1e-14 at " + level.name,
                level.massImbalance.value_or(1.0) <= 1e-14,
                mc::format("%.2e", level.massImbalance.value_or(1.0)));
  }
  mc::writeStudy(study, "compressible_simple_mms");
  std::string failures;
  EXPECT_TRUE(mc::categoryPassed(study, "compressible", &failures)) << failures;
}
