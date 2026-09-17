// P12-GRAD-002-VAL-001 steps 4-7: extended refinement study for the Green-Gauss gradient on the
// DISTORTED mesh family, with the error decomposed into the boundary ring and the interior, and
// with the historical (plain) boundary treatment measured alongside the current one.
//
// Investigation only: no production file and no test is modified here.
//
// ---------------------------------------------------------------------------------------------
// INDEPENDENT DERIVATION (step 4) -- what each treatment must give, derived before measuring.
//
// Green-Gauss:  grad(phi)_P ~= (1/V_P) sum_f phi_f S_f.
//
// INTERIOR cell. Each internal face value is linearly interpolated with the exact error
//     phi(x_f) - phi_interp(x_f) = -1/2 w(1-w) L^2 d2phi/dxi^2 + O(h^3)     [= O(h^2)]
// Opposite faces of a cell carry opposite area vectors S_f, and on a smooth mesh their O(h^2)
// interpolation errors are equal to leading order, so the pair CANCELS in the sum. What survives
// is O(h^3) per face; summed over faces (~h per |S|) and divided by V ~ h^2 this leaves
//     interior gradient error = O(h^2)        -> observed order 2.
//
// BOUNDARY cell, PLAIN (pre-GRAD-002) treatment. The boundary face value is taken from the
// boundary condition and is EXACT (zero error); the opposite interior face is still interpolated
// and still carries its O(h^2) error. The pair no longer cancels. One uncancelled O(h^2) face
// error times |S| ~ h, divided by V ~ h^2, gives
//     boundary-ring gradient error = O(h)     -> observed order 1.
//
// GLOBAL volume-weighted L2 over the whole domain. The ring holds ~4n cells of volume ~h^2, i.e.
// a volume FRACTION ~4h; the interior holds fraction ~1. Hence
//     L2^2 ~ C_i^2 h^4 * 1  +  C_b^2 h^2 * 4h  =  C_i^2 h^4 + 4 C_b^2 h^3
// The h^3 term dominates as h -> 0, so
//     PLAIN global L2 ~ h^{3/2}               -> observed order 3/2 = 1.5.
//
// BOUNDARY cell, GRAD-002 treatment. The correction removes the leading O(h^2) interpolation bias
// from the OPPOSITE face's value, restoring the pairwise cancellation at the boundary too, so the
// ring returns to O(h^2). Then
//     L2^2 ~ C_i^2 h^4 + 4 C_b'^2 h^5  ->  L2 ~ C_i h^2 (1 + O(h))
//     GRAD-002 global L2 ~ h^2                -> observed order 2, APPROACHED FROM BELOW.
//
// PREDICTIONS, to be confirmed or refuted by the numbers below:
//   plain:    ring order ~1,  interior order ~2,  global order -> 1.5
//   GRAD-002: ring order ~2,  interior order ~2,  global order -> 2 from below
// The historical test recorded the plain global rate as "~1.58-1.72" and bounded it by [1.4, 1.9]
// -- consistent with 1.5 plus a pre-asymptotic excess, NOT with any second-order property.
// ---------------------------------------------------------------------------------------------
#include <cmath>
#include <cstdio>
#include <vector>

#include "ManufacturedFields.hpp"
#include "DistortedMesh.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::mesh::Mesh;

