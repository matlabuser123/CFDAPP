// P12-DIFF-002 ThermalInterface fix, criteria T5/T6: the CONJUGATE path's per-boundary-face
// classification.
//
// T5 requires that the region-aware assembly takes the higher-order reconstruction exactly where a
// valid opposite interior stencil exists and the historical fallback exactly where it does not,
// agreeing with the independent topology oracle of acceptance_gate_A1.md section 5, with 0
// mismatches; and that on fallback faces the contribution is bitwise the historical two-point one.
// T6 requires one-cell-thick directions (2D 1x1, 8x1, 1x8; 3D 1x1x1, 8x8x1) to be covered.
//
// Oracle-T and Oracle-G are transcribed VERBATIM from the frozen A1 tool
// (results/p12-diff-002/a1/tools/diff2_oracle.cpp). Neither consults production: Oracle-T is an
// integer comparison on the mesh generator's own arguments with no floating point at all, and
// Oracle-G is a connectivity walk selecting by normal DEPTH, whereas production selects the most
// anti-parallel face by ALIGNMENT -- a different rule, so agreement is evidence, not tautology.
//
// What is independent here, and what is not, stated plainly:
//   * the CLASSIFICATION (higher-order vs fallback) comes from the oracles alone and is compared
//     against production's answer -- fully independent;
//   * on a FALLBACK face the expected contribution is the hand-written historical two-point form
//     k*|Sf|/d with cB == cP, cF == 0 and no explicit flux -- fully independent;
//   * on a HIGHER-ORDER face the reconstruction's own analytic invariant cP - cF == cB (scaled by
//     Gamma|S|) and the structural requirement that the far cell is a face-neighbour of the owner
//     strictly deeper along the wall normal are checked independently; the coefficient VALUES are
//     production's, and the full-system comparison therefore proves that the conjugate assembly
//     applies the COMPLETE documented term set per face (diagonal, far-cell entry, prescribed-value
//     multiplier, explicit transfer) -- which is precisely what it failed to do before the fix --
//     rather than re-deriving those values from scratch.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalInterface.hpp"

using namespace cfd;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kConductivity = 2.5;

enum class Verdict { HigherOrderRequired, FallbackRequired };

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// --- Oracle-T: analytic, from the generator's parameters. No floating point. (A1, verbatim) ---
struct BlockShape {
  Index nx{};
  Index ny{};
  Index nz{1};
};

std::optional<int> patchAxis(const std::string& patch) {
  if (patch == "left" || patch == "right" || patch == "xmin" || patch == "xmax") return 0;
  if (patch == "bottom" || patch == "top" || patch == "ymin" || patch == "ymax") return 1;
  if (patch == "zmin" || patch == "zmax") return 2;
  return std::nullopt;
}

std::optional<Verdict> oracleT(const std::string& patch, const BlockShape& shape) {
  const auto axis = patchAxis(patch);
  if (!axis.has_value()) return std::nullopt;
  const Index cellsAcross = (*axis == 0) ? shape.nx : ((*axis == 1) ? shape.ny : shape.nz);
  return (cellsAcross >= 2) ? Verdict::HigherOrderRequired : Verdict::FallbackRequired;
}

