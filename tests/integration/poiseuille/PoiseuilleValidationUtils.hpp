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
#include "cfd/fields/SurfaceField.hpp"
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

// P12-NUM-007: checkerboard-immune axial pressure gradient. Collocated
// SIMPLE without Rhie-Chow interpolation lets an odd-even pressure mode
// a*(-1)^i grow on this open channel's residual plateau (measured on 64x8:
// amplitude 0.03 after 1000 iterations, 0.64 after 40000), which biases the
// two-column estimate above by a*2/(x2-x1) (7 % drift on 64x8). Averaging
// each ADJACENT PAIR of column-averaged pressures, P(i) = (pbar_i +
// pbar_{i+1})/2 located at the face x_{i+1/2}, cancels that mode exactly;
// the gradient is (P(i2) - P(i1)) / (x_{i2+1/2} - x_{i1+1/2}) with the
// faces nearest x1 and x2 -- identical to the mean central-difference
// gradient over columns i1+1..i2 (telescoping sum).
struct PairAveragedPressureGradient {
  cfd::Real gradient{0.0};
  cfd::Real drop{0.0};  // P(i2) - P(i1)
  cfd::Real x1Face{0.0};
  cfd::Real x2Face{0.0};
};
[[nodiscard]] PairAveragedPressureGradient pairAveragedPressureGradient(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::ScalarField& pressure, cfd::Real x1, cfd::Real x2);

// Amplitude a of the odd-even column mode over the columns whose centres lie
// in [x1, x2]: max |pbar_i - (pbar_{i-1} + pbar_{i+1})/2| / 2 (exactly |a|
// for pbar_i = linear + a*(-1)^i).
[[nodiscard]] cfd::Real pressureOddEvenAmplitude(const cfd::mesh::Mesh& mesh, cfd::Index nx,
                                                 cfd::Index ny,
                                                 const cfd::fields::ScalarField& pressure,
                                                 cfd::Real x1, cfd::Real x2);

// Net mass flow through the internal vertical face column nearest x (sum of
// the face mass fluxes, oriented +x).
[[nodiscard]] cfd::Real sectionMassFlow(const cfd::mesh::Mesh& mesh,
                                        const cfd::fields::SurfaceField& massFlux, cfd::Real x);

// The EXACT fully developed solution of this code's discretisation on ny
// uniform rows (central diffusion + the P12-DIFF-002 second-order one-sided
// wall-flux reconstruction).
//
// P12-DIFF-002-UC-001: DIFF-002 replaced the first-order wall gradient over
// the half cell dy/2 with the two-point one-sided reconstruction
//   cP = mu|S| h2/(h1(h2-h1)), cF = mu|S| h1/(h2(h2-h1)), cB = mu|S|(1/h1+1/h2),
//   h1 = dy/2, h2 = 3dy/2  =>  cP = 3mu|S|/dy, cF = mu|S|/(3dy), cB = 8mu|S|/(3dy),
// so the wall row became -4u_0 + (4/3)u_1 = G dy^2 (it was -3u_0 + u_1 = G dy^2).
// The solution of the new system is the continuum parabola EXACTLY at the cell
// centres, with only the flow-rate-consistent gradient scaled:
//   u_j = (G/2) y_j (H - y_j),  G = -(dp/dx)/mu,
//   dp/dx = -12 mu meanVelocity / H^2 * 2 ny^2 / (2 ny^2 + 1),
// i.e. a relative discretisation error of exactly 1/(2 ny^2 + 1) and a
// pressure-drop ratio of exactly 2 ny^2 / (2 ny^2 + 1). The superseded
// two-point form -- u_j with an extra +G dy^2/8 offset, dp/dx scaled by
// ny^2/(ny^2+2), relative error 2/(ny^2+2) -- is four times less accurate at
// the same ny; both are second order, so DIFF-002 changed the error constant,
// not the order. Full derivation, exact-rational cross-check and the
// machine-precision comparison against the assembled matrix:
// results/p12-diff-002/uc-001/acceptance_gate.md.
//
// Comparing against this AND the continuous solution separates the
// discretisation error (predicted exactly) from entrance, outlet and
// iterative effects.
//
// CAUTION when sampling the centreline. The tests read
// interpolateProfile(profile, H/2), not a cell value, and that sampling has a
// parity-dependent error constant:
//   1.5 U - u_c = 1.5/(2 ny^2 + 1)   (ny odd -- H/2 is a cell centre)
//                 4.5/(2 ny^2 + 1)   (ny even -- H/2 is the chord midpoint)
// exactly three times larger for even ny. A centreline grid-convergence
// triplet must therefore use ny values of a single parity, or the observed
// order is meaningless (the exact reference itself then reports p = 0.94).
[[nodiscard]] cfd::Real discretePressureGradient(cfd::Real dynamicViscosity, cfd::Real meanVelocity,
                                                 cfd::Real channelHeight, cfd::Index ny);
[[nodiscard]] cfd::Real discretePoiseuilleVelocity(cfd::Real y, cfd::Real channelHeight,
                                                   cfd::Real meanVelocity, cfd::Index ny);

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
