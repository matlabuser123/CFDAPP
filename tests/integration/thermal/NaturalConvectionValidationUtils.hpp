#pragma once

// Validation-only helpers for the P3-PHYS-002 differentially-heated-
// cavity benchmark: dimensionless-number calculation, velocity-profile
// extraction, local/average Nusselt number, wall heat flux/heat-balance,
// error metrics, and CSV/JSON evidence output. Deliberately independent
// of the solver and of every other *ValidationUtils.hpp in this repo
// (same "self-contained per validation case" convention those already
// use) -- not part of cfdcore, compiled only into the
// CFDNaturalConvectionValidationTests binary.
//
// Nondimensionalization convention (must match
// validation/literature/natural_convection/README.md exactly): this
// case is always run with rho=1, cp=1, k=1 (so alpha=1), nu=Pr,
// L=Lx=Ly=1, T_hot=1, T_cold=0 (so deltaT=1), gravity magnitude=1 (so
// beta=Ra*Pr gives the requested Ra exactly) -- chosen specifically so
// the solver's raw dimensional output already *is* the nondimensional
// quantity the de Vahl Davis (1983) benchmark reports (U=u*L/alpha=u
// since L=alpha=1; theta=(T-Tc)/(Th-Tc)=T since Tc=0, deltaT=1). Every
// function below assumes this convention; none of them do unit
// conversion themselves.

#include <string>
#include <tuple>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::validation {

struct DimensionlessNumbers {
  cfd::Real ra;
  cfd::Real pr;
};

// Ra = g*beta*deltaT*L^3/(nu*alpha), Pr = nu/alpha -- the single
// canonical calculation this task's own section 4 requires (not hand-
// derived independently in multiple tests/scripts). Throws
// std::invalid_argument if nu or alpha is not finite and > 0, or if any
// other input is not finite.
[[nodiscard]] DimensionlessNumbers computeDimensionlessNumbers(
    cfd::Real gravityMagnitude, cfd::Real beta, cfd::Real deltaT, cfd::Real length,
    cfd::Real kinematicViscosity, cfd::Real thermalDiffusivity);

struct ProfileSample {
  cfd::Real coordinate;
  cfd::Real value;
};

// u(x=xFixed, y), the vertical mid-width profile de Vahl Davis's own
// u_max table entry is defined against -- linearly interpolated in x
// between the two straddling columns, plus exact no-slip anchors u=0 at
// both walls (y=0, y=height): same "wall BC is exact, not extrapolated"
// convention as PoiseuilleValidationUtils::extractVerticalProfileU.
[[nodiscard]] std::vector<ProfileSample> extractUProfileAtMidWidth(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real xFixed, cfd::Real height);

// v(x, y=yFixed), the horizontal mid-height profile de Vahl Davis's own
// v_max table entry is defined against -- same interpolation/anchor
// convention as extractUProfileAtMidWidth, but varying x with exact
// no-slip anchors v=0 at both walls (x=0, x=width).
[[nodiscard]] std::vector<ProfileSample> extractVProfileAtMidHeight(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real yFixed, cfd::Real width);

struct Extremum {
  cfd::Real value;
  cfd::Real coordinate;
};

// The maximum (signed) value in `profile` and its coordinate -- matches
// the benchmark table's own "u_max (y)"/"v_max (x)" convention (the
// reported peak is a positive value at an off-center location, not the
// largest-magnitude value regardless of sign). Throws
// std::invalid_argument if profile is empty.
[[nodiscard]] Extremum findMax(const std::vector<ProfileSample>& profile);
[[nodiscard]] Extremum findMin(const std::vector<ProfileSample>& profile);

