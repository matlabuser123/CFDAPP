// P12-DIFF-002 Amendment A1 -- independent topology oracle for boundary-stencil availability.
//
// The original W3b was mis-derived: it demanded 100 % fallback on meshes that are one-cell-thick in
// only ONE direction, which across their other direction(s) legitimately have a valid interior
// stencil. W3b-A1 replaces the blanket count with a per-face classification agreement against an
// oracle that is NOT the production implementation's own answer.
//
// Two oracles, deliberately built on different information, so neither can be circular:
//
//   Oracle-T (analytic / purely topological, NO floating point at all).
//     From the mesh GENERATOR's documented topology only: a structured block created with
//     (nx, ny, nz) names its boundary patches by axis extreme, so a patch normal to an axis has a
//     second cell inward exactly when that axis has >= 2 cells. Hand-derived; it never looks at a
//     coordinate, a normal, a centroid or any production function. Applies to every single-block
//     structured mesh, including graded and distorted ones (MESH-001/MESH-002 guarantee topology
//     identical to createCartesian2D).
//
//   Oracle-G (general connectivity walk by NORMAL DEPTH -- not by normal alignment).
//     For boundary face f of cell P, walk P's own faces through the cell-adjacency graph. A face g
//     of P that is interior leads to a cell F. Measure how deep each cell lies along the inward
//     wall normal, s = (x_f - x_cell) . n_out. The reconstruction needs a second point STRICTLY
//     deeper than P, i.e. some F with s_F > s_P; that is exactly the mathematical requirement
//     (h2 > h1). This selects by depth, whereas production's MeshGeometry::oppositeInteriorFace
//     selects the most ANTI-PARALLEL interior face -- a different rule, so agreement is evidence,
//     not tautology. Applies to every mesh, including multi-block and curved.
//
// Production's own answer is read from FaceDiffusionTerms::higherOrder and is compared against
// both.
//
// For FALLBACK_REQUIRED faces the probe additionally compares the production terms BIT FOR BIT
// against the pre-DIFF-002 two-point expression, transcribed literally from the production fallback
// branch. For HIGHER_ORDER_REQUIRED faces it checks the reconstruction was really exercised (a
// non-zero far-cell coupling, terms that actually differ from the two-point ones, and the
// consistency identity cP - cF = cB), so a silent fallback cannot pass.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::discretization::boundaryFaceDiffusionTerms;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kGamma = 0.37;

enum class Verdict { HigherOrderRequired, FallbackRequired };

// Negative controls, to prove W3b-A1 is not vacuous. `Production` reads the real classification;
// the other two substitute the answer a broken library would give, WITHOUT touching production:
//   BaselineFallbackEverywhere -- exactly the pre-DIFF-002 library, which had no higher-order path
//     at all. This is the state the ORIGINAL W3b could not distinguish (it passed vacuously there).
//   OverEagerHigherOrderEverywhere -- an implementation that reconstructed even where no opposite
//     interior cell exists, i.e. one that ignored the fallback requirement.
// Both must FAIL the criterion on the mesh set, or the criterion has no power in that direction.
enum class Mode { Production, BaselineFallbackEverywhere, OverEagerHigherOrderEverywhere };

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// ---------------------------------------------------------------------------
// Oracle-T: analytic, from the generator's parameters. No floating point.
// ---------------------------------------------------------------------------
struct BlockShape {
  Index nx{};
  Index ny{};
  Index nz{1};
};

// Which axis a patch is normal to, from its generated name alone. 0 = x, 1 = y, 2 = z.
std::optional<int> patchAxis(const std::string& patch) {
  if (patch == "left" || patch == "right" || patch == "xmin" || patch == "xmax") return 0;
  if (patch == "bottom" || patch == "top" || patch == "ymin" || patch == "ymax") return 1;
  if (patch == "zmin" || patch == "zmax") return 2;
  return std::nullopt;  // not a single-block structured patch name
}

std::optional<Verdict> oracleT(const std::string& patch, const BlockShape& shape) {
  const auto axis = patchAxis(patch);
  if (!axis.has_value()) return std::nullopt;
  const Index cellsAcross = (*axis == 0) ? shape.nx : ((*axis == 1) ? shape.ny : shape.nz);
  return (cellsAcross >= 2) ? Verdict::HigherOrderRequired : Verdict::FallbackRequired;
}

