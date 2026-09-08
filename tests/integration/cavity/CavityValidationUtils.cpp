#include "CavityValidationUtils.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace cfd::validation {

using cfd::Index;
using cfd::Real;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

namespace {

// Locates the pair of indices in a sorted, uniformly-spaced coordinate
// array straddling `target`, clamped to the domain's interior interval so
// callers never extrapolate past the first/last sample.
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
                                                   Real uLid) {
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
  profile.push_back({1.0, uLid});  // moving lid.

  std::sort(profile.begin(), profile.end(), [](const ProfileSample& a, const ProfileSample& b) {
    return a.coordinate < b.coordinate;
  });
  return profile;
}

std::vector<ProfileSample> extractHorizontalProfileV(const Mesh& mesh, Index nx, Index ny,
                                                     const VectorField& velocity, Real yFixed) {
  std::vector<Real> yCenters(ny);
  for (Index j = 0; j < ny; ++j) yCenters[j] = mesh.cell(j * nx).centroid().y;
  const auto [j0, j1] = bracket(yCenters, yFixed);

  std::vector<ProfileSample> profile;
  profile.reserve(nx + 2);
  profile.push_back({0.0, 0.0});  // left wall, no-slip: v = 0.
  for (Index i = 0; i < nx; ++i) {
    const Index cell0 = (j0 * nx) + i;
    const Index cell1 = (j1 * nx) + i;
    const Real x = mesh.cell(cell0).centroid().x;
    const Real v = lerp(yCenters[j0], velocity[cell0].y, yCenters[j1], velocity[cell1].y, yFixed);
    profile.push_back({x, v});
  }
  profile.push_back({1.0, 0.0});  // right wall, no-slip: v = 0.

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

void writeCenterlineCsv(const std::string& path,
                        const std::vector<std::tuple<Real, Real, Real>>& rows,
                        const std::string& coordinateName, const std::string& valueName) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeCenterlineCsv: could not open " + path);
  out << coordinateName << ",numerical_" << valueName << ",ghia_" << valueName << "\n";
  for (const auto& [coordinate, numerical, ghia] : rows) {
    out << coordinate << "," << numerical << "," << ghia << "\n";
  }
}

void writeValidationJson(const std::string& path, const CavityValidationRecord& record) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeValidationJson: could not open " + path);
  out << "{\n"
      << "  \"case\": \"lid_driven_cavity\",\n"
      << "  \"reynolds_number\": 100,\n"
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
      << "    \"finite\": " << (record.finite ? "true" : "false") << "\n"
      << "  },\n"
      << "  \"validation\": {\n"
      << "    \"ghia_u_l2\": " << record.ghiaU.l2 << ",\n"
      << "    \"ghia_u_linf\": " << record.ghiaU.lInf << ",\n"
      << "    \"ghia_u_mean_abs\": " << record.ghiaU.meanAbsolute << ",\n"
      << "    \"ghia_v_l2\": " << record.ghiaV.l2 << ",\n"
      << "    \"ghia_v_linf\": " << record.ghiaV.lInf << ",\n"
      << "    \"ghia_v_mean_abs\": " << record.ghiaV.meanAbsolute << "\n"
      << "  }\n"
      << "}\n";
}

}  // namespace cfd::validation
