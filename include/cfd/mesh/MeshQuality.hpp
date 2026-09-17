#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

class Mesh;

// P12-MESH-004 -- the authoritative production mesh-quality report. One
// evaluation (MeshQuality::evaluate) computes every metric, classifies the
// mesh, and names where each problem is; CaseBuilder's validity gate, the
// CLI, the GUI and the results JSON all read this report -- no other code
// defines these metrics. Non-orthogonality and skewness are the P12-NUM-003
// definitions in MeshGeometry, aggregated here, never redefined.
//
// Metric definitions (2D; a cell's "area" is Cell::volume(), a face's
// "length" is Face::area(), the magnitude of its area vector):
//   cell_area            A_c > 0 [m^2]. Per cell; worst = smallest.
//   face_length          |S_f| > 0 [m]. Per face; worst = shortest.
//   aspect_ratio         max_f |S_f| / min_f |S_f| over the cell's own faces,
//                        >= 1 [-] (1 for a square; w/h for a w x h rectangle;
//                        coordinate-free, so a rotated cell keeps its value).
//                        Per cell; worst = largest.
//   non_orthogonality    angle between S_f and d = x_N - x_P, degrees in
//                        [0, 180): 0 = orthogonal, -> 90 = the orthogonal
//                        diffusion coefficient |S_f|^2 / (d . S_f) blows up
//                        (MeshGeometry::nonOrthogonalityAngleDegrees). Per
//                        internal face; worst = largest.
//   skewness             |x_f - x_f'| / |d| [-]: distance from the face
//                        centroid to where the owner-neighbour line crosses
//                        the face, per unit owner-neighbour distance; 0 =
//                        unskewed (MeshGeometry::skewness). Per internal
//                        face; worst = largest.
//   expansion_ratio      max(A_P, A_N) / min(A_P, A_N) >= 1 [-]: size change
//                        between the two cells sharing an internal face,
//                        symmetric in the direction (expansion and
//                        contraction give the same value); 1 on a uniform
//                        grid, r across a face of a geometric grading r.
//                        Per internal face; worst = largest.
// Statistics of each: count, min, max, mean, RMS, the worst entity (id and
// centroid) and the number of entities beyond the warning threshold.
//
// P12-MESH-005, 3D meshes: the same definitions, evaluated on the same
// Cell::volume() / Face::area() -- so for a 3D mesh cell_area is the cell
// volume [m^3], face_length the face area [m^2], and aspect_ratio the
// ratio of the largest to the smallest face area (for a dx x dy x dz box
// exactly max(dx, dy, dz) / min(dx, dy, dz)). The metric names are kept
// (they are the JSON/CLI keys of every existing 2D report). A cell needs at
// least dimension + 1 faces; locations print z when it is non-zero.
enum class MeshQualitySeverity { Info, Warning, Fatal };
enum class MeshQualityStatus { Valid, ValidWithWarnings, Invalid };

[[nodiscard]] const char* meshQualitySeverityName(MeshQualitySeverity severity) noexcept;
[[nodiscard]] const char* meshQualityStatusName(MeshQualityStatus status) noexcept;

// Warning thresholds: beyond them the mesh is still solvable but its
// numerical reliability is degraded (ValidWithWarnings). Rationale in
// results/p12-mesh-004/summary.md section 5:
//   non-orthogonality 70 deg: the non-orthogonal part of the face flux is
//     tan(70) = 2.7x the orthogonal part -- the deferred correction then
//     dominates the implicit two-point flux and its fixed-point iteration
//     converges slowly or not at all (also the common "severe" level);
//   skewness 0.5: the skewness-corrected Green-Gauss gradient contracts its
//     error by roughly the skewness per sweep (Gradient.hpp), so beyond 0.5
//     each of its kGreenGaussSkewCorrectionSweeps sweeps removes at most
//     half of the skewness error;
//   aspect ratio 100: two-point coefficients across the long and short
//     sides of a cell differ by AR^2 = 1e4, the anisotropy at which the
//     unpreconditioned Krylov solvers' iteration counts (~ sqrt of the
//     condition number) grow by two orders of magnitude;
//   expansion ratio 2: the face-interpolation weight moves from 1/2 to 1/3
//     and the interpolation error's formally first-order part, proportional
//     to (r - 1) / (r + 1), reaches 1/3 of the second-order term's scale.
// There is no universal fatal value for these four: a mesh beyond them is
// reported, not rejected.
//
// Fatal (Invalid -- the mesh cannot be solved safely; not configurable):
// a cell with non-finite or non-positive area, a non-finite centroid, fewer
// than 3 faces, a face it neither owns nor neighbours, or outward area
// vectors that do not sum to zero (|sum| > 1e-10 sum |S_f|) -- a
// "degenerate cell"; a face with non-finite or non-positive length, an
// invalid owner/neighbour id, owner == neighbour, an area vector pointing
// the wrong way ((x_N - x_P) . S_f <= 0, or (x_f - x_P) . S_f <= 0 on a
// boundary: inverted or folded), an internal face whose owner-neighbour
// line is within 1e-6 (cosine) of lying in the face (non-orthogonality
// >= 89.9999 deg: no usable diffusion coefficient), or a boundary face not
// in exactly one patch -- an "invalid face"; and a mesh of more than one
// connected region.
struct MeshQualityThresholds {
  Real nonOrthogonalityWarningDegrees{70.0};
  Real skewnessWarning{0.5};
  Real aspectRatioWarning{100.0};
  Real expansionRatioWarning{2.0};
};

