#include "cfd/mesh/MeshQuality.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>

#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::mesh {

const char* meshQualitySeverityName(MeshQualitySeverity severity) noexcept {
  switch (severity) {
    case MeshQualitySeverity::Info:
      return "info";
    case MeshQualitySeverity::Warning:
      return "warning";
    case MeshQualitySeverity::Fatal:
      return "fatal";
  }
  return "fatal";
}

const char* meshQualityStatusName(MeshQualityStatus status) noexcept {
  switch (status) {
    case MeshQualityStatus::Valid:
      return "valid";
    case MeshQualityStatus::ValidWithWarnings:
      return "valid_with_warnings";
    case MeshQualityStatus::Invalid:
      return "invalid";
  }
  return "invalid";
}

namespace {

// Informational level (Info issue, no effect on the status): a
// non-orthogonal mesh whose uncorrected two-point diffusion neglects a
// cross-diffusion part proportional to tan(theta) -- 18% at 10 degrees.
// P12-MESH-001 measured the uncorrected error doubling at ~45 degrees
// (results/p12-mesh-001/summary.md), hence the recommendation.
constexpr Real kNonOrthogonalityInfoDegrees = 10.0;

std::string number(Real value) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  return buffer;
}

// P12-MESH-005: a location's z is printed whenever it is non-zero, so every
// 2D location (z = 0) prints exactly as before.
std::string point(const Vector2& p) {
  return "(" + number(p.x) + ", " + number(p.y) + (p.z != 0.0 ? ", " + number(p.z) : "") + ")";
}

// Running statistics of one per-entity metric.
class Accumulator {
 public:
  Accumulator(bool largestIsWorst, std::optional<Real> warning)
      : largestIsWorst_(largestIsWorst), warning_(warning) {}

  void add(Real value, Index id, const Vector2& location) {
    if (metric_.count == 0) {
      metric_.minimum = value;
      metric_.maximum = value;
      metric_.worstId = id;
      metric_.worstLocation = location;
    } else {
      const bool worse = largestIsWorst_ ? value > worstValue() : value < worstValue();
      metric_.minimum = std::min(metric_.minimum, value);
      metric_.maximum = std::max(metric_.maximum, value);
      if (worse) {
        metric_.worstId = id;
        metric_.worstLocation = location;
      }
    }
    ++metric_.count;
    sum_ += value;
    sumOfSquares_ += value * value;
    if (warning_.has_value() && value > *warning_) ++metric_.aboveWarning;
  }

  [[nodiscard]] MeshQualityMetric finish() const {
    MeshQualityMetric m = metric_;
    if (m.count > 0) {
      m.mean = sum_ / static_cast<Real>(m.count);
      m.rms = std::sqrt(sumOfSquares_ / static_cast<Real>(m.count));
    }
    return m;
  }

 private:
  [[nodiscard]] Real worstValue() const {
    return largestIsWorst_ ? metric_.maximum : metric_.minimum;
  }

  bool largestIsWorst_;
  std::optional<Real> warning_;
  MeshQualityMetric metric_;
  Real sum_{0.0};
  Real sumOfSquares_{0.0};
};

class Fatal {
 public:
  explicit Fatal(MeshQualityReport& report) : report_(report) {}

  void add(const std::string& metric, const std::string& entity, std::optional<Index> id,
           std::optional<Vector2> location, std::optional<Real> value,
           std::optional<Real> threshold, const std::string& message) {
    ++total_;
    if (report_.problems.size() < kMaxFatalIssues) {
      report_.problems.push_back(message);
      report_.issues.push_back(MeshQualityIssue{MeshQualitySeverity::Fatal, metric, entity, id,
                                                location, value, threshold, 1, message});
    }
  }

  void finish() {
    if (total_ > kMaxFatalIssues) {
      const std::string more = "... more problems not listed";
      report_.problems.push_back(more);
      report_.issues.push_back(MeshQualityIssue{
          MeshQualitySeverity::Fatal, "more", "mesh", std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, total_ - kMaxFatalIssues,
          std::to_string(total_ - kMaxFatalIssues) + " more fatal problems not listed"});
    }
  }

  [[nodiscard]] Index total() const { return total_; }

