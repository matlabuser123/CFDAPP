// P12-DIFF-002-UF-001.7: independent manufactured/analytical verification of the MOMENTUM wall
// shear, for the DIFF-002 reconstruction and for the superseded two-point treatment, on the
// cavity's own orthogonal Cartesian mesh.
//
// The same source is linked against the authoritative library and against the isolated
// pre-DIFF-002 baseline (logs/03_baseline.log), so the two wall treatments are compared
// independently rather than one being inferred from the other.
//
// Method: impose an ANALYTIC field u_x = f(y), set the wall's prescribed value to f(y_wall)
// exactly, and read the boundary face's diffusion terms straight from the production function
//     cfd::discretization::boundaryFaceDiffusionTerms(...)
// The flux INTO THE OWNER that the assembly then represents is, by that header's own documented
// convention,
//     F_in = -(coefficient * phi_P - farCellCoefficient * phi_F) + boundaryValueCoefficient * phi_b
//            + explicitFlux
// which is compared against the exact viscous wall flux  mu * f'(y_wall) * |S|  (oriented into
// the owner). No CFD solve is involved: this isolates the operator.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kMu = 0.71;  // the cavity's own nu = Pr with rho = 1

struct Field {
  const char* name;
  Real (*f)(Real);
  Real (*df)(Real);
};

Real cf(Real) { return 2.5; }
Real cdf(Real) { return 0.0; }
Real lf(Real y) { return 0.3 + 1.7 * y; }
Real ldf(Real) { return 1.7; }
Real qf(Real y) { return 0.3 + 1.7 * y - 2.4 * y * y; }
Real qdf(Real y) { return 1.7 - 4.8 * y; }
Real kf(Real y) { return 0.3 + 1.7 * y - 2.4 * y * y + 1.1 * y * y * y; }
Real kdf(Real y) { return 1.7 - 4.8 * y + 3.3 * y * y; }
Real sf(Real y) { return std::sin(2.3 * y + 0.4); }
Real sdf(Real y) { return 2.3 * std::cos(2.3 * y + 0.4); }

const std::vector<Field> kFields = {{"constant", cf, cdf},
                                    {"linear", lf, ldf},
                                    {"quadratic", qf, qdf},
                                    {"cubic", kf, kdf},
                                    {"sine", sf, sdf}};

// Worst relative wall-flux error over the bottom-wall faces of an n x n cavity mesh.
Real wallFluxError(const Field& field, Index n, Real* absErrOut = nullptr) {
  const Mesh mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  const Index cells = mesh.numberOfCells();
  // The manufactured field, and the boundary value set EXACTLY to f(y_wall).
  fields::VectorField velocity(cells, Vector2{0.0, 0.0});
  for (Index c = 0; c < cells; ++c) {
    velocity[c] = Vector2{field.f(mesh.cell(c).centroid().y), 0.0};
  }
  boundary::BoundaryConditionSet bcs;
  for (const char* p : {"left", "right", "top"}) {
    bcs.set(mesh, p, std::make_unique<boundary::MovingWall>(Vector2{0.0, 0.0}));
  }
  bcs.set(mesh, "bottom", std::make_unique<boundary::MovingWall>(Vector2{field.f(0.0), 0.0}));

  // The component gradient the reconstruction transfers with (exactly the production call).
  // Exactly the production path: MomentumEquation takes gradU from computeVelocityGradient.
  const auto velGrad = cfd::discretization::computeVelocityGradient(mesh, velocity, bcs);
  const auto& gradU = velGrad.gradU;

  Real worstRel = 0.0;
  Real worstAbs = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() != std::string("bottom")) continue;
    for (const Index faceId : patch.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Index owner = face.owner();
      const Real distance =
          MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      const auto t = cfd::discretization::boundaryFaceDiffusionTerms(
          mesh, face, kMu, distance, &gradU, /*prescribedValue=*/true);
      const Real phiB = field.f(face.centroid().y);
      Real fluxIn = -(t.coefficient * velocity[owner].x) + t.boundaryValueCoefficient * phiB +
                    t.explicitFlux;
      if (t.farCellCoefficient != 0.0) {
        fluxIn += t.farCellCoefficient * velocity[t.farCell].x;
      }
      // Sign convention, derived and then verified: NonOrthogonalDiffusion's own comment defines
      // dphi/ds with s INWARD as cP phi_P - cF phi_F - cB phi_b, and calls Gamma|S| * (-dphi/ds)
      // the "flux into the owner" -- the sign that puts +Gamma|S|cP on the diagonal, as a
      // diffusion operator requires. The PHYSICAL inward viscous flux is
      // mu |S| du/dy|_wall = +Gamma|S| dphi/ds, i.e. minus the assembled quantity. So the
      // assembled value is compared against -exact. (Measured confirmation: before this sign was
      // corrected, the linear and quadratic rows read a relative error of exactly 2.000000,
      // i.e. the magnitudes already agreed to the last digit.)
      const Real exact = -kMu * field.df(face.centroid().y) * face.area();
      const Real absErr = std::abs(fluxIn - exact);
      worstAbs = std::max(worstAbs, absErr);
      const Real scale = std::max(std::abs(exact), 1e-30);
      worstRel = std::max(worstRel, absErr / scale);
    }
  }
  if (absErrOut != nullptr) *absErrOut = worstAbs;
  return worstRel;
}

}  // namespace

