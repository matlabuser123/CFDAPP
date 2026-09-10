#pragma once

// Validation-only helpers for the P2-TURB-007 turbulent plane-channel
// benchmark: velocity-profile extraction, wall-shear/wall-units
// computation, law-of-the-wall comparison, development (fully-developed)
// checks, error metrics, and CSV/JSON evidence output. Deliberately
// independent of the solver and of PoiseuilleValidationUtils/
// CavityValidationUtils (same "self-contained per validation case"
// convention those two already use) -- not part of cfdcore, compiled
// only into the CFDTurbulenceChannelValidationTests binary.

#include <string>
#include <tuple>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::validation {

struct ProfileSample {
  cfd::Real coordinate;
  cfd::Real value;
};

// u(xFixed, y) for a structured createCartesian2D(nx, ny, ...) channel
// mesh (cell id = j*nx + i), linearly interpolated in x between the two
// columns straddling xFixed, plus exact no-slip anchors u=0 at both
// walls (y=0 and y=channelHeight) -- same convention as
// PoiseuilleValidationUtils::extractVerticalProfileU.
[[nodiscard]] std::vector<ProfileSample> extractVerticalProfileU(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real xFixed, cfd::Real channelHeight);

// scalar(xFixed, y) for any cell-centered ScalarField (k, epsilon,
// omega, mu_t, F1, F2), linearly interpolated in x the same way as
// extractVerticalProfileU, but *without* a wall-value anchor -- unlike
// velocity, most turbulence scalars are not exactly zero (or even
// finite in a physically meaningful closed form) at y=0, so this
// reports the profile only at actual cell-center locations, sorted by
// y. Callers that know a specific field's exact wall value (e.g. k=0)
// may prepend/append it themselves.
[[nodiscard]] std::vector<ProfileSample> extractVerticalProfileScalar(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::ScalarField& field, cfd::Real xFixed);

// Linearly interpolates `profile` (sorted by coordinate) at `coordinate`.
[[nodiscard]] cfd::Real interpolateProfile(const std::vector<ProfileSample>& profile,
                                           cfd::Real coordinate);

// Wall shear stress, computed as the standard first-order one-sided
// estimate tau_w = mu * U(y1) / y1, where y1 is the distance from the
// wall to the nearest cell-row centroid (channelHeight / (2*ny) for a
// uniform mesh) and U(y1) is extractVerticalProfileU's own linearly
// interpolated near-wall value at xFixed. Uses *molecular* viscosity
// only, never mu_eff/mu_t -- this is the physically exact definition of
// wall shear (true tau_w = mu*dU/dy|wall always; the turbulent stress
// contribution vanishes at a no-slip wall because none of KEpsilonModel/
// KOmegaModel/SSTModel's own wall boundary conditions let mu_t be
// nonzero exactly at y=0). This function does **not** attempt to
// reconstruct a true wall-tangent gradient through a wall-function
// log-law correction -- CFDApp's turbulence models have no velocity
// wall function (see BoundaryConditionType::Wall's own no-slip
// treatment, unchanged since P0), so the one-sided secant from the wall
// to the first cell center is the only self-consistent estimator
// available. It is only strictly accurate when that first cell lies
// within (or very near) the viscous sublayer (y+ < ~5); on a coarser
// mesh where the first cell sits in the buffer or log layer, this
// systematically *underestimates* the true wall gradient (the real
// near-wall profile is concave in that region, steeper right at the
// wall than the secant to a point further out implies) -- see this
// benchmark's own "near-wall limitation" documentation in the P2-TURB-007
// Final Report and TODO.md status note. Throws std::invalid_argument if
// ny is zero.
[[nodiscard]] cfd::Real computeWallShearStress(const cfd::mesh::Mesh& mesh, cfd::Index nx,
                                               cfd::Index ny,
                                               const cfd::fields::VectorField& velocity,
                                               cfd::Real xFixed, cfd::Real channelHeight,
                                               cfd::Real dynamicViscosity, bool bottomWall);

// u_tau = sqrt(|tau_w| / rho). Throws std::invalid_argument if density
// is not finite and > 0.
[[nodiscard]] cfd::Real computeFrictionVelocity(cfd::Real wallShearStress, cfd::Real density);

// y+ = y * u_tau / nu, nu = mu/rho.
[[nodiscard]] cfd::Real computeYPlus(cfd::Real y, cfd::Real frictionVelocity,
                                     cfd::Real kinematicViscosity);

// u+ = U / u_tau.
[[nodiscard]] cfd::Real computeUPlus(cfd::Real velocity, cfd::Real frictionVelocity);

// Achieved Re_tau = u_tau * delta / nu, delta = channelHeight/2 -- read
// off the converged numerical solution, never assumed equal to the
// benchmark's target input Reynolds number (P2-TURB-007 section 5).
[[nodiscard]] cfd::Real computeAchievedReTau(cfd::Real frictionVelocity, cfd::Real channelHeight,
                                             cfd::Real kinematicViscosity);