// Local hot-wall Nusselt number Nu(y) = (T_hotWall - T_firstCell(y)) /
// wallAdjacentCellDistance (nondimensional form, valid under this file's
// own documented unit convention: L=deltaT=1) -- one row per wall-
// adjacent cell, sorted by y. Same one-sided-secant estimator
// convention (and the same known near-wall-resolution bias, improving
// with refinement) as
// tests/integration/turbulence_channel/ChannelFlowValidationUtils.hpp's
// own computeWallShearStress -- documented at this function's call site
// in the actual benchmark test, not repeated here. Throws
// std::invalid_argument if temperature.size() != mesh.numberOfCells().
[[nodiscard]] std::vector<ProfileSample> computeLocalNusseltAtHotWall(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::ScalarField& temperature, cfd::Real hotWallTemperature, cfd::Real length);

// Arithmetic mean of `nusseltProfile`'s own values -- valid as the
// integral-over-the-wall average specifically because the mesh is
// uniform (every wall-adjacent cell has the same height), so a plain
// mean is exactly the length-weighted average the definition
// (Nu_avg = (1/L) * integral Nu_local ds) calls for. Throws
// std::invalid_argument if nusseltProfile is empty.
[[nodiscard]] cfd::Real computeAverageNusselt(const std::vector<ProfileSample>& nusseltProfile);

// Total heat flux *into* the fluid domain across one vertical wall
// (positive at the hot wall, negative at the cold wall, by construction
// of the shared "wallTemperature - firstCellTemperature" formula --
// see this function's own use in the heat-balance check). `wallColumn`
// is 0 for the left (hot) wall or nx-1 for the right (cold) wall.
// Throws std::invalid_argument if temperature.size() !=
// mesh.numberOfCells() or wallColumn is out of range.
[[nodiscard]] cfd::Real computeWallHeatFluxIntoFluid(const cfd::mesh::Mesh& mesh, cfd::Index nx,
                                                     cfd::Index ny,
                                                     const cfd::fields::ScalarField& temperature,
                                                     cfd::Index wallColumn, cfd::Real wallTemperature,
                                                     cfd::Real length, cfd::Real height);

// abs(computed - reference) / abs(reference). Throws std::invalid_argument
// if reference is exactly 0.
[[nodiscard]] cfd::Real relativeError(cfd::Real computed, cfd::Real reference);

// Writes coordinate,numerical[,reference] rows to `path` (reference
// omitted -- empty column -- when `rows` carries only the numerical
// value, e.g. a raw temperature/velocity profile with no closed-form
// reference).
void writeProfileCsv(const std::string& path,
                     const std::vector<std::tuple<cfd::Real, cfd::Real, cfd::Real>>& rows,
                     const std::string& coordinateName, const std::string& valueName);

// Fresh-evidence JSON record for one (Ra, grid) run.
struct NaturalConvectionValidationRecord {
  cfd::Real ra{0.0};
  cfd::Real pr{0.0};
  cfd::Index nx{0};
  cfd::Index ny{0};

  bool flowConverged{false};
  cfd::Index flowIterations{0};
  bool thermalConverged{false};
  cfd::Index thermalIterations{0};
  cfd::Index outerIterations{0};
  cfd::Real finalOuterTemperatureChange{0.0};

  cfd::Real globalMassImbalance{0.0};
  cfd::Real maxWallNormalFlux{0.0};

  cfd::Real qHot{0.0};
  cfd::Real qCold{0.0};
  cfd::Real heatImbalance{0.0};

  cfd::Real nuAvgComputed{0.0};
  cfd::Real nuAvgReference{0.0};
  cfd::Real nuAvgError{0.0};

  cfd::Real uMaxComputed{0.0};
  cfd::Real uMaxComputedY{0.0};
  cfd::Real uMaxReference{0.0};
  cfd::Real uMaxError{0.0};

  cfd::Real vMaxComputed{0.0};
  cfd::Real vMaxComputedX{0.0};
  cfd::Real vMaxReference{0.0};
  cfd::Real vMaxError{0.0};

  cfd::Real minTheta{0.0};
  cfd::Real maxTheta{0.0};

  bool deterministic{false};
  double runtimeSeconds{0.0};
};

void writeValidationJson(const std::string& path, const NaturalConvectionValidationRecord& record);

}  // namespace cfd::validation
