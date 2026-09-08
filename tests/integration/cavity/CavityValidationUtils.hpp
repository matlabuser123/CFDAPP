#pragma once

// Validation-only helpers: centerline extraction, error metrics against a
// published reference, and CSV/JSON evidence output for the lid-driven
// cavity Re=100 benchmark (TODO.md "P0 -- Physical Validation").
//
// Deliberately independent of the solver (TODO.md section 47): this code
// only reads SIMPLEResult/Mesh after a solve has already happened and
// never participates in solving. Not part of cfdcore -- compiled only
// into the validation test binary.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "GhiaRe100.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::validation {

struct ProfileSample {
  cfd::Real coordinate;
  cfd::Real value;
};

// u(xFixed, y) for a structured createCartesian2D(nx, ny, ...) mesh,
// linearly interpolated in x between the two columns straddling xFixed,
// plus the exact wall (y=0, u=0) and lid (y=1, u=uLid) boundary values as
// anchors -- so interpolation against Ghia's y=0/y=1 samples uses the
// analytically-known boundary condition, not an extrapolated cell value.
[[nodiscard]] std::vector<ProfileSample> extractVerticalProfileU(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real xFixed, cfd::Real uLid);

// v(x, yFixed), same convention: linear interpolation in y between the
// two straddling rows, with exact v=0 anchors at the left/right walls
// (x=0, x=1), which are no-slip so v=0 there exactly.
[[nodiscard]] std::vector<ProfileSample> extractHorizontalProfileV(
    const cfd::mesh::Mesh& mesh, cfd::Index nx, cfd::Index ny,
    const cfd::fields::VectorField& velocity, cfd::Real yFixed);

// Linearly interpolates `profile` (sorted by coordinate) at `coordinate`.
[[nodiscard]] cfd::Real interpolateProfile(const std::vector<ProfileSample>& profile,
                                           cfd::Real coordinate);

struct ErrorMetrics {
  cfd::Real l2{0.0};
  cfd::Real lInf{0.0};
  cfd::Real meanAbsolute{0.0};
};

// Writes coordinate,numerical,ghia rows to `path` (TODO.md section 41).
void writeCenterlineCsv(const std::string& path,
                        const std::vector<std::tuple<cfd::Real, cfd::Real, cfd::Real>>& rows,
                        const std::string& coordinateName, const std::string& valueName);

// Compares `profile` (already covering the full domain, e.g. from
// extractVerticalProfileU) against a Ghia reference table by interpolating
// `profile` at each reference coordinate (TODO.md section 26: interpolate
// the numerical solution to the reference coordinates, never the reverse).
// If `csvPath` is non-empty, also writes the coordinate/numerical/ghia
// rows used for the comparison.
template <std::size_t N>
[[nodiscard]] ErrorMetrics computeGhiaErrors(const std::vector<ProfileSample>& profile,
                                             const std::array<ghia_re100::Sample, N>& reference,
                                             const std::string& csvPath = {},
                                             const std::string& coordinateName = "coordinate",
                                             const std::string& valueName = "value") {
  ErrorMetrics metrics;
  cfd::Real sumSquares = 0.0;
  cfd::Real sumAbs = 0.0;
  std::vector<std::tuple<cfd::Real, cfd::Real, cfd::Real>> rows;
  rows.reserve(N);
  for (const auto& sample : reference) {
    const cfd::Real numerical = interpolateProfile(profile, sample.coordinate);
    const cfd::Real error = numerical - sample.value;
    sumSquares += error * error;
    sumAbs += std::abs(error);
    metrics.lInf = std::max(metrics.lInf, std::abs(error));
    rows.emplace_back(sample.coordinate, numerical, sample.value);
  }
  metrics.l2 = std::sqrt(sumSquares / static_cast<cfd::Real>(N));
  metrics.meanAbsolute = sumAbs / static_cast<cfd::Real>(N);
  if (!csvPath.empty()) {
    writeCenterlineCsv(csvPath, rows, coordinateName, valueName);
  }
  return metrics;
}

// Fresh-evidence JSON record for one grid (TODO.md section 38 / 56).
struct CavityValidationRecord {
  cfd::Index nx{0};
  cfd::Index ny{0};
  bool converged{false};
  cfd::Index iterations{0};
  cfd::Real finalUResidual{0.0};
  cfd::Real finalVResidual{0.0};
  cfd::Real finalPressureResidual{0.0};
  cfd::Real finalContinuityResidual{0.0};
  cfd::Real globalMassImbalance{0.0};
  cfd::Real maxWallNormalFlux{0.0};
  bool finite{false};
  ErrorMetrics ghiaU;
  ErrorMetrics ghiaV;
};

void writeValidationJson(const std::string& path, const CavityValidationRecord& record);

}  // namespace cfd::validation
