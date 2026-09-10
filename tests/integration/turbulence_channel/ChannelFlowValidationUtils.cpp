#include "ChannelFlowValidationUtils.hpp"

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

// Same bracket()/lerp() convention as PoiseuilleValidationUtils.cpp /
// CavityValidationUtils.cpp's own private helpers.
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

}  // namespace

std::vector<ProfileSample> extractVerticalProfileU(const Mesh& mesh, Index nx, Index ny,
                                                   const VectorField& velocity, Real xFixed,
                                                   Real channelHeight) {
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
  profile.push_back({channelHeight, 0.0});  // top wall, no-slip.

  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
}

std::vector<ProfileSample> extractVerticalProfileScalar(const Mesh& mesh, Index nx, Index ny,
                                                        const ScalarField& field, Real xFixed) {
  const auto xCenters = columnCentersX(mesh, nx);
  const auto [i0, i1] = bracket(xCenters, xFixed);

  std::vector<ProfileSample> profile;
  profile.reserve(ny);
  for (Index j = 0; j < ny; ++j) {
    const Index cell0 = (j * nx) + i0;
    const Index cell1 = (j * nx) + i1;
    const Real y = mesh.cell(cell0).centroid().y;
    const Real value = lerp(xCenters[i0], field[cell0], xCenters[i1], field[cell1], xFixed);
    profile.push_back({y, value});
  }
  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
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

Real computeWallShearStress(const Mesh& mesh, Index nx, Index ny, const VectorField& velocity,
                            Real xFixed, Real channelHeight, Real dynamicViscosity,
                            bool bottomWall) {
  if (ny == 0) throw std::invalid_argument("computeWallShearStress: ny must be > 0");
  const auto xCenters = columnCentersX(mesh, nx);
  const auto [i0, i1] = bracket(xCenters, xFixed);
  const Index row = bottomWall ? 0 : (ny - 1);
  const Index cell0 = (row * nx) + i0;
  const Index cell1 = (row * nx) + i1;
  const Real u1 = lerp(xCenters[i0], velocity[cell0].x, xCenters[i1], velocity[cell1].x, xFixed);
  const Real y1 = channelHeight / (2.0 * static_cast<Real>(ny));  // cell-center-to-wall distance.
  return dynamicViscosity * u1 / y1;
}

Real computeFrictionVelocity(Real wallShearStress, Real density) {
  if (!(density > 0.0) || !std::isfinite(density)) {
    throw std::invalid_argument("computeFrictionVelocity: density must be finite and > 0");
  }
  return std::sqrt(std::abs(wallShearStress) / density);
}

Real computeYPlus(Real y, Real frictionVelocity, Real kinematicViscosity) {
  return y * frictionVelocity / kinematicViscosity;
}

Real computeUPlus(Real velocity, Real frictionVelocity) { return velocity / frictionVelocity; }

Real computeAchievedReTau(Real frictionVelocity, Real channelHeight, Real kinematicViscosity) {
  return frictionVelocity * (channelHeight / 2.0) / kinematicViscosity;
}

Real lawOfTheWallUPlus(Real yPlus, Real kappa, Real additiveB, Real viscousSublayerMaxYPlus,
                       Real bufferLayerMaxYPlus) {
  if (!(yPlus > 0.0) || !std::isfinite(yPlus)) {
    throw std::invalid_argument("lawOfTheWallUPlus: yPlus must be finite and > 0");
  }
  if (yPlus < viscousSublayerMaxYPlus) return yPlus;
  const Real logLawValue = (1.0 / kappa) * std::log(yPlus) + additiveB;
  if (yPlus > bufferLayerMaxYPlus) return logLawValue;
  // Buffer layer: neither closed form is physically valid here: blend
  // linearly (in y+, not log-y+) between the two boundary values purely
  // as a continuous visual/interpolation convenience for figures and
  // region-boundary error metrics -- documented as such, never presented
  // as a genuine "buffer-layer law".
  const Real sublayerBoundaryValue = viscousSublayerMaxYPlus;
  const Real logBoundaryValue = (1.0 / kappa) * std::log(bufferLayerMaxYPlus) + additiveB;
  return lerp(viscousSublayerMaxYPlus, sublayerBoundaryValue, bufferLayerMaxYPlus, logBoundaryValue,
              yPlus);
}

namespace {

void accumulate(ErrorMetrics* metrics, Real numerical, Real reference, Real* sumSquares,
                Real* sumAbs) {
  const Real error = numerical - reference;
  *sumSquares += error * error;
  *sumAbs += std::abs(error);
  metrics->lInf = std::max(metrics->lInf, std::abs(error));
  metrics->sampleCount += 1;
}

void finalize(ErrorMetrics* metrics, Real sumSquares, Real sumAbs) {
  if (metrics->sampleCount == 0) return;
  const auto n = static_cast<Real>(metrics->sampleCount);
  metrics->l2 = std::sqrt(sumSquares / n);
  metrics->meanAbsolute = sumAbs / n;
}

}  // namespace

RegionErrorMetrics computeWallUnitsErrors(const std::vector<ProfileSample>& wallUnitsProfile,
                                          Real kappa, Real additiveB, Real viscousSublayerMaxYPlus,
                                          Real bufferLayerMaxYPlus, const std::string& csvPath) {
  RegionErrorMetrics result;
  Real overallSq = 0.0, overallAbs = 0.0;
  Real nearSq = 0.0, nearAbs = 0.0;
  Real bufferSq = 0.0, bufferAbs = 0.0;
  Real outerSq = 0.0, outerAbs = 0.0;
  std::vector<std::tuple<Real, Real, Real>> rows;
  rows.reserve(wallUnitsProfile.size());

  for (const auto& sample : wallUnitsProfile) {
    const Real yPlus = sample.coordinate;
    const Real uPlusNumerical = sample.value;
    const Real uPlusReference =
        lawOfTheWallUPlus(yPlus, kappa, additiveB, viscousSublayerMaxYPlus, bufferLayerMaxYPlus);
    rows.emplace_back(yPlus, uPlusNumerical, uPlusReference);

    accumulate(&result.overall, uPlusNumerical, uPlusReference, &overallSq, &overallAbs);
    if (yPlus < viscousSublayerMaxYPlus) {
      accumulate(&result.nearWall, uPlusNumerical, uPlusReference, &nearSq, &nearAbs);
    } else if (yPlus <= bufferLayerMaxYPlus) {
      accumulate(&result.buffer, uPlusNumerical, uPlusReference, &bufferSq, &bufferAbs);
    } else {
      accumulate(&result.outer, uPlusNumerical, uPlusReference, &outerSq, &outerAbs);
    }
  }
  finalize(&result.overall, overallSq, overallAbs);
  finalize(&result.nearWall, nearSq, nearAbs);
  finalize(&result.buffer, bufferSq, bufferAbs);
  finalize(&result.outer, outerSq, outerAbs);

  if (!csvPath.empty()) writeProfileCsv(csvPath, rows, "y_plus", "u_plus");
  return result;
}

Real developmentRelativeDifference(const std::vector<ProfileSample>& profileA,
                                   const std::vector<ProfileSample>& profileB) {
  if (profileA.size() != profileB.size() || profileA.empty()) {
    throw std::invalid_argument(
        "developmentRelativeDifference: profiles must be non-empty and the same length");
  }
  Real diffSq = 0.0, normSq = 0.0;
  for (std::size_t i = 0; i < profileA.size(); ++i) {
    const Real diff = profileA[i].value - profileB[i].value;
    diffSq += diff * diff;
    normSq += profileB[i].value * profileB[i].value;
  }
  if (normSq == 0.0) return 0.0;
  return std::sqrt(diffSq / normSq);
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

namespace {

void writeErrorMetrics(std::ofstream& out, const ErrorMetrics& metrics, bool trailingComma) {
  out << "{ \"l2\": " << metrics.l2 << ", \"linf\": " << metrics.lInf
      << ", \"mean_absolute\": " << metrics.meanAbsolute
      << ", \"sample_count\": " << metrics.sampleCount << " }" << (trailingComma ? "," : "")
      << "\n";
}

}  // namespace

void writeValidationJson(const std::string& path, const ChannelFlowValidationRecord& record) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeValidationJson: could not open " + path);
  out << "{\n"
      << "  \"case\": \"turbulent_channel_flow\",\n"
      << "  \"model\": \"" << record.model << "\",\n"
      << "  \"reynolds_number_bulk\": " << record.reynoldsNumberBulk << ",\n"
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
      << "    \"has_turbulence_residual\": " << (record.hasTurbulenceResidual ? "true" : "false")
      << ",\n"
      << "    \"turbulence_residual\": " << record.finalTurbulenceResidual << ",\n"
      << "    \"global_mass_imbalance\": " << record.globalMassImbalance << ",\n"
      << "    \"inlet_flux\": " << record.inletFlux << ",\n"
      << "    \"outlet_flux\": " << record.outletFlux << ",\n"
      << "    \"finite\": " << (record.finite ? "true" : "false") << ",\n"
      << "    \"runtime_seconds\": " << record.runtimeSeconds << "\n"
      << "  },\n"
      << "  \"development\": { \"relative_diff_between_stations\": "
      << record.developmentRelativeDiff << " },\n"
      << "  \"wall\": {\n"
      << "    \"tau_w_bottom\": " << record.wallShearBottom << ",\n"
      << "    \"tau_w_top\": " << record.wallShearTop << ",\n"
      << "    \"tau_w_asymmetry\": " << record.wallShearAsymmetry << ",\n"
      << "    \"u_tau\": " << record.frictionVelocity << ",\n"
      << "    \"first_cell_y_plus\": " << record.firstCellYPlus << ",\n"
      << "    \"achieved_re_tau\": " << record.achievedReTau << ",\n"
      << "    \"re_tau_target\": " << record.reTauTarget << ",\n"
      << "    \"re_tau_relative_error\": " << record.reTauRelativeError << "\n"
      << "  },\n"
      << "  \"wall_units_error\": {\n"
      << "    \"overall\": ";
  writeErrorMetrics(out, record.wallUnitsError.overall, true);
  out << "    \"near_wall\": ";
  writeErrorMetrics(out, record.wallUnitsError.nearWall, true);
  out << "    \"buffer\": ";
  writeErrorMetrics(out, record.wallUnitsError.buffer, true);
  out << "    \"outer\": ";
  writeErrorMetrics(out, record.wallUnitsError.outer, false);
  out << "  },\n"
      << "  \"turbulence_fields\": {\n"
      << "    \"min_k\": " << record.minK << ",\n"
      << "    \"max_k\": " << record.maxK << ",\n"
      << "    \"has_second_scalar\": " << (record.hasSecondScalar ? "true" : "false") << ",\n"
      << "    \"min_second_scalar\": " << record.minSecondScalar << ",\n"
      << "    \"max_second_scalar\": " << record.maxSecondScalar << ",\n"
      << "    \"min_mu_t\": " << record.minMuT << ",\n"
      << "    \"max_mu_t\": " << record.maxMuT << ",\n"
      << "    \"molecular_viscosity\": " << record.molecularViscosity << ",\n"
      << "    \"has_f1_f2\": " << (record.hasF1F2 ? "true" : "false") << ",\n"
      << "    \"min_f1\": " << record.minF1 << ",\n"
      << "    \"max_f1\": " << record.maxF1 << ",\n"
      << "    \"min_f2\": " << record.minF2 << ",\n"
      << "    \"max_f2\": " << record.maxF2 << ",\n"
      << "    \"min_wall_distance\": " << record.minWallDistance << ",\n"
      << "    \"max_wall_distance\": " << record.maxWallDistance << "\n"
      << "  },\n"
      << "  \"deterministic\": " << (record.deterministic ? "true" : "false") << "\n"
      << "}\n";
}

}  // namespace cfd::validation