 private:
  MeshQualityReport& report_;
  Index total_{0};
};

bool finite(const Vector2& v) { return isFinite(v); }

// P12-MESH-003: connected components of the cell graph (breadth-first over
// internal faces with valid owner/neighbor ids), largest first.
void countComponents(const Mesh& mesh, MeshQualityReport& report) {
  const Index cellCount = mesh.numberOfCells();
  std::vector<std::vector<Index>> adjacency(cellCount);
  for (const Face& face : mesh.faces()) {
    if (face.isBoundary() || face.owner() >= cellCount || *face.neighbor() >= cellCount) continue;
    adjacency[face.owner()].push_back(*face.neighbor());
    adjacency[*face.neighbor()].push_back(face.owner());
  }
  std::vector<bool> seen(cellCount, false);
  std::vector<Index> queue;
  for (Index start = 0; start < cellCount; ++start) {
    if (seen[start]) continue;
    Index size = 0;
    queue.assign(1, start);
    seen[start] = true;
    while (!queue.empty()) {
      const Index c = queue.back();
      queue.pop_back();
      ++size;
      for (const Index n : adjacency[c]) {
        if (!seen[n]) {
          seen[n] = true;
          queue.push_back(n);
        }
      }
    }
    report.componentCellCounts.push_back(size);
  }
  std::sort(report.componentCellCounts.begin(), report.componentCellCounts.end(), std::greater<>());
  report.connectedComponents = report.componentCellCounts.size();
}

void addWarning(MeshQualityReport& report, const MeshQualityMetric& m, const char* metric,
                const char* entity, Real threshold, const std::string& valueText,
                const std::string& thresholdText, const std::string& advice) {
  if (m.aboveWarning == 0) return;
  report.issues.push_back(MeshQualityIssue{
      MeshQualitySeverity::Warning, metric, entity, m.worstId, m.worstLocation, m.maximum,
      threshold, m.aboveWarning,
      std::string(metric) + " " + valueText + " > " + thresholdText + " at " + entity + " " +
          std::to_string(m.worstId) + " " + point(m.worstLocation) + " (" +
          std::to_string(m.aboveWarning) + " " + entity + (m.aboveWarning == 1 ? "" : "s") +
          " above the warning threshold): " + advice});
}

}  // namespace

std::string formatMeshQualityIssue(const MeshQualityIssue& issue) {
  std::string line = std::string(meshQualitySeverityName(issue.severity)) + " " + issue.metric +
                     ": " + issue.message;
  std::string details;
  const auto append = [&](const std::string& part) {
    details += (details.empty() ? "" : "; ") + part;
  };
  if (issue.value.has_value()) append("value " + number(*issue.value));
  if (issue.threshold.has_value()) append("threshold " + number(*issue.threshold));
  if (issue.location.has_value()) append("at " + point(*issue.location));
  if (!details.empty()) line += " [" + details + "]";
  return line;
}

std::string MeshQualityReport::summaryLine() const {
  std::string line =
      std::string(meshQualityStatusName(status)) + ": " + std::to_string(cellCount) +
      " cells, cell area " + number(cellArea.minimum) + " .. " + number(cellArea.maximum) +
      ", max aspect ratio " + number(aspectRatio.maximum) + ", max non-orthogonality " +
      number(nonOrthogonality.maximum) + " deg, max skewness " + number(skewness.maximum) +
      ", max expansion ratio " + number(expansionRatio.count > 0 ? expansionRatio.maximum : 1.0) +
      ", degenerate cells " + std::to_string(degenerateCells) + ", invalid faces " +
      std::to_string(invalidFaces);
  Index warnings = 0;
  for (const auto& issue : issues) {
    if (issue.severity == MeshQualitySeverity::Warning) ++warnings;
  }
  if (warnings > 0) line += ", " + std::to_string(warnings) + " warning(s)";
  return line;
}