// Analytical law-of-the-wall value at a given y+: viscous sublayer
// (u+ = y+) for y+ < viscousSublayerMaxYPlus, log law
// (u+ = (1/kappa)*ln(y+) + B) for y+ > bufferLayerMaxYPlus, and the
// linear-in-log-space blend of the two bracketing y+ boundary values
// for the buffer layer in between (neither closed form is valid there;
// this is a documented visual/interpolation convenience only, not a
// physical law -- see ChannelReTau180.hpp's own comment). Throws
// std::invalid_argument if yPlus is not finite and > 0.
[[nodiscard]] cfd::Real lawOfTheWallUPlus(cfd::Real yPlus, cfd::Real kappa, cfd::Real additiveB,
                                          cfd::Real viscousSublayerMaxYPlus,
                                          cfd::Real bufferLayerMaxYPlus);

struct ErrorMetrics {
  cfd::Real l2{0.0};
  cfd::Real lInf{0.0};
  cfd::Real meanAbsolute{0.0};
  cfd::Index sampleCount{0};
};

// Region-based (near-wall / buffer / outer) u+ vs y+ error against
// lawOfTheWallUPlus, evaluated at each of `wallUnitsProfile`'s own y+
// coordinates (no external interpolation grid needed, same "closed-form
// reference, compare at the profile's own coordinates" precedent as
// PoiseuilleValidationUtils::computeVelocityProfileErrors). Region
// boundaries: near-wall y+ < viscousSublayerMaxYPlus, buffer
// viscousSublayerMaxYPlus <= y+ <= bufferLayerMaxYPlus, outer/log
// y+ > bufferLayerMaxYPlus. Any region with zero samples in it reports
// sampleCount=0 and all-zero errors (not a fabricated "perfect match").
struct RegionErrorMetrics {
  ErrorMetrics overall;
  ErrorMetrics nearWall;
  ErrorMetrics buffer;
  ErrorMetrics outer;
};

[[nodiscard]] RegionErrorMetrics computeWallUnitsErrors(
    const std::vector<ProfileSample>& wallUnitsProfile,  // coordinate=y+, value=u+.
    cfd::Real kappa, cfd::Real additiveB, cfd::Real viscousSublayerMaxYPlus,
    cfd::Real bufferLayerMaxYPlus, const std::string& csvPath = {});

// L2 relative difference between two same-length velocity profiles
// sampled at different downstream stations, used to prove the profile
// has stopped evolving (P2-TURB-007 section 26) before it is used as
// the benchmark comparison station.
[[nodiscard]] cfd::Real developmentRelativeDifference(const std::vector<ProfileSample>& profileA,
                                                      const std::vector<ProfileSample>& profileB);

// Writes coordinate,numerical[,analytical] rows to `path`. analytical
// values are omitted (empty column) when `rows` carries only the
// numerical value (turbulence-scalar profiles with no closed-form
// reference).
void writeProfileCsv(const std::string& path,
                     const std::vector<std::tuple<cfd::Real, cfd::Real, cfd::Real>>& rows,
                     const std::string& coordinateName, const std::string& valueName);

// Fresh-evidence JSON record for one (model, grid) run -- mirrors
// PoiseuilleValidationRecord's shape, extended with turbulence-specific
// quantities (P2-TURB-007 section 37).
struct ChannelFlowValidationRecord {
  std::string model;
  cfd::Index nx{0};
  cfd::Index ny{0};
  cfd::Real channelLength{0.0};
  cfd::Real channelHeight{0.0};
  cfd::Real reynoldsNumberBulk{0.0};

  bool converged{false};
  cfd::Index iterations{0};
  cfd::Real finalUResidual{0.0};
  cfd::Real finalVResidual{0.0};
  cfd::Real finalPressureResidual{0.0};
  cfd::Real finalContinuityResidual{0.0};
  bool hasTurbulenceResidual{false};
  cfd::Real finalTurbulenceResidual{0.0};
  cfd::Real globalMassImbalance{0.0};
  cfd::Real inletFlux{0.0};
  cfd::Real outletFlux{0.0};
  bool finite{false};

  cfd::Real developmentRelativeDiff{0.0};

  cfd::Real wallShearBottom{0.0};
  cfd::Real wallShearTop{0.0};
  cfd::Real wallShearAsymmetry{0.0};  // relative |bottom-top| difference, a symmetry sanity check.
  cfd::Real frictionVelocity{0.0};
  cfd::Real firstCellYPlus{0.0};
  cfd::Real achievedReTau{0.0};
  cfd::Real reTauTarget{0.0};
  cfd::Real reTauRelativeError{0.0};

  RegionErrorMetrics wallUnitsError;

  cfd::Real minK{0.0};
  cfd::Real maxK{0.0};
  bool hasSecondScalar{false};  // epsilon (kepsilon) or omega (komega/sst).
  cfd::Real minSecondScalar{0.0};
  cfd::Real maxSecondScalar{0.0};
  cfd::Real minMuT{0.0};
  cfd::Real maxMuT{0.0};
  cfd::Real molecularViscosity{0.0};

  bool hasF1F2{false};  // SST only.
  cfd::Real minF1{0.0};
  cfd::Real maxF1{0.0};
  cfd::Real minF2{0.0};
  cfd::Real maxF2{0.0};
  cfd::Real minWallDistance{0.0};
  cfd::Real maxWallDistance{0.0};

  bool deterministic{false};
  double runtimeSeconds{0.0};
};

void writeValidationJson(const std::string& path, const ChannelFlowValidationRecord& record);

}  // namespace cfd::validation
