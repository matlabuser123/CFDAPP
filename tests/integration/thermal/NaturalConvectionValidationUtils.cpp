#include "NaturalConvectionValidationUtils.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace cfd::validation {

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

namespace {

// Same bracket()/lerp() convention as every other *ValidationUtils.cpp
// in this repository.
std::pair<Index, Index> bracket(const std::vector<Real>& sortedCoordinates, Real target) {
  const Index n = sortedCoordinates.size();
  if (n < 2) throw std::invalid_argument("bracket: need at least two coordinates");
  Index hi = 1;
  while (hi < n - 1 && sortedCoordinates[hi] < target) ++hi;
  return {hi - 1, hi};
}

Real lerp(Real x0, Real y0, Real x1, Real y1, Real x) {
  if (x1 == x0) return y0;
  const Real weight = (x - x0) / (x1 - x0);
  return y0 + weight * (y1 - y0);
}

std::vector<Real> columnCentersX(const Mesh& mesh, Index nx) {
  std::vector<Real> xCenters(nx);
  for (Index i = 0; i < nx; ++i) xCenters[i] = mesh.cell(i).centroid().x;
  return xCenters;
}

std::vector<Real> rowCentersY(const Mesh& mesh, Index nx, Index ny) {
  std::vector<Real> yCenters(ny);
  for (Index j = 0; j < ny; ++j) yCenters[j] = mesh.cell(j * nx).centroid().y;
  return yCenters;
}

void requireFinite(Real value, const char* name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string("NaturalConvectionValidationUtils: ") + name +
                                " must be finite");
  }
}

}  // namespace

DimensionlessNumbers computeDimensionlessNumbers(Real gravityMagnitude, Real beta, Real deltaT,
                                                 Real length, Real kinematicViscosity,
                                                 Real thermalDiffusivity) {
  requireFinite(gravityMagnitude, "gravityMagnitude");
  requireFinite(beta, "beta");
  requireFinite(deltaT, "deltaT");
  requireFinite(length, "length");
  if (!std::isfinite(kinematicViscosity) || !(kinematicViscosity > 0.0)) {
    throw std::invalid_argument(
        "computeDimensionlessNumbers: kinematicViscosity must be finite and > 0");
  }
  if (!std::isfinite(thermalDiffusivity) || !(thermalDiffusivity > 0.0)) {
    throw std::invalid_argument(
        "computeDimensionlessNumbers: thermalDiffusivity must be finite and > 0");
  }
  DimensionlessNumbers result;
  result.pr = kinematicViscosity / thermalDiffusivity;
  result.ra = gravityMagnitude * beta * deltaT * length * length * length /
              (kinematicViscosity * thermalDiffusivity);
  return result;
}

std::vector<ProfileSample> extractUProfileAtMidWidth(const Mesh& mesh, Index nx, Index ny,
                                                     const VectorField& velocity, Real xFixed,
                                                     Real height) {
  const auto xCenters = columnCentersX(mesh, nx);
  const auto [i0, i1] = bracket(xCenters, xFixed);

  std::vector<ProfileSample> profile;
  profile.reserve(ny + 2);
  profile.push_back({0.0, 0.0});  // bottom wall, no-slip.
  for (Index j = 0; j < ny; ++j) {
    const Index cell0 = (j * nx) + i0;
    const Index cell1 = (j * nx) + i1;
    const Real y = mesh.cell(cell0).centroid().y;
    const Real u = lerp(xCenters[i0], velocity[cell0].x, xCenters[i1], velocity[cell1].x, xFixed);
    profile.push_back({y, u});
  }
  profile.push_back({height, 0.0});  // top wall, no-slip.

  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
}

std::vector<ProfileSample> extractVProfileAtMidHeight(const Mesh& mesh, Index nx, Index ny,
                                                      const VectorField& velocity, Real yFixed,
                                                      Real width) {
  const auto yCenters = rowCentersY(mesh, nx, ny);
  const auto [j0, j1] = bracket(yCenters, yFixed);

  std::vector<ProfileSample> profile;
  profile.reserve(nx + 2);
  profile.push_back({0.0, 0.0});  // left wall, no-slip.
  for (Index i = 0; i < nx; ++i) {
    const Index cell0 = (j0 * nx) + i;
    const Index cell1 = (j1 * nx) + i;
    const Real x = mesh.cell(cell0).centroid().x;
    const Real v = lerp(yCenters[j0], velocity[cell0].y, yCenters[j1], velocity[cell1].y, yFixed);
    profile.push_back({x, v});
  }
  profile.push_back({width, 0.0});  // right wall, no-slip.

  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
}

Extremum findMax(const std::vector<ProfileSample>& profile) {
  if (profile.empty()) throw std::invalid_argument("findMax: profile must not be empty");
  Extremum result{profile.front().value, profile.front().coordinate};
  for (const auto& sample : profile) {
    if (sample.value > result.value) result = {sample.value, sample.coordinate};
  }
  return result;
}

Extremum findMin(const std::vector<ProfileSample>& profile) {
  if (profile.empty()) throw std::invalid_argument("findMin: profile must not be empty");
  Extremum result{profile.front().value, profile.front().coordinate};
  for (const auto& sample : profile) {
    if (sample.value < result.value) result = {sample.value, sample.coordinate};
  }
  return result;
}

