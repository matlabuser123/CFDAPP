#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

// P16-TWF -- the thermal wall function (Launder-Spalding / Jayatilleke).
//
// WHY THIS EXISTS
// ---------------
// The momentum side has a wall function: for a cell whose centre sits in the
// log layer, the wall SHEAR is taken from the log law rather than from the
// raw cell-centre velocity gradient, because that gradient badly
// under-resolves du/dy across such a cell. The thermal side had no
// counterpart, so the wall HEAT FLUX was the raw cell-centre gradient with
// k_eff -- the exact error the momentum wall function exists to remove.
//
// Measured consequence, on cases/turbulent_heated_channel at y+ ~ 180:
// Nu_CFD = 980.7 against Gnielinski's 228.4, a 4.295x over-prediction. See
// results/p16/turbulence/eddy_viscosity/benchmark.md, where that failure was
// PREDICTED before it was run, with this as the predicted cause.
//
// THE CONVENTION, and it matters
// ------------------------------
//                  [  1                 ]
//     T+  =  Pr_t  [ --- ln(E y+) + P   ]      log region
//                  [ kappa              ]
//
//     T+  =  Pr y+                             conduction sublayer
//
// P is INSIDE the Pr_t bracket. The alternative reading
// `Pr_t ln(E y+)/kappa + P` differs by a factor of Pr_t on the P term and is
// NOT what Launder-Spalding derives or what established implementations use.
// Frozen in results/p16/turbulence/eddy_viscosity/thermal_wall_function.md
// before any of this was written.
//
// kappa and E come from the SAME WallFunctionCoefficients the momentum wall
// function uses, so both treatments describe one wall rather than two
// conventions.
namespace cfd::thermal {

// Jayatilleke's P-function -- the extra thermal resistance of the conduction
// sublayer relative to the momentum sublayer:
//
//   P = 9.24 [ (Pr/Pr_t)^0.75 - 1 ] [ 1 + 0.28 exp(-0.007 Pr/Pr_t) ]
//
// The four constants are Jayatilleke's and are not adjustable.
//
// P is NEGATIVE whenever Pr < Pr_t, which is the ordinary case for air
// (Pr = 0.704 against Pr_t = 0.85 gives P = -1.559). A formulation that
// clamped P to be non-negative would be wrong, so nothing here clamps it.
//
// Throws InvalidArgumentError if either Prandtl number is not finite and > 0.
[[nodiscard]] Real jayatillekeP(Real prandtl, Real turbulentPrandtl);

// The dimensionless temperature T+ at a given y+.
//
// Below the crossing point the conduction branch Pr y+ applies; above it, the
// log branch. The two meet by construction at thermalSublayerThickness below,
// so this function is continuous there.
//
// Throws InvalidArgumentError if yPlus is not finite and >= 0, or either
// Prandtl number is not finite and > 0.
[[nodiscard]] Real temperaturePlus(Real yPlus, Real prandtl, Real turbulentPrandtl,
                                   const cfd::turbulence::WallFunctionCoefficients& coefficients);

// y+_T, where Pr y+ = Pr_t [ln(E y+)/kappa + P].
//
// Found by bisection rather than by a fitted correlation, so it is exact for
// whatever Pr/Pr_t a case actually has. The bracket is rigorous: the
// conduction branch starts below the log branch at small y+ and overtakes it,
// and both are continuous and monotonic in between.
[[nodiscard]] Real thermalSublayerThickness(
    Real prandtl, Real turbulentPrandtl,
    const cfd::turbulence::WallFunctionCoefficients& coefficients);

// The wall heat-transfer conductance
//
//   G = rho cp u_tau / T+        [W / m^2 / K]
//
// so that the flux into the owner cell is G A (T_w - T_P). This is the form
// the energy assembly consumes: a per-face conductance, parallel to the
// per-face wall viscosity the momentum wall function already supplies.
//
// u_tau is the friction velocity from the turbulence model's OWN wall shear,
// so the thermal law is driven by the same wall state the momentum law
// produced rather than by a second estimate of it.
//
// u_tau = 0 (a stagnation or separation point) returns exactly 0: no friction
// velocity means no turbulent wall transport, and the molecular path is what
// should carry the heat there. The caller decides what to do with that.
//
// Throws InvalidArgumentError if density, specificHeat, or either Prandtl
// number is not finite and > 0, or if frictionVelocity or yPlus is not finite
// and >= 0.
[[nodiscard]] Real wallHeatTransferConductance(
    Real density, Real specificHeat, Real frictionVelocity, Real yPlus, Real prandtl,
    Real turbulentPrandtl, const cfd::turbulence::WallFunctionCoefficients& coefficients);

// ONE evaluation of the wall law for one face, feeding BOTH the matrix and the
// diagnostics.
//
// This exists because of a concrete failure earlier in P16: the conservation
// balance reconstructed the wall heat flux with an "equivalent-looking" formula
// (the two-point secant) instead of the operator the solve actually used, and
// under-computed it by 13% -- making a provably conservative scheme look
// non-conservative. The fix is structural, not a comment: there is exactly one
// place that turns wall state into wall heat, and everything downstream reads
// its output.
//
//   T+   = f(y+, Pr, Pr_t)
//   h_w  = rho cp u_tau / T+
//   G_w  = h_w A_f
//   q_w  = G_w (T_w - T_P)
struct ThermalWallFaceResult {
  Real yPlus{};
  Real temperaturePlus{};
  Real heatTransferCoefficient{};  // h_w
  Real conductance{};              // G_w = h_w A_f -- what the matrix consumes
  Real heatFlux{};                 // q_w = G_w (T_w - T_P) -- what diagnostics consume
};

// u_tau = 0 or y+ = 0 gives an all-zero result, which the assembly reads as
// "leave this face on the molecular path".
//
// Throws InvalidArgumentError if density, specificHeat, wallArea or either
// Prandtl number is not finite and > 0, or frictionVelocity/yPlus is not
// finite and >= 0.
[[nodiscard]] ThermalWallFaceResult computeThermalWallFace(
    Real density, Real specificHeat, Real frictionVelocity, Real yPlus, Real wallArea,
    Real cellTemperature, Real wallTemperature, Real molecularPrandtl, Real turbulentPrandtl,
    const cfd::turbulence::WallFunctionCoefficients& coefficients);

}  // namespace cfd::thermal
