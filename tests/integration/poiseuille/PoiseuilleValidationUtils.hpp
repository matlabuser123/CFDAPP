#pragma once

// Validation-only helpers: velocity-profile extraction, the analytical
// planar-Poiseuille solution, axial pressure-gradient extraction, error
// metrics, and CSV/JSON evidence output for the inlet/outlet channel
// benchmark (TODO.md "P0 -- Physical Validation" > "Poiseuille Flow").
//
// Deliberately independent of the solver, same convention as
// tests/integration/cavity/CavityValidationUtils.hpp: this code only
// reads a SIMPLEResult/Mesh after a solve has already happened and never
// participates in solving. Not part of cfdcore -- compiled only into the
// validation test binary.

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
// mesh (cell id = j*nx + i, row-major, same convention as
// CavityValidationUtils), linearly interpolated in x between the two
// columns straddling xFixed, plus exact no-slip anchors u=0 at y=0 and
// y=channelHeight (both walls) -- so interpolation against the analytical
// profile's y=0/y=H endpoints uses the exact boundary condition, not an
// extrapolated cell value.
[[nodiscard]] std::vector<ProfileSample> extractVerticalProfileU(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real xFixed, cfd::Real channelHeight);

// Column-averaged pressure (mean over the ny cells sharing a given x
// column) at xFixed, linearly interpolated between the two straddling
// columns. Fully-developed planar Poiseuille flow has dp/dy = 0, so
// column-averaging is a robust way to read p(x) off a field that is only
// exactly uniform in y in the limit of a fully-developed, well-resolved
// solution.
[[nodiscard]] cfd::Real columnAveragedPressure(const cfd::mesh::Mesh& mesh, cfd::Index nx,
                                               cfd::Index ny,
                                               const cfd::fields::ScalarField& pressure,
                                               cfd::Real xFixed);

// (p(x2) - p(x1)) / (x2 - x1), via columnAveragedPressure. x1 and x2
// should both sit in the fully-developed region, away from the entrance
// and outlet-boundary influence.
[[nodiscard]] cfd::Real numericalPressureGradient(const cfd::mesh::Mesh& mesh, cfd::Index nx,
                                                  cfd::Index ny,
                                                  const cfd::fields::ScalarField& pressure,
                                                  cfd::Real x1, cfd::Real x2);

// Linearly interpolates `profile` (sorted by coordinate) at `coordinate`.
[[nodiscard]] cfd::Real interpolateProfile(const std::vector<ProfileSample>& profile,
                                           cfd::Real coordinate);

// Analytical fully-developed planar Poiseuille profile, parameterized by
// the bulk (mean) velocity rather than the pressure gradient directly --
// u(y) = 6*meanVelocity*(y/H)*(1 - y/H), which integrates to exactly
// meanVelocity over [0, H] and is what a uniform-velocity inlet's mass
// flow rate implies the fully-developed profile must average to, by
// conservation of mass in an incompressible straight channel.
[[nodiscard]] cfd::Real analyticalPoiseuilleVelocity(cfd::Real y, cfd::Real channelHeight,
                                                     cfd::Real meanVelocity);

// Analytical fully-developed pressure gradient dp/dx = -12*mu*meanVelocity
// / H^2, derived from the same profile (standard planar-Poiseuille
// result: mean velocity = H^2/(12*mu) * (-dp/dx)).
[[nodiscard]] cfd::Real analyticalPressureGradient(cfd::Real dynamicViscosity,
                                                   cfd::Real meanVelocity, cfd::Real channelHeight);

struct ErrorMetrics {
  cfd::Real l2{0.0};
  cfd::Real lInf{0.0};
  cfd::Real meanAbsolute{0.0};
};

// Writes coordinate,numerical,analytical rows to `path`.
void writeProfileCsv(const std::string& path,
                     const std::vector<std::tuple<cfd::Real, cfd::Real, cfd::Real>>& rows,
                     const std::string& coordinateName, const std::string& valueName);

// Compares `profile` against the analytical Poiseuille profile at each of
// the profile's own coordinates (no external reference table to
// interpolate to, unlike the Ghia comparison -- the analytical solution
// is a closed form evaluable anywhere). If `csvPath` is non-empty, also
// writes the coordinate/numerical/analytical rows used for the
// comparison.
[[nodiscard]] ErrorMetrics computeVelocityProfileErrors(const std::vector<ProfileSample>& profile,
                                                        cfd::Real channelHeight,
                                                        cfd::Real meanVelocity,
                                                        const std::string& csvPath = {});

// Fresh-evidence JSON record for one grid.
struct PoiseuilleValidationRecord {
  cfd::Index nx{0};
  cfd::Index ny{0};
  cfd::Real channelLength{0.0};
  cfd::Real channelHeight{0.0};
  cfd::Real reynoldsNumber{0.0};
  bool converged{false};
  cfd::Index iterations{0};
  cfd::Real finalUResidual{0.0};
  cfd::Real finalVResidual{0.0};
  cfd::Real finalPressureResidual{0.0};
  cfd::Real finalContinuityResidual{0.0};
  cfd::Real globalMassImbalance{0.0};
  cfd::Real maxWallNormalFlux{0.0};
  cfd::Real inletFlux{0.0};
  cfd::Real outletFlux{0.0};
  bool finite{false};
  ErrorMetrics velocityError;
  cfd::Real pressureGradientNumerical{0.0};
  cfd::Real pressureGradientAnalytical{0.0};
  cfd::Real pressureGradientRelativeError{0.0};
};

void writeValidationJson(const std::string& path, const PoiseuilleValidationRecord& record);

}  // namespace cfd::validation
