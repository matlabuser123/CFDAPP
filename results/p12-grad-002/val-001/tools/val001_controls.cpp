// P12-GRAD-002-VAL-001 step 7: non-vacuity controls for the PROPOSED replacement band, evaluated
// on the test's OWN grid sizes {8, 16, 32, 64} and its own distorted mesh family.
//
// Proposed criterion (derived in val001_refinement.cpp and in the gate, NOT chosen to fit):
//   the global volume-weighted L2 gradient error must DECREASE at every refinement, and every
//   pairwise observed order must lie in [1.875, 2.15].
//
// Derivation of the band. The corrected treatment gives global L2 = C_i h^2 sqrt(1 + k h), so the
// pairwise order is p(n) = 2 - c/n + O(1/n^2). Fitting c to the measured deficits gives
//   c = 0.515, 0.494, 0.512, 0.525, 0.538 over pairs (8,16) ... (128,256)  -> c ~= 0.5, stable.
// Using a 2x safety factor, c_max = 1.0, the coarsest pair the test uses (n = 8) predicts
//   p >= 2 - 1.0/8 = 1.875.
// The order approaches 2 strictly FROM BELOW, so the upper bound only has to reject a spurious
// super-convergence artifact: 2 + 0.15 = 2.15.
//
// Controls that must be REJECTED, per the authorization:
//   (a) old / discontinuous boundary treatment      -> plain Green-Gauss
//   (b) deliberately first-order boundary treatment -> boundary face takes the OWNER CELL value
//   (c) incorrect boundary coefficient / sign       -> correction applied with the wrong sign
//   (d) loss of expected convergence                -> boundary value perturbed by O(h)
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ManufacturedFields.hpp"
#include "DistortedMesh.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::mesh::Mesh;

namespace {

const std::vector<Index> kGridSizes = {8, 16, 32, 64};  // exactly the test's
constexpr Real kLo = 1.875;
constexpr Real kHi = 2.15;

enum class Variant { Current, PlainGG, BoundaryOwnerValue, BoundaryPerturbedOh,
                     BoundarySignFlip };

// Green-Gauss with a selectable boundary face value -- the control knob.
fields::VectorField variantGradient(const Mesh& mesh, const fields::ScalarField& field,
                                    Variant v) {
  fields::VectorField grad(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const Real h = 1.0 / std::sqrt(static_cast<Real>(mesh.numberOfCells()));
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const auto& face = mesh.face(faceId);
    const Index owner = face.owner();
    if (face.isBoundary()) {
      Real phiB = cfd::test::phiSmooth(face.centroid());
      if (v == Variant::BoundaryOwnerValue) {
        phiB = field[owner];  // (b) first-order boundary value
      } else if (v == Variant::BoundaryPerturbedOh) {
        phiB += 0.25 * h;  // (d) an O(h) boundary-value error
      }
      if (v == Variant::BoundarySignFlip) {
        grad[owner] = grad[owner] - face.areaVector() * phiB;  // (c) wrong boundary sign
      } else {
        grad[owner] = grad[owner] + face.areaVector() * phiB;
      }
      continue;
    }
    const Index neighbour = *face.neighbor();
    const Real dP = magnitude(face.centroid() - mesh.cell(owner).centroid());
    const Real dN = magnitude(face.centroid() - mesh.cell(neighbour).centroid());
    const Real w = dN / (dP + dN);
    const Real phiF = w * field[owner] + (1.0 - w) * field[neighbour];
    grad[owner] = grad[owner] + face.areaVector() * phiF;
    grad[neighbour] = grad[neighbour] - face.areaVector() * phiF;
  }
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    grad[c] = grad[c] * (1.0 / mesh.cell(c).volume());
  }
  return grad;
}

Real globalL2(const Mesh& mesh, const fields::VectorField& grad) {
  Real sumSq = 0.0, vol = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2 e = grad[cell.id()] - cfd::test::gradSmooth(cell.centroid());
    sumSq += dot(e, e) * cell.volume();
    vol += cell.volume();
  }
  return std::sqrt(sumSq / vol);
}

