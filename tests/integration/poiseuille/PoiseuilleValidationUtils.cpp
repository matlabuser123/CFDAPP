#include "PoiseuilleValidationUtils.hpp"

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

// Locates the pair of indices in a sorted, uniformly-spaced coordinate
// array straddling `target`, clamped to the domain's interior interval so
// callers never extrapolate past the first/last sample. Same convention
// as CavityValidationUtils.cpp's private bracket().
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

}  // namespace

std::vector<ProfileSample> extractVerticalProfileU(const Mesh& mesh, Index nx, Index ny,
                                                   const VectorField& velocity, Real xFixed,
                                                   Real channelHeight) {
  std::vector<Real> xCenters(nx);
  for (Index i = 0; i < nx; ++i) xCenters[i] = mesh.cell(i).centroid().x;
  const auto [i0, i1] = bracket(xCenters, xFixed);

  std::vector<ProfileSample> profile;
  profile.reserve(ny + 2);
  profile.push_back({0.0, 0.0});  // bottom wall, no-slip: u = 0.
  for (Index j = 0; j < ny; ++j) {
    const Index cell0 = (j * nx) + i0;
    const Index cell1 = (j * nx) + i1;
    const Real y = mesh.cell(cell0).centroid().y;
    const Real u = lerp(xCenters[i0], velocity[cell0].x, xCenters[i1], velocity[cell1].x, xFixed);
    profile.push_back({y, u});
  }
  profile.push_back({channelHeight, 0.0});  // top wall, no-slip: u = 0.

  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
}

Real columnAveragedPressure(const Mesh& mesh, Index nx, Index ny, const ScalarField& pressure,
                            Real xFixed) {
  std::vector<Real> xCenters(nx);
  for (Index i = 0; i < nx; ++i) xCenters[i] = mesh.cell(i).centroid().x;
  const auto [i0, i1] = bracket(xCenters, xFixed);

  Real sum0 = 0.0;
  Real sum1 = 0.0;
  for (Index j = 0; j < ny; ++j) {
    sum0 += pressure[(j * nx) + i0];
    sum1 += pressure[(j * nx) + i1];
  }
  const Real avg0 = sum0 / static_cast<Real>(ny);
  const Real avg1 = sum1 / static_cast<Real>(ny);
  return lerp(xCenters[i0], avg0, xCenters[i1], avg1, xFixed);
}

Real numericalPressureGradient(const Mesh& mesh, Index nx, Index ny, const ScalarField& pressure,
                               Real x1, Real x2) {
  if (x2 == x1) throw std::invalid_argument("numericalPressureGradient: x1 and x2 must differ");
  const Real p1 = columnAveragedPressure(mesh, nx, ny, pressure, x1);
  const Real p2 = columnAveragedPressure(mesh, nx, ny, pressure, x2);
  return (p2 - p1) / (x2 - x1);
}

Real interpolateProfile(const std::vector<ProfileSample>& profile, Real coordinate) {
  if (profile.size() < 2)
    throw std::invalid_argument("interpolateProfile: need at least two samples");
  std::vector<Real> coordinates(profile.size());
  for (std::size_t k = 0; k < profile.size(); ++k) coordinates[k] = profile[k].coordinate;
  const auto [i0, i1] = bracket(coordinates, coordinate);
  return lerp(profile[i0].coordinate, profile[i0].value, profile[i1].coordinate, profile[i1].value,
              coordinate);
}

Real analyticalPoiseuilleVelocity(Real y, Real channelHeight, Real meanVelocity) {
  const Real eta = y / channelHeight;
  return 6.0 * meanVelocity * eta * (1.0 - eta);
}

Real analyticalPressureGradient(Real dynamicViscosity, Real meanVelocity, Real channelHeight) {
  return -12.0 * dynamicViscosity * meanVelocity / (channelHeight * channelHeight);
}

void writeProfileCsv(const std::string& path, const std::vector<std::tuple<Real, Real, Real>>& rows,
                     const std::string& coordinateName, const std::string& valueName) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeProfileCsv: could not open " + path);
  out << coordinateName << ",numerical_" << valueName << ",analytical_" << valueName << "\n";
  for (const auto& [coordinate, numerical, analytical] : rows) {
    out << coordinate << "," << numerical << "," << analytical << "\n";
  }
}

ErrorMetrics computeVelocityProfileErrors(const std::vector<ProfileSample>& profile,
                                          Real channelHeight, Real meanVelocity,
                                          const std::string& csvPath) {
  ErrorMetrics metrics;
  Real sumSquares = 0.0;
  Real sumAbs = 0.0;
  std::vector<std::tuple<Real, Real, Real>> rows;
  rows.reserve(profile.size());
  for (const auto& sample : profile) {
    const Real analytical =
        analyticalPoiseuilleVelocity(sample.coordinate, channelHeight, meanVelocity);
    const Real error = sample.value - analytical;
    sumSquares += error * error;
    sumAbs += std::abs(error);
    metrics.lInf = std::max(metrics.lInf, std::abs(error));
    rows.emplace_back(sample.coordinate, sample.value, analytical);
  }
  const auto n = static_cast<Real>(profile.size());
  metrics.l2 = std::sqrt(sumSquares / n);
  metrics.meanAbsolute = sumAbs / n;
  if (!csvPath.empty()) {
    writeProfileCsv(csvPath, rows, "y", "u");
  }
  return metrics;
}

void writeValidationJson(const std::string& path, const PoiseuilleValidationRecord& record) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeValidationJson: could not open " + path);
  out << "{\n"
      << "  \"case\": \"poiseuille_flow\",\n"
      << "  \"reynolds_number\": " << record.reynoldsNumber << ",\n"
      << "  \"channel\": { \"length\": " << record.channelLength
      << ", \"height\": " << record.channelHeight << " },\n"
      << "  \"mesh\": { \"nx\": " << record.nx << ", \"ny\": " << record.ny << " },\n"
      << "  \"solver\": {\n"
      << "    \"converged\": " << (record.converged ? "true" : "false") << ",\n"
      << "    \"iterations\": " << record.iterations << ",\n"
      << "    \"u_residual\": " << record.finalUResidual << ",\n"
      << "    \"v_residual\": " << record.finalVResidual << ",\n"
      << "    \"p_residual\": " << record.finalPressureResidual << ",\n"
      << "    \"continuity_residual\": " << record.finalContinuityResidual << ",\n"
      << "    \"global_mass_imbalance\": " << record.globalMassImbalance << ",\n"
      << "    \"max_wall_normal_flux\": " << record.maxWallNormalFlux << ",\n"
      << "    \"inlet_flux\": " << record.inletFlux << ",\n"
      << "    \"outlet_flux\": " << record.outletFlux << ",\n"
      << "    \"finite\": " << (record.finite ? "true" : "false") << "\n"
      << "  },\n"
      << "  \"validation\": {\n"
      << "    \"velocity_l2\": " << record.velocityError.l2 << ",\n"
      << "    \"velocity_linf\": " << record.velocityError.lInf << ",\n"
      << "    \"velocity_mean_abs\": " << record.velocityError.meanAbsolute << ",\n"
      << "    \"pressure_gradient_numerical\": " << record.pressureGradientNumerical << ",\n"
      << "    \"pressure_gradient_analytical\": " << record.pressureGradientAnalytical << ",\n"
      << "    \"pressure_gradient_relative_error\": " << record.pressureGradientRelativeError
      << "\n"
      << "  }\n"
      << "}\n";
}

}  // namespace cfd::validation