namespace {

// The historical / discontinuous control: PLAIN Green-Gauss, exactly the treatment the
// pre-GRAD-002 code used on this mesh family. (On a distorted mesh the old code's aligned
// paired-fit branch never applied -- it required cross(d, S_f) == 0 -- so plain Green-Gauss IS
// the historical behaviour here.) Boundary faces take the exact boundary value, internal faces
// the distance-weighted linear interpolation.
fields::VectorField plainGreenGauss(const Mesh& mesh, const fields::ScalarField& field) {
  fields::VectorField grad(mesh.numberOfCells(), Vector2{0.0, 0.0});
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const auto& face = mesh.face(faceId);
    const Index owner = face.owner();
    if (face.isBoundary()) {
      const Real phiB = cfd::test::phiSmooth(face.centroid());  // exact BC value
      grad[owner] = grad[owner] + face.areaVector() * phiB;
      continue;
    }
    const Index neighbour = *face.neighbor();
    // Distance-weighted linear interpolation to the face centroid.
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

struct Norms {
  Real l1{0.0}, l2{0.0}, linf{0.0};
};

// Volume-weighted norms of |grad - grad_exact| over a selected cell set.
Norms norms(const Mesh& mesh, Index n, const fields::VectorField& grad, int which) {
  // which: 0 = all, 1 = interior only, 2 = boundary ring only
  Real sumAbs = 0.0, sumSq = 0.0, vol = 0.0, maxAbs = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id() % n;
    const Index j = cell.id() / n;
    const bool ring = (i == 0 || i == n - 1 || j == 0 || j == n - 1);
    if (which == 1 && ring) continue;
    if (which == 2 && !ring) continue;
    const Vector2 e = grad[cell.id()] - cfd::test::gradSmooth(cell.centroid());
    const Real m = std::sqrt(dot(e, e));
    sumAbs += m * cell.volume();
    sumSq += dot(e, e) * cell.volume();
    vol += cell.volume();
    maxAbs = std::max(maxAbs, m);
  }
  return {sumAbs / vol, std::sqrt(sumSq / vol), maxAbs};
}

void printSeries(const char* title, const std::vector<Index>& ns, const std::vector<Norms>& v) {
  std::printf("\n%s\n", title);
  std::printf("  %5s %12s %14s %8s %14s %8s %14s %8s\n", "n", "h", "L1", "p(L1)", "L2", "p(L2)",
              "Linf", "p(Linf)");
  for (std::size_t k = 0; k < ns.size(); ++k) {
    const Real h = 1.0 / static_cast<Real>(ns[k]);
    std::printf("  %5lld %12.6g %14.6e", static_cast<long long>(ns[k]), h, v[k].l1);
    if (k == 0) std::printf(" %8s", "--"); else
      std::printf(" %8.4f", std::log(v[k-1].l1 / v[k].l1) / std::log(2.0));
    std::printf(" %14.6e", v[k].l2);
    if (k == 0) std::printf(" %8s", "--"); else
      std::printf(" %8.4f", std::log(v[k-1].l2 / v[k].l2) / std::log(2.0));
    std::printf(" %14.6e", v[k].linf);
    if (k == 0) std::printf(" %8s\n", "--"); else
      std::printf(" %8.4f\n", std::log(v[k-1].linf / v[k].linf) / std::log(2.0));
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<Index> ns = {16, 32, 64, 128, 256};
  if (argc > 1) {
    ns.clear();
    for (int i = 1; i < argc; ++i) ns.push_back(std::atoll(argv[i]));
  }
  std::printf("# P12-GRAD-002-VAL-001 extended refinement, GreenGauss gradient,\n");
  std::printf("# DISTORTED mesh family (amplitude 0.15/n, exactly the test's), phi = sin(pi x) cos(pi y)\n");
  std::printf("# RAW norms first; orders are log2 ratios of successive errors.\n");

  std::vector<Norms> curAll, curInt, curRing, oldAll, oldInt, oldRing;
  for (const Index n : ns) {
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.15 / static_cast<Real>(n)));
    const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
    fields::ScalarField field(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) field[cell.id()] = cfd::test::phiSmooth(cell.centroid());

    const auto gCur = cfd::discretization::gradient(mesh, field, boundaries,
                                                   cfd::discretization::GradientScheme::GreenGauss);
    const auto gOld = plainGreenGauss(mesh, field);
    curAll.push_back(norms(mesh, n, gCur, 0));
    curInt.push_back(norms(mesh, n, gCur, 1));
    curRing.push_back(norms(mesh, n, gCur, 2));
    oldAll.push_back(norms(mesh, n, gOld, 0));
    oldInt.push_back(norms(mesh, n, gOld, 1));
    oldRing.push_back(norms(mesh, n, gOld, 2));
  }

  std::printf("\n================ CURRENT (P12-GRAD-002 boundary-consistent) ================");
  printSeries("GLOBAL (all cells) -- this is what the failing test measures", ns, curAll);
  printSeries("INTERIOR ONLY", ns, curInt);
  printSeries("BOUNDARY RING ONLY", ns, curRing);

  std::printf("\n================ HISTORICAL CONTROL (plain Green-Gauss) ================");
  printSeries("GLOBAL (all cells)", ns, oldAll);
  printSeries("INTERIOR ONLY", ns, oldInt);
  printSeries("BOUNDARY RING ONLY", ns, oldRing);

  std::printf("\n## derivation check (predictions stated in this file's header)\n");
  const auto ord = [](const std::vector<Norms>& v, std::size_t k) {
    return std::log(v[k - 1].l2 / v[k].l2) / std::log(2.0);
  };
  const std::size_t last = ns.size() - 1;
  std::printf("  predicted PLAIN    ring ~1    measured %.4f   (finest pair)\n", ord(oldRing, last));
  std::printf("  predicted PLAIN    global 1.5 measured %.4f\n", ord(oldAll, last));
  std::printf("  predicted GRAD-002 ring ~2    measured %.4f\n", ord(curRing, last));
  std::printf("  predicted GRAD-002 global 2   measured %.4f\n", ord(curAll, last));
  std::printf("  predicted BOTH     interior 2 measured %.4f (current) / %.4f (plain)\n",
              ord(curInt, last), ord(oldInt, last));
  return 0;
}
