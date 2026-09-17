// P12-DIFF-002-UC-001 Step 5: cross-check CFDApp against the INDEPENDENTLY derived DIFF-002
// discrete system. Investigation only -- nothing is modified.
//
// The reference (uc001_reference.py) contains no CFDApp code and was completed first. This probe
// compares production against it at two levels:
//
//   LEVEL 1, the OPERATOR, at machine precision. For a channel column the production momentum
//   diffusion assembly is compared ENTRY BY ENTRY against the hand-derived coefficients
//
//       x-internal face   : mu A_x / dx,          A_x = dy
//       y-internal face   : mu A_y / dy,          A_y = dx
//       y-wall (DIFF-002) : cP = 3 mu A_y / dy,   cF = mu A_y / (3 dy),  cB = 8 mu A_y / (3 dy)
//
//   with the assembled row holding -flux_into_owner, so the diagonal accumulates +coefficient, the
//   neighbour -coefficient, and the far cell -cF. Nothing here is read from production first: the
//   expected row is built from dx, dy and mu.
//
//   LEVEL 2, the SOLVED 1D SYSTEM. The same assembly is solved with the flow-rate closure and
//   compared cell by cell against the reference's exact rationals.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::VelocityComponent;

namespace {

constexpr Real kMu = 0.1;
constexpr Real kH = 1.0;

Real storedEntry(const algebra::SparseMatrix& m, Index row, Index col) {
  const Index* off = m.rowOffsetsData();
  for (Index k = off[row]; k < off[row + 1]; ++k) {
    if (m.columnIndicesData()[k] == col) return m.valuesData()[k];
  }
  return 0.0;
}

}  // namespace