MeshQualityReport MeshQuality::evaluate(const Mesh& mesh, const MeshQualityThresholds& thresholds) {
  MeshQualityReport report;
  report.thresholds = thresholds;
  report.minimumVolume = std::numeric_limits<Real>::max();
  report.maximumVolume = std::numeric_limits<Real>::lowest();
  report.minimumFaceArea = std::numeric_limits<Real>::max();
  report.cellCount = mesh.numberOfCells();
  report.faceCount = mesh.numberOfFaces();

  const Index cellCount = mesh.numberOfCells();
  const Index faceCount = mesh.numberOfFaces();
  Fatal fatal(report);
  std::vector<bool> degenerate(cellCount, false);
  std::vector<bool> invalidFace(faceCount, false);
  const auto cellFatal = [&](const Cell& cell, const std::string& metric, std::optional<Real> value,
                             std::optional<Real> threshold, const std::string& message) {
    degenerate[cell.id()] = true;
    fatal.add(metric, "cell", cell.id(),
              finite(cell.centroid()) ? std::optional<Vector2>(cell.centroid()) : std::nullopt,
              value, threshold, message);
  };
  const auto faceFatal = [&](const Face& face, const std::string& metric, std::optional<Real> value,
                             std::optional<Real> threshold, const std::string& message) {
    invalidFace[face.id()] = true;
    fatal.add(metric, "face", face.id(),
              finite(face.centroid()) ? std::optional<Vector2>(face.centroid()) : std::nullopt,
              value, threshold, message);
  };

  Accumulator cellArea(false, std::nullopt);
  Accumulator faceLength(false, std::nullopt);
  Accumulator aspectRatio(true, thresholds.aspectRatioWarning);
  Accumulator nonOrthogonality(true, thresholds.nonOrthogonalityWarningDegrees);
  Accumulator skewness(true, thresholds.skewnessWarning);
  Accumulator expansionRatio(true, thresholds.expansionRatioWarning);

  // --- Cells: area, aspect ratio, degenerate-cell conditions ---------------
  for (const Cell& cell : mesh.cells()) {
    const std::string where = "cell " + std::to_string(cell.id());
    report.minimumVolume = std::min(report.minimumVolume, cell.volume());
    report.maximumVolume = std::max(report.maximumVolume, cell.volume());
    if (!std::isfinite(cell.volume()) || !(cell.volume() > 0.0)) {
      cellFatal(cell, "cell_area", cell.volume(), 0.0,
                where + " has non-positive or non-finite area " + number(cell.volume()));
    } else if (finite(cell.centroid())) {
      cellArea.add(cell.volume(), cell.id(), cell.centroid());
    }
    if (!finite(cell.centroid())) {
      cellFatal(cell, "cell_centroid", std::nullopt, std::nullopt,
                where + " has a non-finite centroid");
    }
    // A closed cell needs at least dimension + 1 faces (a triangle in 2D, a
    // tetrahedron in 3D -- P12-MESH-005).
    const std::size_t minimumFaces = static_cast<std::size_t>(mesh.dimension()) + 1;
    if (cell.faceIds().size() < minimumFaces) {
      cellFatal(cell, "connectivity", static_cast<Real>(cell.faceIds().size()),
                static_cast<Real>(minimumFaces),
                where + " has fewer than " + std::to_string(minimumFaces) + " faces");
      continue;
    }
    Vector2 closure{0.0, 0.0};
    Real lengthSum = 0.0;
    Real shortest = std::numeric_limits<Real>::max();
    Real longest = 0.0;
    bool connected = true;
    for (const Index faceId : cell.faceIds()) {
      if (faceId >= faceCount) {
        cellFatal(cell, "connectivity", std::nullopt, std::nullopt,
                  where + " lists a non-existent face " + std::to_string(faceId));
        connected = false;
        break;
      }
      const Face& face = mesh.face(faceId);
      if (face.owner() == cell.id()) {
        closure = closure + face.areaVector();
      } else if (face.neighbor().has_value() && *face.neighbor() == cell.id()) {
        closure = closure - face.areaVector();
      } else {
        cellFatal(cell, "connectivity", std::nullopt, std::nullopt,
                  where + " lists face " + std::to_string(faceId) +
                      " that it neither owns nor neighbors");
        connected = false;
        break;
      }
      lengthSum += face.area();
      shortest = std::min(shortest, face.area());
      longest = std::max(longest, face.area());
    }
    if (!connected) continue;
    if (!(magnitude(closure) <= 1e-10 * lengthSum)) {
      cellFatal(cell, "closure", magnitude(closure), 1e-10 * lengthSum,
                where + " is not closed (outward area vectors do not sum to zero)");
    }
    if (std::isfinite(shortest) && std::isfinite(longest) && shortest > 0.0 &&
        finite(cell.centroid())) {
      aspectRatio.add(longest / shortest, cell.id(), cell.centroid());
    }
  }

  // --- Patches ---------------------------------------------------------------
  std::vector<int> patchCount(faceCount, 0);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      if (faceId >= faceCount) {
        fatal.add("patch", "patch", std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                  "patch '" + patch.name() + "' lists a non-existent face");
        continue;
      }
      if (!mesh.face(faceId).isBoundary()) {
        invalidFace[faceId] = true;
        fatal.add("patch", "patch", faceId, mesh.face(faceId).centroid(), std::nullopt,
                  std::nullopt,
                  "patch '" + patch.name() + "' lists internal face " + std::to_string(faceId));
      }
      ++patchCount[faceId];
    }
  }

  // --- Faces: length, ids, orientation, non-orthogonality, skewness,
  // expansion ratio ------------------------------------------------------------
  for (const Face& face : mesh.faces()) {
    const std::string where = "face " + std::to_string(face.id());
    report.minimumFaceArea = std::min(report.minimumFaceArea, face.area());
    if (!std::isfinite(face.area()) || !(face.area() > 0.0)) {
      faceFatal(face, "face_length", face.area(), 0.0,
                where + " has non-positive or non-finite length " + number(face.area()));
    } else if (finite(face.centroid())) {
      faceLength.add(face.area(), face.id(), face.centroid());
    }
    const bool ownerValid = face.owner() < cellCount;
    const bool neighborValid = !face.neighbor().has_value() || *face.neighbor() < cellCount;
    if (!ownerValid || !neighborValid) {
      faceFatal(face, "connectivity", std::nullopt, std::nullopt,
                where + " has an invalid owner/neighbor cell id");
      continue;
    }
    if (face.neighbor().has_value() && *face.neighbor() == face.owner()) {
      faceFatal(face, "connectivity", std::nullopt, std::nullopt,
                where + " has the same cell as owner and neighbor");
      continue;
    }
    const Cell& owner = mesh.cell(face.owner());
    if (face.isBoundary()) {
      ++report.boundaryFaceCount;
      if (patchCount[face.id()] != 1) {
        faceFatal(face, "patch", static_cast<Real>(patchCount[face.id()]), 1.0,
                  where + " (boundary) is in " + std::to_string(patchCount[face.id()]) +
                      " boundary patches, expected exactly 1");
      }
      if (!(dot(face.centroid() - owner.centroid(), face.areaVector()) > 0.0)) {
        faceFatal(face, "orientation", std::nullopt, std::nullopt,
                  where + " (boundary) area vector does not point out of its owner cell");
      }
      continue;
    }
    ++report.internalFaceCount;
    const Cell& neighbor = mesh.cell(*face.neighbor());
    const Vector2 d = neighbor.centroid() - owner.centroid();
    if (!(dot(d, face.areaVector()) > 0.0)) {
      faceFatal(face, "orientation", std::nullopt, std::nullopt,
                where +
                    " area vector does not point from owner to neighbor "
                    "((x_N - x_P) . Sf <= 0: inverted or folded face)");
      continue;
    }
    // decomposeFaceArea's `valid` is the single well-posedness condition
    // non-orthogonality and skewness share (MeshGeometry.cpp).
    if (!MeshGeometry::decomposeFaceArea(mesh, face).valid) {
      faceFatal(face, "non_orthogonality", std::nullopt, 89.9999,
                where +
                    " is degenerate: the owner-neighbor line nearly lies in the face "
                    "(non-orthogonality >= 89.9999 deg), no usable diffusion coefficient");
      continue;
    }
    const Real angle = MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face);
    nonOrthogonality.add(angle, face.id(), face.centroid());
    if (const auto skew = MeshGeometry::skewness(mesh, face); skew.has_value()) {
      skewness.add(*skew, face.id(), face.centroid());
    }
    const Real a = owner.volume();
    const Real b = neighbor.volume();
    if (std::isfinite(a) && std::isfinite(b) && a > 0.0 && b > 0.0) {
      expansionRatio.add(std::max(a, b) / std::min(a, b), face.id(), face.centroid());
    }
  }

  // --- Connectivity of the whole mesh -----------------------------------------
  countComponents(mesh, report);
  if (report.connectedComponents > 1) {
    std::string sizes;
    for (std::size_t k = 0; k < report.componentCellCounts.size() && k < 10; ++k) {
      sizes += (k == 0 ? "" : ", ") + std::to_string(report.componentCellCounts[k]);
    }
    fatal.add("connected_components", "mesh", std::nullopt, std::nullopt,
              static_cast<Real>(report.connectedComponents), 1.0,
              "mesh has " + std::to_string(report.connectedComponents) +
                  " disconnected cell regions (cells per region: " + sizes +
                  "); the fluid domain must be one connected region");
  }
  fatal.finish();

  report.cellArea = cellArea.finish();
  report.faceLength = faceLength.finish();
  report.aspectRatio = aspectRatio.finish();
  report.nonOrthogonality = nonOrthogonality.finish();
  report.skewness = skewness.finish();
  report.expansionRatio = expansionRatio.finish();
  report.degenerateCells =
      static_cast<Index>(std::count(degenerate.begin(), degenerate.end(), true));
  report.invalidFaces =
      static_cast<Index>(std::count(invalidFace.begin(), invalidFace.end(), true));

  // Pre-existing summary fields.
  report.maximumAspectRatio = report.aspectRatio.maximum;
  report.maxNonOrthogonalityDegrees = report.nonOrthogonality.maximum;
  report.meanNonOrthogonalityDegrees = report.nonOrthogonality.mean;
  report.maxSkewness = report.skewness.maximum;
  report.meanSkewness = report.skewness.mean;

  // --- Warnings (aggregated per metric) and information ----------------------
  addWarning(report, report.nonOrthogonality, "non_orthogonality", "face",
             thresholds.nonOrthogonalityWarningDegrees,
             number(report.nonOrthogonality.maximum) + " deg",
             number(thresholds.nonOrthogonalityWarningDegrees) + " deg",
             "the non-orthogonal flux correction dominates the implicit flux; expect degraded "
             "accuracy and slow or failing convergence");
  addWarning(report, report.skewness, "skewness", "face", thresholds.skewnessWarning,
             number(report.skewness.maximum), number(thresholds.skewnessWarning),
             "the skewness-corrected gradient converges slowly; expect degraded gradient "
             "accuracy");
  addWarning(report, report.aspectRatio, "aspect_ratio", "cell", thresholds.aspectRatioWarning,
             number(report.aspectRatio.maximum), number(thresholds.aspectRatioWarning),
             "strongly anisotropic coefficients; expect slow linear-solver convergence");
  addWarning(report, report.expansionRatio, "expansion_ratio", "face",
             thresholds.expansionRatioWarning, number(report.expansionRatio.maximum),
             number(thresholds.expansionRatioWarning),
             "abrupt cell-size change; interpolation there is only first-order accurate");
  if (report.nonOrthogonality.maximum > kNonOrthogonalityInfoDegrees) {
    const auto& m = report.nonOrthogonality;
    report.issues.push_back(
        MeshQualityIssue{MeshQualitySeverity::Info, "non_orthogonality", "face", m.worstId,
                         m.worstLocation, m.maximum, kNonOrthogonalityInfoDegrees, 1,
                         "non-orthogonal mesh (max " + number(m.maximum) + " deg at face " +
                             std::to_string(m.worstId) +
                             "): set non_orthogonal_corrections >= 1 for accurate "
                             "diffusion"});
  }

  bool warnings = false;
  for (const auto& issue : report.issues) {
    warnings = warnings || issue.severity == MeshQualitySeverity::Warning;
  }
  report.status = fatal.total() > 0 ? MeshQualityStatus::Invalid
                  : warnings        ? MeshQualityStatus::ValidWithWarnings
                                    : MeshQualityStatus::Valid;
  report.valid = report.status != MeshQualityStatus::Invalid;
  return report;
}

}  // namespace cfd::mesh