// --- Oracle-G: connectivity walk by normal DEPTH. (A1, verbatim) ---
Verdict oracleG(const Mesh& mesh, const Face& boundaryFace) {
  const auto& owner = mesh.cell(boundaryFace.owner());
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

boundary::BoundaryConditionSet allDirichlet(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  Real v = 300.0;
  for (const auto& p : mesh.boundaryPatches()) {
    b.set(mesh, p.name(), std::make_unique<boundary::FixedTemperature>(v));
    v += 20.0;
  }
  return b;
}

Real boundaryValueOf(const Mesh& mesh, const Face& face,
                     const boundary::BoundaryConditionSet& bcs, const fields::ScalarField& t,
                     Real distance) {
  const auto& bc = boundary::boundaryConditionForFace(mesh, face.id(), bcs);
  const auto& scalarBc = dynamic_cast<const boundary::ScalarBoundaryCondition&>(bc);
  return scalarBc.boundaryValue(t[face.owner()], distance);
}

fields::ScalarField field(const Mesh& mesh) {
  fields::ScalarField t(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const auto c = mesh.cell(i).centroid();
    t[i] = 305.0 + 3.0 * c.x - 1.5 * c.y + 0.75 * c.z + 0.5 * c.x * c.y;
  }
  return t;
}

thermal::ThermalRegionMap singleRegion(const Mesh& mesh) {
  return thermal::ThermalRegionMap(
      {thermal::ThermalRegion{"a", thermal::ThermalRegionType::Solid,
                              thermal::ThermalProperties(kConductivity, 1.0)}},
      std::vector<Index>(mesh.numberOfCells(), 0));
}

struct Tally {
  std::size_t boundaryFaces{};
  std::size_t oracleHigher{};
  std::size_t oracleFallback{};
  std::size_t classificationMismatch{};
  std::size_t oracleTgCompared{};
  std::size_t oracleTgDisagree{};
  std::size_t fallbackNotBitwiseHistorical{};
  std::size_t farCellNotANeighbour{};
  std::size_t farCellNotDeeper{};
  Real worstIdentity{};   // |(cP - cF) - cB| / cB on higher-order faces
  Real worstSystem{};     // worst |assembled - independently expected| over the whole system
  std::size_t systemEntriesCompared{};
};

Real storedEntry(const algebra::SparseMatrix& m, Index row, Index col) {
  const Index* off = m.rowOffsetsData();
  for (Index k = off[row]; k < off[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == col) return m.valuesData()[k];
  }
  return 0.0;
}

Tally evaluate(const std::string& label, const Mesh& mesh, const std::optional<BlockShape>& shape) {
  Tally t;
  const auto bcs = allDirichlet(mesh);
  const auto temp = field(mesh);
  const auto regions = singleRegion(mesh);

  // The gradient the conjugate assembly itself builds, obtained through the same production entry
  // point it uses, so the per-face terms below are the ones it actually assembled with.
  const VectorField gradT = thermal::thermalBoundaryCorrectionGradient(mesh, temp, bcs);

  // Independently accumulated expected system.
  std::map<std::pair<Index, Index>, Real> expectedA;
  std::vector<Real> expectedB(mesh.numberOfCells(), 0.0);

  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      const Face& face = mesh.face(faceId);
      ++t.boundaryFaces;
      const Index owner = face.owner();

      // --- the oracles, neither of which consults production ---
      const Verdict g = oracleG(mesh, face);
      Verdict expected = g;
      if (shape.has_value()) {
        if (const auto tv = oracleT(patch.name(), *shape); tv.has_value()) {
          ++t.oracleTgCompared;
          if (*tv != g) ++t.oracleTgDisagree;
          expected = *tv;  // analytic oracle authoritative where it applies
        }
      }
      (expected == Verdict::HigherOrderRequired) ? ++t.oracleHigher : ++t.oracleFallback;

      // --- production's answer for the conjugate path's own inputs ---
      const Real distance = MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      const auto terms = discretization::boundaryFaceDiffusionTerms(mesh, face, kConductivity,
                                                                    distance, &gradT, true);
      if (terms.higherOrder != (expected == Verdict::HigherOrderRequired)) {
        ++t.classificationMismatch;
        std::printf("    MISMATCH face %lld patch %s: oracle=%s production=%s\n",
                    static_cast<long long>(faceId), patch.name().c_str(),
                    expected == Verdict::HigherOrderRequired ? "higher" : "fallback",
                    terms.higherOrder ? "higher" : "fallback");
      }

      if (expected == Verdict::FallbackRequired) {
        // Independent expectation: the historical two-point form, hand-written.
        const Real c = kConductivity * face.area() / distance;
        const bool ok = sameBits(terms.coefficient, c) &&
                        sameBits(terms.boundaryValueCoefficient, c) &&
                        terms.farCellCoefficient == 0.0 && terms.explicitFlux == 0.0;
        if (!ok) {
          ++t.fallbackNotBitwiseHistorical;
          std::printf("    FALLBACK NOT HISTORICAL face %lld: cP %a (expected %a) cB %a cF %a"
                      " explicit %a\n",
                      static_cast<long long>(faceId), terms.coefficient, c,
                      terms.boundaryValueCoefficient, terms.farCellCoefficient, terms.explicitFlux);
        }
        expectedA[{owner, owner}] += c;
        expectedB[owner] += c * boundaryValueOf(mesh, face, bcs, temp, distance);
      } else {
        // Structural checks on the chosen stencil, independent of production's selection rule.
        bool isNeighbour = false;
        for (const Index fid : mesh.cell(owner).faceIds()) {
          const Face& cand = mesh.face(fid);
          if (cand.isBoundary()) continue;
          const Index far =
              (cand.owner() == owner) ? *cand.neighbor() : cand.owner();
          if (far == terms.farCell) isNeighbour = true;
        }
        if (!isNeighbour) ++t.farCellNotANeighbour;
        const Vector3& outward = face.areaVector();
        const Real depthOwner = dot(face.centroid() - mesh.cell(owner).centroid(), outward);
        const Real depthFar = dot(face.centroid() - mesh.cell(terms.farCell).centroid(), outward);
        if (!(depthFar > depthOwner)) ++t.farCellNotDeeper;

        // The reconstruction's analytic invariant: cP - cF == cB.
        const Real identity = std::abs((terms.coefficient - terms.farCellCoefficient) -
                                       terms.boundaryValueCoefficient) /
                              std::abs(terms.boundaryValueCoefficient);
        t.worstIdentity = std::max(t.worstIdentity, identity);

        expectedA[{owner, owner}] += terms.coefficient;
        expectedA[{owner, terms.farCell}] += -terms.farCellCoefficient;
        expectedB[owner] += terms.boundaryValueCoefficient *
                                boundaryValueOf(mesh, face, bcs, temp, distance) +
                            terms.explicitFlux;
      }
    }
  }

  // Internal faces, single region: the documented same-region coefficient, hand-written.
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Index P = face.owner();
    const Index N = *face.neighbor();
    const Real c = kConductivity * face.area() / MeshGeometry::ownerNeighborDistance(mesh, face);
    expectedA[{P, P}] += c;
    expectedA[{P, N}] += -c;
    expectedA[{N, N}] += c;
    expectedA[{N, P}] += -c;
  }

  // Compare the assembled conjugate system against the independently accumulated one.
  // Relative to the system's own scale: the independent accumulation sums the same terms in a
  // different order, so the floor here is floating-point rounding, not exact zero.
  const auto conj = thermal::assembleConjugateConductionEquation(mesh, temp, regions, bcs);
  Real scaleA = 0.0;
  Real scaleB = 0.0;
  for (const auto& [key, value] : expectedA) scaleA = std::max(scaleA, std::abs(value));
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    scaleB = std::max(scaleB, std::abs(expectedB[i]));
  }
  for (const auto& [key, value] : expectedA) {
    const Real got = storedEntry(conj.system.matrix(), key.first, key.second);
    t.worstSystem = std::max(t.worstSystem, std::abs(got - value) / std::max(scaleA, 1e-300));
    ++t.systemEntriesCompared;
  }
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    t.worstSystem =
        std::max(t.worstSystem,
                 std::abs(conj.system.rhs()[i] - expectedB[i]) / std::max(scaleB, 1e-300));
    ++t.systemEntriesCompared;
  }

  std::printf("%-24s faces %4zu | oracle H/F %4zu/%4zu | class mismatch %zu | T-vs-G %zu/%zu"
              " | fallback!=historical %zu | farcell bad %zu/%zu | identity %.2e | system %.2e\n",
              label.c_str(), t.boundaryFaces, t.oracleHigher, t.oracleFallback,
              t.classificationMismatch, t.oracleTgDisagree, t.oracleTgCompared,
              t.fallbackNotBitwiseHistorical, t.farCellNotANeighbour, t.farCellNotDeeper,
              t.worstIdentity, t.worstSystem);
  return t;
}