struct MeshQualityMetric {
  Index count{0};  // entities measured (0 = not available, e.g. no internal face)
  Real minimum{0.0};
  Real maximum{0.0};
  Real mean{0.0};
  Real rms{0.0};
  Index worstId{0};         // cell or face id of the worst value (lowest id on ties)
  Vector2 worstLocation{};  // that cell's / face's centroid
  Index aboveWarning{0};    // entities beyond the warning threshold
};

// One reported condition. Warnings are aggregated per metric (the worst
// entity and how many exceed the threshold); fatal conditions are listed
// per entity (at most kMaxFatalIssues, then one "more" entry).
struct MeshQualityIssue {
  MeshQualitySeverity severity{MeshQualitySeverity::Info};
  std::string metric;  // e.g. "non_orthogonality", "cell_area", "connectivity"
  std::string entity;  // "cell", "face", "patch" or "mesh"
  std::optional<Index> id;
  std::optional<Vector2> location;
  std::optional<Real> value;      // measured value, when the condition has one
  std::optional<Real> threshold;  // the limit it was compared against
  Index count{1};                 // entities with this condition (aggregated warnings)
  std::string message;            // one self-contained human-readable line
};

struct MeshQualityReport {
  // --- Pre-existing summary fields (unchanged meaning) --------------------
  Real minimumVolume{};
  Real maximumVolume{};
  Real minimumFaceArea{};
  Real maximumAspectRatio{};

  // P12-NUM-003: aggregated over every INTERNAL face with a well-posed
  // decomposition. Zero on a mesh with no internal faces (a vacuous "no
  // data" zero, not a claim of perfect orthogonality).
  Real maxNonOrthogonalityDegrees{};
  Real meanNonOrthogonalityDegrees{};
  Real maxSkewness{};
  Real meanSkewness{};

  // The messages of the fatal issues (at most kMaxFatalIssues, then a final
  // "... more" entry), empty for a valid mesh.
  std::vector<std::string> problems;

  // P12-MESH-003: connected components of the cell graph and the cell count
  // of each, largest first.
  Index connectedComponents{0};
  std::vector<Index> componentCellCounts;

  bool valid{};  // == (status != Invalid)

  // --- P12-MESH-004 ---------------------------------------------------------
  MeshQualityStatus status{MeshQualityStatus::Invalid};
  MeshQualityThresholds thresholds;
  Index cellCount{0};
  Index faceCount{0};
  Index internalFaceCount{0};
  Index boundaryFaceCount{0};
  MeshQualityMetric cellArea;
  MeshQualityMetric faceLength;
  MeshQualityMetric aspectRatio;
  MeshQualityMetric nonOrthogonality;  // degrees
  MeshQualityMetric skewness;
  MeshQualityMetric expansionRatio;
  Index degenerateCells{0};
  Index invalidFaces{0};
  std::vector<MeshQualityIssue> issues;  // deterministic: fatal (by entity id), then warnings

  // "<status>: 1080 cells, min area 4.54e-03, max aspect ratio 1.26, ..." --
  // the one-line summary the CLI and case errors print.
  [[nodiscard]] std::string summaryLine() const;
};

inline constexpr std::size_t kMaxFatalIssues = 20;

// "<severity> <metric>: <message> [value V; threshold T; at (x, y)]" -- the
// one line the CLI, CaseBuilder errors and the GUI print per issue.
[[nodiscard]] std::string formatMeshQualityIssue(const MeshQualityIssue& issue);

class MeshQuality {
 public:
  MeshQuality() = delete;

  [[nodiscard]] static MeshQualityReport evaluate(const Mesh& mesh,
                                                  const MeshQualityThresholds& thresholds = {});
};

}  // namespace cfd::mesh