// ---------------------------------------------------------------------------
// Oracle-G: general connectivity walk, selecting by normal depth.
// ---------------------------------------------------------------------------
Verdict oracleG(const Mesh& mesh, const Face& boundaryFace) {
  const auto& owner = mesh.cell(boundaryFace.owner());
  // The raw outward area vector is enough: every comparison below is a difference of depths
  // measured along it, and that ordering is invariant under positive scaling, so no normalization
  // (and no division) is needed.
  const Vector3& outward = boundaryFace.areaVector();
  const Vector3& faceCentroid = boundaryFace.centroid();
  const Real depthOwner = dot(faceCentroid - owner.centroid(), outward);
  for (const Index faceId : owner.faceIds()) {
    if (faceId == boundaryFace.id()) continue;
    const Face& candidate = mesh.face(faceId);
    if (candidate.isBoundary()) continue;
    const Index farId =
        (candidate.owner() == owner.id()) ? *candidate.neighbor() : candidate.owner();
    if (dot(faceCentroid - mesh.cell(farId).centroid(), outward) > depthOwner) {
      return Verdict::HigherOrderRequired;
    }
  }
  return Verdict::FallbackRequired;
}

// ---------------------------------------------------------------------------
// The pre-DIFF-002 two-point terms, transcribed from the production fallback branch.
// ---------------------------------------------------------------------------
discretization::FaceDiffusionTerms preChangeTerms(const Mesh& mesh, const Face& face, Real distance,
                                                  const VectorField& grad) {
  discretization::FaceDiffusionTerms terms;
  const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
  if (decomposition.valid) {
    terms.coefficient = kGamma * magnitude(decomposition.orthogonal) / distance;
    terms.explicitFlux = kGamma * dot(decomposition.nonOrthogonal, grad[face.owner()]);
  } else {
    terms.coefficient = kGamma * face.area() / distance;
    terms.explicitFlux = 0.0;
  }
  terms.boundaryValueCoefficient = terms.coefficient;
  terms.farCellCoefficient = 0.0;
  return terms;
}

struct Tally {
  std::size_t boundaryFaces{};
  std::size_t oracleHigher{};
  std::size_t oracleFallback{};
  std::size_t productionHigher{};
  std::size_t productionFallback{};
  std::size_t classificationMismatch{};
  std::size_t fallbackBitwiseMismatch{};
  std::size_t higherOrderNotExercised{};
  std::size_t oracleTgCompared{};
  std::size_t oracleTgDisagree{};
  bool pass() const {
    return classificationMismatch == 0 && fallbackBitwiseMismatch == 0 &&
           higherOrderNotExercised == 0 && oracleTgDisagree == 0;
  }
};

// `shape` is supplied only for single-block structured meshes, where Oracle-T applies.
Tally evaluate(const Mesh& mesh, const std::optional<BlockShape>& shape, Mode mode) {
  Tally t;
  VectorField grad(mesh.numberOfCells(), Vector3{});
  for (const auto& cell : mesh.cells()) {
    grad[cell.id()] = Vector3{0.8 + cell.centroid().x, -0.3 * cell.centroid().y, 0.11};
  }

  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      const Face& face = mesh.face(faceId);
      ++t.boundaryFaces;

      // --- the oracles, neither of which consults production ---
      const Verdict g = oracleG(mesh, face);
      Verdict expected = g;
      if (shape.has_value()) {
        if (const auto tv = oracleT(patch.name(), *shape); tv.has_value()) {
          ++t.oracleTgCompared;
          if (*tv != g) ++t.oracleTgDisagree;
          expected = *tv;  // the analytic oracle is authoritative where it applies
        }
      }
      if (expected == Verdict::HigherOrderRequired) {
        ++t.oracleHigher;
      } else {
        ++t.oracleFallback;
      }

      // --- production ---
      const Real distance =
          MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
      const auto terms = boundaryFaceDiffusionTerms(mesh, face, kGamma, distance, &grad, true);
      Verdict actual = terms.higherOrder ? Verdict::HigherOrderRequired : Verdict::FallbackRequired;
      if (mode == Mode::BaselineFallbackEverywhere) {
        actual = Verdict::FallbackRequired;
      } else if (mode == Mode::OverEagerHigherOrderEverywhere) {
        actual = Verdict::HigherOrderRequired;
      }
      if (actual == Verdict::HigherOrderRequired) {
        ++t.productionHigher;
      } else {
        ++t.productionFallback;
      }
      if (actual != expected) {
        ++t.classificationMismatch;
        continue;
      }

      if (expected == Verdict::FallbackRequired) {
        const auto before = preChangeTerms(mesh, face, distance, grad);
        if (!sameBits(terms.coefficient, before.coefficient) ||
            !sameBits(terms.explicitFlux, before.explicitFlux) ||
            !sameBits(terms.boundaryValueCoefficient, before.boundaryValueCoefficient) ||
            !sameBits(terms.farCellCoefficient, before.farCellCoefficient)) {
          ++t.fallbackBitwiseMismatch;
        }
      } else {
        // The reconstruction must actually have been exercised, not silently degraded: a real
        // far-cell coupling, terms that differ from the two-point ones, and cP - cF = cB.
        const auto before = preChangeTerms(mesh, face, distance, grad);
        const Real identity = terms.coefficient - terms.farCellCoefficient;
        const bool exercised = terms.farCellCoefficient != 0.0 &&
                               !sameBits(terms.coefficient, before.coefficient) &&
                               std::abs(identity - terms.boundaryValueCoefficient) <=
                                   1e-12 * std::abs(terms.boundaryValueCoefficient);
        if (!exercised) ++t.higherOrderNotExercised;
      }
    }
  }
  return t;
}