// Evaluates the PROPOSED criterion exactly as the amended test would.
bool evaluate(const char* label, const std::vector<Real>& errs, bool expectPass) {
  bool decreasing = true, inBand = true;
  std::printf("  %-42s", label);
  for (std::size_t i = 0; i < errs.size(); ++i) std::printf(" %11.4e", errs[i]);
  std::printf("\n%46s", "orders:");
  for (std::size_t i = 1; i < errs.size(); ++i) {
    if (!(errs[i] < errs[i - 1])) decreasing = false;
    const Real p = std::log(errs[i - 1] / errs[i]) / std::log(2.0);
    if (!(p > kLo && p < kHi)) inBand = false;
    std::printf(" %11.4f", p);
  }
  const bool pass = decreasing && inBand;
  std::printf("   -> %s (expected %s)%s\n", pass ? "PASS" : "FAIL", expectPass ? "PASS" : "FAIL",
              pass == expectPass ? "" : "   <<< UNEXPECTED");
  return pass == expectPass;
}

}  // namespace

int main() {
  std::printf("# VAL-001 non-vacuity controls -- proposed band [%.3f, %.3f] on the test's own\n",
              kLo, kHi);
  std::printf("# grids {8,16,32,64} and its own distorted family. Errors are global volume-\n");
  std::printf("# weighted L2 of |grad - grad_exact|.\n\n");
  std::printf("  %-42s %11s %11s %11s %11s\n", "variant", "n=8", "n=16", "n=32", "n=64");

  std::vector<Real> cur, plain, ownerVal, perturbed, signFlip;
  for (const Index n : kGridSizes) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.15 / static_cast<Real>(n)));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
    fields::ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
    cur.push_back(globalL2(mesh, cfd::discretization::gradient(
        mesh, field, boundaries, cfd::discretization::GradientScheme::GreenGauss)));
    plain.push_back(globalL2(mesh, variantGradient(mesh, field, Variant::PlainGG)));
    ownerVal.push_back(globalL2(mesh, variantGradient(mesh, field, Variant::BoundaryOwnerValue)));
    perturbed.push_back(globalL2(mesh, variantGradient(mesh, field, Variant::BoundaryPerturbedOh)));
    signFlip.push_back(globalL2(mesh, variantGradient(mesh, field, Variant::BoundarySignFlip)));
  }

  int bad = 0;
  bad += !evaluate("CURRENT (GRAD-002 boundary-consistent)", cur, true);
  bad += !evaluate("(a) old/discontinuous: plain Green-Gauss", plain, false);
  bad += !evaluate("(b) first-order boundary value (owner)", ownerVal, false);
  bad += !evaluate("(c) wrong boundary coefficient SIGN", signFlip, false);
  bad += !evaluate("(d) O(h) boundary-value perturbation", perturbed, false);

  std::printf("\n## the SUPERSEDED band [1.4, 1.9] evaluated on the same variants\n");
  const auto old = [](const char* label, const std::vector<Real>& errs) {
    bool ok = true;
    std::printf("  %-42s orders:", label);
    for (std::size_t i = 1; i < errs.size(); ++i) {
      const Real p = std::log(errs[i - 1] / errs[i]) / std::log(2.0);
      if (!(p > 1.4 && p < 1.9)) ok = false;
      std::printf(" %8.4f", p);
    }
    std::printf("   -> %s\n", ok ? "PASS" : "FAIL");
  };
  old("CURRENT (correct second-order boundary)", cur);
  old("(a) old/discontinuous: plain Green-Gauss", plain);
  old("(b) first-order boundary value (owner)", ownerVal);
  std::printf("\n  The superseded band ACCEPTS the old first-order-ring treatment and REJECTS the\n");
  std::printf("  corrected one: it pins the limitation rather than any correctness property.\n");

  std::printf("\n## controls behaved as pre-registered: %s\n", bad == 0 ? "YES" : "NO");
  return bad == 0 ? 0 : 1;
}