int main() {
  // A 3-column channel: the middle column's x-faces are internal, so its rows isolate the
  // y-direction wall treatment. Walls top and bottom, Outlet (gradient-type, never reconstructed)
  // left and right.
  const Index nx = 3;
  const Index ny = 8;
  const Real lx = 3.0 * kH / static_cast<Real>(ny);  // square cells: dx = dy
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, lx, kH);
  const Real dx = lx / static_cast<Real>(nx);
  const Real dy = kH / static_cast<Real>(ny);

  boundary::BoundaryConditionSet bcs;
  bcs.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  bcs.set(mesh, "top", std::make_unique<boundary::Wall>());
  bcs.set(mesh, "left", std::make_unique<boundary::Outlet>());
  bcs.set(mesh, "right", std::make_unique<boundary::Outlet>());

  const Index n = mesh.numberOfCells();
  const fields::VectorField velocity(n, Vector2{0.0, 0.0});
  algebra::SparseMatrixBuilder builder(n, n);
  algebra::Vector rhs(n, 0.0);
  assembleDiffusionContribution(mesh, kMu, velocity, bcs, VelocityComponent::U, builder, rhs);
  const auto matrix = builder.build();

  // Hand-derived coefficients (areas per unit depth). The areas are asserted against the mesh so
  // that a LEVEL 1 mismatch cannot be blamed on this probe's own geometric assumption.
  const Real areaX = dy;   // x-normal face
  const Real areaY = dx;   // y-normal face
  {
    const Index midCell = 1;  // (i=1, j=0)
    Real seenX = -1.0;
    Real seenY = -1.0;
    for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
      const auto& f = mesh.face(faceId);
      const bool touches = f.owner() == midCell || (f.neighbor() && *f.neighbor() == midCell);
      if (!touches) continue;
      if (std::abs(f.areaVector().x) > 0.5 * f.area()) seenX = f.area();
      if (std::abs(f.areaVector().y) > 0.5 * f.area()) seenY = f.area();
    }
    std::printf("# mesh face areas: x-normal %.17g (assumed %.17g)  y-normal %.17g (assumed %.17g)\n",
                seenX, areaX, seenY, areaY);
    if (std::abs(seenX - areaX) > 1e-15 || std::abs(seenY - areaY) > 1e-15) {
      std::printf("  GEOMETRY ASSUMPTION WRONG -- probe invalid, not a production result\n");
      return 2;
    }
    std::printf("# cell volume mesh %.17g  assumed %.17g\n", mesh.cell(midCell).volume(), dx * dy);
  }
  const Real cxInternal = kMu * areaX / dx;
  const Real cyInternal = kMu * areaY / dy;
  const Real cP = 3.0 * kMu * areaY / dy;
  const Real cF = kMu * areaY / (3.0 * dy);
  const Real cB = 8.0 * kMu * areaY / (3.0 * dy);

  std::printf("# UC-001 Step 5 -- production vs the independently derived DIFF-002 operator\n");
  std::printf("# mesh %lldx%lld, dx = dy = %.17g, mu = %.17g\n", static_cast<long long>(nx),
              static_cast<long long>(ny), dx, kMu);
  std::printf("# derived: x-internal %.17g  y-internal %.17g  cP %.17g  cF %.17g  cB %.17g\n",
              cxInternal, cyInternal, cP, cF, cB);
  std::printf("# identity cP - cF - cB = %.3e (must be 0)\n\n", cP - cF - cB);

  // ------------------------------------------------------------------------------------------
  // LEVEL 1: entry-by-entry comparison of the middle column's rows.
  // ------------------------------------------------------------------------------------------
  std::printf("## LEVEL 1 -- assembled row vs hand-derived row, middle column, every entry\n");
  Real worstMatrix = 0.0;
  Real worstRhs = 0.0;
  std::size_t rowsChecked = 0;
  for (Index j = 0; j < ny; ++j) {
    const Index cell = j * nx + 1;  // i = 1, the middle column
    std::map<Index, Real> expected;
    Real expectedRhs = 0.0;

    // x direction: both faces internal (to i = 0 and i = 2).
    expected[cell] += cxInternal;
    expected[cell - 1] -= cxInternal;
    expected[cell] += cxInternal;
    expected[cell + 1] -= cxInternal;

    // y direction.
    if (j == 0) {  // bottom wall + internal face upward
      expected[cell] += cP;
      expected[cell + nx] -= cF;          // the far cell is the second row up
      expectedRhs += cB * 0.0;            // Wall: u_b = 0
      expected[cell] += cyInternal;
      expected[cell + nx] -= cyInternal;
    } else if (j == ny - 1) {  // top wall + internal face downward
      expected[cell] += cP;
      expected[cell - nx] -= cF;
      expectedRhs += cB * 0.0;
      expected[cell] += cyInternal;
      expected[cell - nx] -= cyInternal;
    } else {
      expected[cell] += cyInternal;
      expected[cell - nx] -= cyInternal;
      expected[cell] += cyInternal;
      expected[cell + nx] -= cyInternal;
    }

    Real rowWorst = 0.0;
    for (Index col = 0; col < n; ++col) {
      const Real got = storedEntry(matrix, cell, col);
      const auto it = expected.find(col);
      const Real want = (it == expected.end()) ? 0.0 : it->second;
      rowWorst = std::max(rowWorst, std::abs(got - want));
    }
    const Real dr = std::abs(rhs[cell] - expectedRhs);
    worstMatrix = std::max(worstMatrix, rowWorst);
    worstRhs = std::max(worstRhs, dr);
    ++rowsChecked;
    std::printf("  j=%lld cell %3lld  diag %.17g (derived %.17g)  worst |d entry| %.3e  |d rhs| %.3e\n",
                static_cast<long long>(j), static_cast<long long>(cell),
                storedEntry(matrix, cell, cell), expected[cell], rowWorst, dr);
  }
  std::printf("  rows checked %zu   worst |d matrix| %.3e   worst |d rhs| %.3e\n", rowsChecked,
              worstMatrix, worstRhs);

  // ------------------------------------------------------------------------------------------
  // LEVEL 2: solve the 1D system production assembles, with the flow-rate closure, and compare
  // every cell against the reference's exact rationals (63/43 etc.).
  // ------------------------------------------------------------------------------------------
  std::printf("\n## LEVEL 2 -- the solved 1D profile vs the reference's exact rationals\n");
  // The 1D row for the middle column, divided by (mu*areaY/dy), is what the reference builds:
  //   j=0:   -(cP+cyInternal)/k u_0 + (cF+cyInternal)/k u_1 = G dy    (k = mu*areaY/dy)
  // Solve the (ny+1) system [u_0..u_{ny-1}, G] exactly as the reference does, but with the
  // coefficients READ OUT OF THE PRODUCTION MATRIX -- so agreement tests the operator, not the
  // algebra.
  std::vector<std::vector<Real>> a(ny + 1, std::vector<Real>(ny + 2, 0.0));
  for (Index j = 0; j < ny; ++j) {
    const Index cell = j * nx + 1;
    for (Index k = 0; k < ny; ++k) {
      const Index other = k * nx + 1;
      // Only the y-direction couplings: subtract the x-internal part, which a fully developed
      // column does not see (u is x-independent, so those contributions cancel identically).
      Real v = storedEntry(matrix, cell, other);
      if (other == cell) v -= 2.0 * cxInternal;
      a[j][k] = v;
    }
    // The assembled row equals -mu V grad^2 u, and the momentum balance is mu grad^2 u = dp/dx
    // with dp/dx = mu G, so   row.u + mu V G = 0.
    a[j][ny] = kMu * mesh.cell(cell).volume() * 1.0;
    a[j][ny + 1] = 0.0;
  }
  for (Index k = 0; k < ny; ++k) a[ny][k] = 1.0;
  a[ny][ny] = 0.0;
  a[ny][ny + 1] = static_cast<Real>(ny) * 1.0;  // sum u_j = ny * U, U = 1

  // Gauss-Jordan in double precision.
  const Index m = ny + 1;
  for (Index col = 0; col < m; ++col) {
    Index pivot = col;
    for (Index r = col; r < m; ++r) {
      if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
    }
    std::swap(a[col], a[pivot]);
    const Real inv = 1.0 / a[col][col];
    for (Index c = 0; c <= m; ++c) a[col][c] *= inv;
    for (Index r = 0; r < m; ++r) {
      if (r == col) continue;
      const Real f = a[r][col];
      if (f == 0.0) continue;
      for (Index c = 0; c <= m; ++c) a[r][c] -= f * a[col][c];
    }
  }
  std::vector<Real> u(ny);
  for (Index j = 0; j < ny; ++j) u[j] = a[j][m];
  const Real G = a[ny][m];

  // The reference's exact values (rationals printed as the closest double; see logs/01).
  const Real refU[8] = {15.0 / 43.0, 39.0 / 43.0, 55.0 / 43.0, 63.0 / 43.0,
                        63.0 / 43.0, 55.0 / 43.0, 39.0 / 43.0, 15.0 / 43.0};
  const Real refDpdx = -256.0 / 215.0;
  Real worstCell = 0.0;
  for (Index j = 0; j < ny; ++j) {
    const Real d = std::abs(u[j] - refU[j]);
    worstCell = std::max(worstCell, d);
    std::printf("  j=%lld  production %.15f   reference %.15f   |diff| %.3e\n",
                static_cast<long long>(j), u[j], refU[j], d);
  }
  const Real dpdx = kMu * G;
  std::printf("  dp/dx production %.15f   reference %.15f   |rel diff| %.3e\n", dpdx, refDpdx,
              std::abs(dpdx - refDpdx) / std::abs(refDpdx));
  const Real centre = 0.5 * (u[ny / 2 - 1] + u[ny / 2]);
  std::printf("  centreline production %.15f  reference %.15f (63/43)  |diff| %.3e\n", centre,
              63.0 / 43.0, std::abs(centre - 63.0 / 43.0));
  std::printf("  worst per-cell |diff| %.3e\n", worstCell);

  // ------------------------------------------------------------------------------------------
  // LEVEL 3: MATRIX STRUCTURE -- the far-cell coupling must exist, at the derived column.
  // ------------------------------------------------------------------------------------------
  std::printf("\n## LEVEL 3 -- matrix structure of the two wall rows\n");
  bool structureOk = true;
  for (const Index j : {Index{0}, Index{ny - 1}}) {
    const Index cell = j * nx + 1;
    const Index farDerived = (j == 0) ? cell + nx : cell - nx;
    const Index* off = matrix.rowOffsetsData();
    std::vector<Index> cols;
    for (Index k = off[cell]; k < off[cell + 1]; ++k) {
      if (matrix.valuesData()[k] != 0.0) cols.push_back(matrix.columnIndicesData()[k]);
    }
    const bool hasFar = std::find(cols.begin(), cols.end(), farDerived) != cols.end();
    // Derived stencil: {P, P-1, P+1, far}. The far cell coincides with the y-neighbour, so a wall
    // row has exactly 4 structural nonzeros, one fewer than an interior row's 5.
    const bool count = cols.size() == 4;
    std::printf("  j=%lld cell %3lld  nonzeros %zu (derived 4: %s)  far column %lld present: %s"
                "   entry (P,far) %.17g  derived -(cF + y-internal) %.17g\n",
                static_cast<long long>(j), static_cast<long long>(cell), cols.size(),
                count ? "yes" : "NO", static_cast<long long>(farDerived), hasFar ? "yes" : "NO",
                storedEntry(matrix, cell, farDerived), -(cF + cyInternal));
    structureOk = structureOk && hasFar && count &&
                  std::abs(storedEntry(matrix, cell, farDerived) + (cF + cyInternal)) <= 1e-15;
  }

  // ------------------------------------------------------------------------------------------
  // LEVEL 4: the BOUNDARY-VALUE COEFFICIENT cB, exercised with a NONZERO wall value. With a
  // still wall u_b = 0 the RHS check above is vacuous (cB * 0 == 0 whatever cB is), so cB is
  // re-tested against a moving wall, where the derived RHS is cB * u_b exactly.
  // ------------------------------------------------------------------------------------------
  std::printf("\n## LEVEL 4 -- cB against a NONZERO wall value (the u_b = 0 RHS check is vacuous)\n");
  const Real uWall = 0.375;
  boundary::BoundaryConditionSet moving;
  moving.set(mesh, "bottom", std::make_unique<boundary::MovingWall>(Vector2{uWall, 0.0}));
  moving.set(mesh, "top", std::make_unique<boundary::Wall>());
  moving.set(mesh, "left", std::make_unique<boundary::Outlet>());
  moving.set(mesh, "right", std::make_unique<boundary::Outlet>());
  algebra::SparseMatrixBuilder mb(n, n);
  algebra::Vector mrhs(n, 0.0);
  assembleDiffusionContribution(mesh, kMu, velocity, moving, VelocityComponent::U, mb, mrhs);
  const auto mmat = mb.build();
  const Index bottomMid = 1;
  const Real gotRhs = mrhs[bottomMid];
  const Real wantRhs = cB * uWall;
  std::printf("  u_b %.17g   derived cB %.17g   derived rhs cB*u_b %.17g\n", uWall, cB, wantRhs);
  std::printf("  production rhs %.17g   |diff| %.3e\n", gotRhs, std::abs(gotRhs - wantRhs));
  // Non-vacuity of this check: the two-point coefficient (cB_2pt = 2 mu A/dy) would give a
  // different RHS, so the comparison can actually fail.
  const Real cB2pt = 2.0 * kMu * areaY / dy;
  std::printf("  two-point cB would be %.17g -> rhs %.17g, differing by %.3e (check is non-vacuous)\n",
              cB2pt, cB2pt * uWall, std::abs(cB2pt * uWall - wantRhs));
  const bool cbOk = std::abs(gotRhs - wantRhs) <= 1e-15;
  // The matrix must be unchanged by the wall VALUE (only the RHS moves).
  Real matrixDrift = 0.0;
  for (Index r = 0; r < n; ++r) {
    for (Index c = 0; c < n; ++c) {
      matrixDrift = std::max(matrixDrift, std::abs(storedEntry(mmat, r, c) - storedEntry(matrix, r, c)));
    }
  }
  std::printf("  matrix unchanged by the wall value: worst |diff| %.3e\n", matrixDrift);

  // ------------------------------------------------------------------------------------------
  // LEVEL 5: the WALL CONTRIBUTION (shear) from the converged profile, three ways.
  // ------------------------------------------------------------------------------------------
  std::printf("\n## LEVEL 5 -- wall flux from the solved profile\n");
  // Derived: flux_into_owner = -(cP u_P - cF u_F) + cB u_b, with u_b = 0.
  const Real fluxDerived = -(cP * u[0] - cF * u[1]);
  // The exact continuum wall shear over this face: tau = mu du/dy|_0 = mu * (-G) * H/2, and the
  // face flux is tau * A.
  const Real fluxContinuum = kMu * (-(dpdx / kMu)) * (kH / 2.0) * areaY;
  // The reference's own discrete wall flux, from its exact rationals.
  const Real fluxReference = -(cP * refU[0] - cF * refU[1]);
  std::printf("  derived-from-production-profile %.15f\n", fluxDerived);
  std::printf("  reference (exact rationals)     %.15f   |diff| %.3e\n", fluxReference,
              std::abs(fluxDerived - fluxReference));
  // Magnitudes are compared: fluxDerived is the flux INTO the owner (negative, out of the fluid),
  // fluxContinuum is written as a positive shear magnitude.
  std::printf("  |continuum mu*(dp/dx)*H/2*A|    %.15f   |rel diff of magnitudes| %.3e\n",
              std::abs(fluxContinuum),
              std::abs(std::abs(fluxDerived) - std::abs(fluxContinuum)) / std::abs(fluxContinuum));
  // Global momentum balance: the two wall fluxes must absorb the whole pressure force.
  const Real pressureForce = -dpdx * (kH * areaY);
  const Real wallTotal = 2.0 * std::abs(fluxDerived);
  std::printf("  2*|wall flux| %.15f   pressure force |dp/dx|*H*A %.15f   |rel diff| %.3e\n",
              wallTotal, pressureForce, std::abs(wallTotal - pressureForce) / pressureForce);
  const bool wallOk = std::abs(fluxDerived - fluxReference) <= 1e-14 &&
                      std::abs(wallTotal - pressureForce) / pressureForce <= 1e-14;

  std::printf("\n## VERDICT\n");
  const bool level1 = worstMatrix <= 1e-14 && worstRhs <= 1e-14;
  const bool level2 = worstCell <= 1e-13;
  std::printf("  LEVEL 1 operator entry-by-entry (bound 1e-14):   %s\n", level1 ? "PASS" : "FAIL");
  std::printf("  LEVEL 2 solved profile, every cell (1e-13):      %s\n", level2 ? "PASS" : "FAIL");
  std::printf("  LEVEL 3 matrix structure / far-cell column:      %s\n", structureOk ? "PASS" : "FAIL");
  std::printf("  LEVEL 4 cB with a nonzero wall value (1e-15):    %s\n", cbOk ? "PASS" : "FAIL");
  std::printf("  LEVEL 5 wall flux + global momentum balance:     %s\n", wallOk ? "PASS" : "FAIL");
  const bool all = level1 && level2 && structureOk && cbOk && wallOk && matrixDrift <= 1e-15;
  std::printf("  OVERALL: %s\n", all ? "PRODUCTION MATCHES THE INDEPENDENT REFERENCE"
                                     : "DISAGREEMENT");
  return all ? 0 : 1;
}