std::vector<ProfileSample> computeLocalNusseltAtHotWall(const Mesh& mesh, Index nx, Index ny,
                                                        const ScalarField& temperature,
                                                        Real hotWallTemperature, Real length) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw std::invalid_argument(
        "computeLocalNusseltAtHotWall: temperature size does not match mesh cell count");
  }
  const Real wallAdjacentDistance = length / (2.0 * static_cast<Real>(nx));
  std::vector<ProfileSample> profile;
  profile.reserve(ny);
  for (Index j = 0; j < ny; ++j) {
    const Index cell = j * nx;  // column 0 (left/hot wall).
    const Real y = mesh.cell(cell).centroid().y;
    const Real nu = (hotWallTemperature - temperature[cell]) / wallAdjacentDistance;
    profile.push_back({y, nu});
  }
  return profile;
}

Real computeAverageNusselt(const std::vector<ProfileSample>& nusseltProfile) {
  if (nusseltProfile.empty()) {
    throw std::invalid_argument("computeAverageNusselt: nusseltProfile must not be empty");
  }
  Real sum = 0.0;
  for (const auto& sample : nusseltProfile) sum += sample.value;
  return sum / static_cast<Real>(nusseltProfile.size());
}

Real computeWallHeatFluxIntoFluid(const Mesh& mesh, Index nx, Index ny,
                                  const ScalarField& temperature, Index wallColumn,
                                  Real wallTemperature, Real length, Real height) {
  if (temperature.size() != mesh.numberOfCells()) {
    throw std::invalid_argument(
        "computeWallHeatFluxIntoFluid: temperature size does not match mesh cell count");
  }
  if (wallColumn >= nx) {
    throw std::invalid_argument("computeWallHeatFluxIntoFluid: wallColumn out of range");
  }
  const Real wallAdjacentDistance = length / (2.0 * static_cast<Real>(nx));
  const Real cellHeight = height / static_cast<Real>(ny);
  Real totalFlux = 0.0;
  for (Index j = 0; j < ny; ++j) {
    const Index cell = (j * nx) + wallColumn;
    // "Into fluid" convention: positive at the hot wall (wallTemperature
    // > firstCellTemperature there, heat flows from wall to fluid),
    // negative at the cold wall (wallTemperature < firstCellTemperature,
    // heat flows from fluid to wall) -- the same formula at both walls,
    // no separate sign-flip branch needed (see this function's own
    // header comment).
    const Real localFluxIntoFluid = (wallTemperature - temperature[cell]) / wallAdjacentDistance;
    totalFlux += localFluxIntoFluid * cellHeight;
  }
  return totalFlux;
}

Real relativeError(Real computed, Real reference) {
  if (reference == 0.0) {
    throw std::invalid_argument("relativeError: reference must not be exactly 0");
  }
  return std::abs(computed - reference) / std::abs(reference);
}

void writeProfileCsv(const std::string& path, const std::vector<std::tuple<Real, Real, Real>>& rows,
                     const std::string& coordinateName, const std::string& valueName) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeProfileCsv: could not open " + path);
  out << coordinateName << ",numerical_" << valueName << ",reference_" << valueName << "\n";
  for (const auto& [coordinate, numerical, reference] : rows) {
    out << coordinate << "," << numerical << "," << reference << "\n";
  }
}

void writeValidationJson(const std::string& path, const NaturalConvectionValidationRecord& record) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeValidationJson: could not open " + path);
  out << "{\n"
      << "  \"case\": \"natural_convection_cavity\",\n"
      << "  \"ra\": " << record.ra << ",\n"
      << "  \"pr\": " << record.pr << ",\n"
      << "  \"mesh\": { \"nx\": " << record.nx << ", \"ny\": " << record.ny << " },\n"
      << "  \"solver\": {\n"
      << "    \"flow_converged\": " << (record.flowConverged ? "true" : "false") << ",\n"
      << "    \"flow_iterations\": " << record.flowIterations << ",\n"
      << "    \"thermal_converged\": " << (record.thermalConverged ? "true" : "false") << ",\n"
      << "    \"thermal_iterations\": " << record.thermalIterations << ",\n"
      << "    \"outer_iterations\": " << record.outerIterations << ",\n"
      << "    \"final_outer_temperature_change\": " << record.finalOuterTemperatureChange << ",\n"
      << "    \"runtime_seconds\": " << record.runtimeSeconds << "\n"
      << "  },\n"
      << "  \"conservation\": {\n"
      << "    \"global_mass_imbalance\": " << record.globalMassImbalance << ",\n"
      << "    \"max_wall_normal_flux\": " << record.maxWallNormalFlux << ",\n"
      << "    \"q_hot\": " << record.qHot << ",\n"
      << "    \"q_cold\": " << record.qCold << ",\n"
      << "    \"heat_imbalance\": " << record.heatImbalance << "\n"
      << "  },\n"
      << "  \"validation\": {\n"
      << "    \"nu_avg_computed\": " << record.nuAvgComputed << ",\n"
      << "    \"nu_avg_reference\": " << record.nuAvgReference << ",\n"
      << "    \"nu_avg_error\": " << record.nuAvgError << ",\n"
      << "    \"u_max_computed\": " << record.uMaxComputed << ",\n"
      << "    \"u_max_computed_y\": " << record.uMaxComputedY << ",\n"
      << "    \"u_max_reference\": " << record.uMaxReference << ",\n"
      << "    \"u_max_error\": " << record.uMaxError << ",\n"
      << "    \"v_max_computed\": " << record.vMaxComputed << ",\n"
      << "    \"v_max_computed_x\": " << record.vMaxComputedX << ",\n"
      << "    \"v_max_reference\": " << record.vMaxReference << ",\n"
      << "    \"v_max_error\": " << record.vMaxError << ",\n"
      << "    \"min_theta\": " << record.minTheta << ",\n"
      << "    \"max_theta\": " << record.maxTheta << "\n"
      << "  },\n"
      << "  \"deterministic\": " << (record.deterministic ? "true" : "false") << "\n"
      << "}\n";
}

}  // namespace cfd::validation