Mode gMode = Mode::Production;
Tally gTotal;

void report(const std::string& name, const Mesh& mesh, const std::optional<BlockShape>& shape) {
  const Tally t = evaluate(mesh, shape, gMode);
  gTotal.boundaryFaces += t.boundaryFaces;
  gTotal.oracleHigher += t.oracleHigher;
  gTotal.oracleFallback += t.oracleFallback;
  gTotal.productionHigher += t.productionHigher;
  gTotal.productionFallback += t.productionFallback;
  gTotal.classificationMismatch += t.classificationMismatch;
  gTotal.fallbackBitwiseMismatch += t.fallbackBitwiseMismatch;
  gTotal.higherOrderNotExercised += t.higherOrderNotExercised;
  gTotal.oracleTgCompared += t.oracleTgCompared;
  gTotal.oracleTgDisagree += t.oracleTgDisagree;
  std::printf(
      "%-38s faces %5zu | oracle HO %5zu FB %5zu | prod HO %5zu FB %5zu | "
      "class-mismatch %4zu | fb-bit-mismatch %4zu | ho-not-exercised %4zu | T/G %zu/%zu | %s\n",
      name.c_str(), t.boundaryFaces, t.oracleHigher, t.oracleFallback, t.productionHigher,
      t.productionFallback, t.classificationMismatch, t.fallbackBitwiseMismatch,
      t.higherOrderNotExercised, t.oracleTgDisagree, t.oracleTgCompared,
      t.pass() ? "PASS" : "FAIL");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = (argc > 1) ? argv[1] : "production";
  if (mode == "baseline") {
    gMode = Mode::BaselineFallbackEverywhere;
  } else if (mode == "overeager") {
    gMode = Mode::OverEagerHigherOrderEverywhere;
  } else if (mode != "production") {
    std::printf("usage: diff2_oracle [production|baseline|overeager]\n");
    return 2;
  }
  std::printf("# MODE %s%s\n", mode.c_str(),
              gMode == Mode::Production
                  ? " (the real production classification)"
                  : " (NEGATIVE CONTROL -- this substitutes a broken library's answer and MUST "
                    "FAIL)");
  std::printf("# P12-DIFF-002 A1 -- W3b-A1 independent-oracle classification agreement\n");
  std::printf("# Oracle-T: analytic from (nx,ny,nz) + patch name, no floating point.\n");
  std::printf("# Oracle-G: connectivity walk by normal DEPTH (production selects by ALIGNMENT).\n");
  std::printf("# T/G column: disagreements / faces where both oracles applied.\n");
  std::printf("# gamma %.6g; fallback comparison is on raw bit patterns.\n\n", kGamma);

  std::printf("## required 2D structured\n");
  for (const auto& [nx, ny] :
       std::vector<std::pair<Index, Index>>{{1, 1}, {8, 1}, {1, 8}, {8, 8}}) {
    report("2D " + std::to_string(nx) + "x" + std::to_string(ny),
           MeshGeometry::createCartesian2D(nx, ny, static_cast<Real>(nx), static_cast<Real>(ny)),
           BlockShape{nx, ny, 1});
  }

  std::printf("\n## required 3D structured\n");
  for (const auto& [nx, ny, nz] : std::vector<std::tuple<Index, Index, Index>>{{1, 1, 1},
                                                                               {8, 1, 1},
                                                                               {1, 8, 1},
                                                                               {1, 1, 8},
                                                                               {8, 8, 1},
                                                                               {8, 1, 8},
                                                                               {1, 8, 8},
                                                                               {8, 8, 8}}) {
    report("3D " + std::to_string(nx) + "x" + std::to_string(ny) + "x" + std::to_string(nz),
           MeshGeometry::createCartesian3D(nx, ny, nz, static_cast<Real>(nx), static_cast<Real>(ny),
                                           static_cast<Real>(nz)),
           BlockShape{nx, ny, nz});
  }

  std::printf("\n## graded and distorted (same topology as the Cartesian block)\n");
  report("graded 2x4 geometric r=2",
         MeshGeometry::createGraded2D(
             2, 4, 1.0, 1.0, mesh::AxisGrading{mesh::GradingType::Uniform, 1.0},
             mesh::AxisGrading{mesh::GradingType::Geometric, 2.0, mesh::GradingCluster::Start}),
         BlockShape{2, 4, 1});
  report("graded 16x16 geometric r=1.2",
         MeshGeometry::createGraded2D(
             16, 16, 1.0, 1.0,
             mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
             mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both}),
         BlockShape{16, 16, 1});
  for (const auto& [label, nx, ny, shear] :
       std::vector<std::tuple<std::string, Index, Index, Real>>{
           {"distorted 16x16 shear 0.20", 16, 16, 0.20},
           {"distorted 16x16 shear 0.45", 16, 16, 0.45},
           {"distorted 8x1 shear 0.20", 8, 1, 0.20},
           {"distorted 1x8 shear 0.20", 1, 8, 0.20}}) {
    std::vector<Vector3> vertices;
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        const Real xi = static_cast<Real>(i) / static_cast<Real>(nx);
        const Real eta = static_cast<Real>(j) / static_cast<Real>(ny);
        // Interior nodes only are displaced, so the boundary patches stay exactly planar and the
        // prescribed boundary values stay exact (the GRAD-002 instrument lesson).
        const bool interior = (i > 0 && i < nx && j > 0 && j < ny);
        const Real dx = interior ? (shear * eta / static_cast<Real>(nx)) : 0.0;
        const Real dy = interior ? (shear * xi / static_cast<Real>(ny)) : 0.0;
        vertices.push_back(Vector3{xi + dx, eta + dy, 0.0});
      }
    }
    report(label, MeshGeometry::createStructuredQuad2D(nx, ny, vertices), BlockShape{nx, ny, 1});
  }

  std::printf("\n## committed production cases (Oracle-G only -- multi-block/curved topology)\n");
  std::vector<std::string> cases;
  for (const auto& entry : std::filesystem::directory_iterator("cases")) {
    if (entry.is_directory()) cases.push_back(entry.path().generic_string());
  }
  std::sort(cases.begin(), cases.end());
  for (const std::string& c : cases) {
    try {
      const io::SimulationSetup setup = io::CaseBuilder{}.build(io::CaseReader{}.read(c));
      report(c, setup.mesh, std::nullopt);
    } catch (const std::exception&) {
      std::printf("%-38s (not buildable here -- skipped)\n", c.c_str());
    }
  }

  std::printf(
      "\nTOTAL boundary faces %zu | oracle HO %zu FB %zu | prod HO %zu FB %zu | "
      "class-mismatch %zu | fb-bit-mismatch %zu | ho-not-exercised %zu | oracle T/G disagree %zu "
      "of %zu\n",
      gTotal.boundaryFaces, gTotal.oracleHigher, gTotal.oracleFallback, gTotal.productionHigher,
      gTotal.productionFallback, gTotal.classificationMismatch, gTotal.fallbackBitwiseMismatch,
      gTotal.higherOrderNotExercised, gTotal.oracleTgDisagree, gTotal.oracleTgCompared);
  // Non-vacuity, stated in the log itself: the mesh set must contain BOTH classes, or 100 %
  // agreement would be unfalsifiable in one direction.
  std::printf("BOTH CLASSES PRESENT: HIGHER_ORDER_REQUIRED %zu, FALLBACK_REQUIRED %zu -> %s\n",
              gTotal.oracleHigher, gTotal.oracleFallback,
              (gTotal.oracleHigher > 0 && gTotal.oracleFallback > 0) ? "yes" : "NO");
  std::printf("W3b-A1 %s\n", gTotal.pass() ? "PASS" : "FAIL");
  return 0;
}