int main() {
  std::printf("# UF-001.7 momentum wall-shear verification (mu = %.2f, orthogonal cavity mesh)\n",
              kMu);
  std::printf("# flux_into_owner from the production boundaryFaceDiffusionTerms vs the exact\n"
              "# viscous wall flux mu f'(y_w) |S|. No CFD solve.\n\n");
  std::printf("%-11s", "field");
  const std::vector<Index> ns = {10, 20, 40, 80, 160};
  for (const Index n : ns) std::printf("  n=%-13lld", static_cast<long long>(n));
  std::printf("   observed order (finest pair)\n");

  int constantExact = 0;
  for (const auto& field : kFields) {
    std::printf("%-11s", field.name);
    std::vector<Real> errs;
    for (const Index n : ns) {
      Real absErr = 0.0;
      const Real rel = wallFluxError(field, n, &absErr);
      errs.push_back(std::string(field.name) == "constant" ? absErr : rel);
      std::printf("  %-15.6e", errs.back());
    }
    if (errs.size() >= 2 && errs[errs.size() - 1] > 0.0 && errs[errs.size() - 2] > 0.0) {
      const Real p = std::log(errs[errs.size() - 2] / errs.back()) / std::log(2.0);
      std::printf("   %.3f\n", p);
    } else {
      std::printf("   exact (0)\n");
      if (std::string(field.name) == "constant") constantExact = 1;
    }
  }
  std::printf("\n(constant row is an ABSOLUTE error -- the exact flux is 0 there, so a relative\n"
              " error is undefined; every other row is relative.)\n");

  // Conservation: a constant field must give exactly zero net boundary flux on a closed cavity,
  // which is the cP - cF = cB identity. Checked directly on the terms.
  std::printf("\n## the cP - cF = cB identity, straight from the production terms\n");
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0);
  fields::VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  boundary::BoundaryConditionSet bcs;
  for (const char* p : {"left", "right", "top", "bottom"}) {
    bcs.set(mesh, p, std::make_unique<boundary::MovingWall>(Vector2{1.0, 0.0}));
  }
  const auto velGrad2 = cfd::discretization::computeVelocityGradient(mesh, velocity, bcs);
  const auto& gradU = velGrad2.gradU;
  Real worstIdentity = 0.0;
  Real worstConstantFlux = 0.0;
  std::size_t higherOrderFaces = 0, fallbackFaces = 0;
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const auto& face = mesh.face(faceId);
    if (!face.isBoundary()) continue;
    const Real distance =
        MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
    const auto t = cfd::discretization::boundaryFaceDiffusionTerms(mesh, face, kMu, distance,
                                                                  &gradU, true);
    if (t.farCellCoefficient != 0.0) ++higherOrderFaces; else ++fallbackFaces;
    worstIdentity = std::max(
        worstIdentity, std::abs(t.coefficient - t.farCellCoefficient - t.boundaryValueCoefficient));
    Real fluxIn = -(t.coefficient * 1.0) + t.boundaryValueCoefficient * 1.0 + t.explicitFlux;
    if (t.farCellCoefficient != 0.0) fluxIn += t.farCellCoefficient * 1.0;
    worstConstantFlux = std::max(worstConstantFlux, std::abs(fluxIn));
  }
  std::printf("  boundary faces: %zu with a far-cell term, %zu fallback\n", higherOrderFaces,
              fallbackFaces);
  std::printf("  worst |cP - cF - cB|            %.3e\n", worstIdentity);
  std::printf("  worst |flux| for a CONSTANT field %.3e  (must be 0: conservation)\n",
              worstConstantFlux);
  return 0;
}