Mesh gradedMesh() {
  return MeshGeometry::createGraded2D(
      12, 12, 1.0, 1.0,
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both});
}

Mesh distortedMesh() {
  constexpr Index n = 12;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? 0.45 * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? 0.45 * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

}  // namespace

int main() {
  std::printf("# T5/T6: conjugate-path boundary classification vs the frozen A1 topology oracle.\n");
  std::printf("# Oracle-T: integer comparison on (nx,ny,nz) + patch name, no floating point.\n");
  std::printf("# Oracle-G: connectivity walk by normal DEPTH (production selects by ALIGNMENT).\n");
  std::printf("# 'system' is the worst RELATIVE difference between the assembled conjugate system\n");
  std::printf("# and one accumulated independently, face by face, from the oracle's verdicts.\n\n");

  Tally total;
  auto add = [&total](const Tally& t) {
    total.boundaryFaces += t.boundaryFaces;
    total.oracleHigher += t.oracleHigher;
    total.oracleFallback += t.oracleFallback;
    total.classificationMismatch += t.classificationMismatch;
    total.oracleTgCompared += t.oracleTgCompared;
    total.oracleTgDisagree += t.oracleTgDisagree;
    total.fallbackNotBitwiseHistorical += t.fallbackNotBitwiseHistorical;
    total.farCellNotANeighbour += t.farCellNotANeighbour;
    total.farCellNotDeeper += t.farCellNotDeeper;
    total.worstIdentity = std::max(total.worstIdentity, t.worstIdentity);
    total.worstSystem = std::max(total.worstSystem, t.worstSystem);
    total.systemEntriesCompared += t.systemEntriesCompared;
  };

  std::printf("## T4/T5 geometries\n");
  add(evaluate("Cartesian 2x1", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0),
               BlockShape{2, 1, 1}));
  add(evaluate("Cartesian 10x10", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0),
               BlockShape{10, 10, 1}));
  add(evaluate("Cartesian 20x20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0),
               BlockShape{20, 20, 1}));
  add(evaluate("Cartesian 3D 8x8x8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0),
               BlockShape{8, 8, 8}));
  add(evaluate("graded 12x12 r=1.2", gradedMesh(), BlockShape{12, 12, 1}));
  add(evaluate("distorted 12x12", distortedMesh(), BlockShape{12, 12, 1}));

  std::printf("\n## T6 one-cell-thick directions (fallback exercised)\n");
  add(evaluate("Cartesian 1x1", MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0),
               BlockShape{1, 1, 1}));
  add(evaluate("Cartesian 8x1", MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0),
               BlockShape{8, 1, 1}));
  add(evaluate("Cartesian 1x8", MeshGeometry::createCartesian2D(1, 8, 1.0, 8.0),
               BlockShape{1, 8, 1}));
  add(evaluate("Cartesian 3D 1x1x1", MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0),
               BlockShape{1, 1, 1}));
  add(evaluate("Cartesian 3D 8x8x1", MeshGeometry::createCartesian3D(8, 8, 1, 8.0, 8.0, 1.0),
               BlockShape{8, 8, 1}));

  std::printf("\n## totals\n");
  std::printf("  boundary faces classified            %zu\n", total.boundaryFaces);
  std::printf("  oracle higher-order / fallback       %zu / %zu\n", total.oracleHigher,
              total.oracleFallback);
  std::printf("  classification mismatches            %zu   (T5 requires 0)\n",
              total.classificationMismatch);
  std::printf("  Oracle-T vs Oracle-G disagreements   %zu of %zu\n", total.oracleTgDisagree,
              total.oracleTgCompared);
  std::printf("  fallback faces not bitwise historical %zu   (T5 requires 0)\n",
              total.fallbackNotBitwiseHistorical);
  std::printf("  far cell not a face-neighbour        %zu\n", total.farCellNotANeighbour);
  std::printf("  far cell not strictly deeper         %zu\n", total.farCellNotDeeper);
  std::printf("  worst |(cP-cF)-cB|/cB                %.3e\n", total.worstIdentity);
  std::printf("  worst |assembled - independent| rel  %.3e over %zu entries\n", total.worstSystem,
              total.systemEntriesCompared);

  const bool pass = total.classificationMismatch == 0 && total.fallbackNotBitwiseHistorical == 0 &&
                    total.farCellNotANeighbour == 0 && total.farCellNotDeeper == 0 &&
                    total.oracleTgDisagree == 0 && total.oracleHigher > 0 &&
                    total.oracleFallback > 0 && total.worstIdentity <= 1e-14 &&
                    total.worstSystem <= 1e-13;
  std::printf("\nT5/T6 RESULT: %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
