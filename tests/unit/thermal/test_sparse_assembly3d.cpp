// P12-MESH-005 -- the production implicit scalar-transport assembly
// (thermal::assembleEnergyEquation: diffusion + upwind convection + source)
// on tiny 3D hexahedral meshes, checked entry by entry against matrices
// derived BY HAND (results/p12-mesh-005/acceptance_gate.md, items D1-D3).
//
// Conventions (EnergyEquation.hpp): an internal face couples owner P and
// neighbour N with D_f = k A_f / d_PN (+D_f on both diagonals, -D_f off the
// diagonal); a FixedValue face adds D_b = k A_b / d_Pb to the diagonal and
// D_b T_b to the right-hand side (d_Pb = half the cell size); a
// FixedGradient(0) face adds nothing; upwind convection adds cp * mdot to the
// upwind cell's diagonal, -cp * mdot to the downwind row, and moves an inflow
// face's cp * mdot * T_b to the right-hand side; the source adds Q V_P.
// k = 2, cp = 1, Q = 5 throughout; T_b = 1 on every FixedValue patch.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kConductivity = 2.0;
constexpr Real kSpecificHeat = 1.0;
constexpr Real kSource = 5.0;

struct Expected {
  std::vector<Index> rowOffsets;
  std::vector<Index> columns;
  std::vector<Real> values;
  std::vector<Real> rhs;
};

BoundaryConditionSet allFixedValue(const Mesh& mesh, Real value) {
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(value));
  }
  return bcs;
}

cfd::thermal::EnergyAssembly assemble(const Mesh& mesh, const SurfaceField& flux,
                                      const BoundaryConditionSet& bcs) {
  return cfd::thermal::assembleEnergyEquation(
      mesh, ScalarField(mesh.numberOfCells(), 0.0), flux,
      cfd::thermal::ThermalProperties(kConductivity, kSpecificHeat), bcs, kSource);
}

void expectSystem(const cfd::thermal::EnergyAssembly& a, const Expected& e) {
  const auto& m = a.system.matrix();
  const Index n = e.rhs.size();
  ASSERT_EQ(m.rows(), n);
  ASSERT_EQ(m.columns(), n);
  ASSERT_EQ(m.nonZeros(), e.values.size());
  // D1: the sparsity pattern, exactly.
  for (Index r = 0; r <= n; ++r) EXPECT_EQ(m.rowOffsetsData()[r], e.rowOffsets[r]) << "row " << r;
  for (Index k = 0; k < m.nonZeros(); ++k) {
    EXPECT_EQ(m.columnIndicesData()[k], e.columns[k]) << "entry " << k;
    EXPECT_LT(m.columnIndicesData()[k], n);
  }
  // D2: every coefficient and right-hand-side value.
  for (Index k = 0; k < m.nonZeros(); ++k) {
    EXPECT_LE(std::abs(m.valuesData()[k] - e.values[k]), 1e-13 * std::abs(e.values[k]))
        << "entry " << k << ": " << m.valuesData()[k] << " vs " << e.values[k];
  }
  for (Index r = 0; r < n; ++r) {
    EXPECT_LE(std::abs(a.system.rhs()[r] - e.rhs[r]), 1e-13 * std::abs(e.rhs[r]))
        << "rhs " << r << ": " << a.system.rhs()[r] << " vs " << e.rhs[r];
  }
}

void expectDeterministic(const Mesh& mesh, const SurfaceField& flux,
                         const BoundaryConditionSet& bcs) {
  const auto a = assemble(mesh, flux, bcs);
  const auto b = assemble(mesh, flux, bcs);
  const auto& ma = a.system.matrix();
  const auto& mb = b.system.matrix();
  ASSERT_EQ(ma.nonZeros(), mb.nonZeros());
  for (Index k = 0; k < ma.nonZeros(); ++k) {
    EXPECT_EQ(ma.columnIndicesData()[k], mb.columnIndicesData()[k]);
    EXPECT_EQ(ma.valuesData()[k], mb.valuesData()[k]);
  }
  for (Index r = 0; r <= ma.rows(); ++r) EXPECT_EQ(ma.rowOffsetsData()[r], mb.rowOffsetsData()[r]);
  for (Index r = 0; r < ma.rows(); ++r) EXPECT_EQ(a.system.rhs()[r], b.system.rhs()[r]);
}

}  // namespace

// M1, one unit cube: six boundary faces, A = 1, d = 0.5 -> D_b = 2 * 1 / 0.5 = 4.
// diag = 6 * 4 = 24; rhs = 6 * 4 * 1 + 5 * 1 = 29.
//
// P12-DIFF-002 A5 negative control (acceptance_gate_A5.md section 4, criterion A5-f): every axis of
// this mesh is one cell thick, so NO wall has a second cell inward and every one of the six faces
// legitimately keeps the historical two-point form. These values are therefore unchanged by
// DIFF-002 and must keep passing exactly as they are -- which is what proves the amended values in
// the tests below were derived per-face from the stencil, not applied as a blanket factor.
TEST(SparseAssembly3DTest, OneCell) {
  const Mesh mesh = MeshGeometry::createCartesian3D(1, 1, 1, 1.0, 1.0, 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const auto bcs = allFixedValue(mesh, 1.0);
  expectSystem(assemble(mesh, noFlux, bcs), Expected{{0, 1}, {0}, {24.0}, {29.0}});
  expectDeterministic(mesh, noFlux, bcs);
}

// M2, 2 x 1 x 1 on the unit cube: dx = 0.5, dy = dz = 1, V = 0.5.
//   internal x-face: A = 1, d = 0.5  -> D = 4
//   y / z faces:     A = 0.5, d = 0.5 -> D = 2 (four per cell)
//
// P12-DIFF-002 A5, entry 9 (validation-migration/acceptance_gate_A5.md). The xmin/xmax walls are
// normal to the x axis, which has 2 cells, so a second cell exists inward and the second-order
// one-sided reconstruction applies there; the y and z axes are one cell thick, so those four walls
// keep the historical two-point form exactly. Derived independently in
// a5/tools/derive_expected.py, block C1, with k = 2, h1 = dx/2 = 0.25, h2 = 3dx/2 = 0.75, A = 1:
//   cP = k A h2/(h1 (h2-h1)) = 2 * 0.75/(0.25*0.5) = 12      (two-point would be k A/h1 = 8)
//   cF = k A h1/(h2 (h2-h1)) = 2 * 0.25/(0.75*0.5) = 4/3
//   cB = k A (1/h1 + 1/h2)   = 2 * (4 + 4/3)       = 32/3    and cP - cF = cB
// Each cell: diag = cP + 4 * 2 + 4 = 24; the far cell is reached through the shared internal face,
// so the off-diagonal is -(4 + cF) = -16/3; rhs = cB + 4 * 2 * 1 + 5 * 0.5.
TEST(SparseAssembly3DTest, TwoCellsInX) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const auto bcs = allFixedValue(mesh, 1.0);
  const Real diag = 12.0 + 8.0 + 4.0;             // cP + four fallback y/z walls + internal
  const Real off = -(4.0 + 4.0 / 3.0);            // internal + far-cell coefficient
  const Real rhs = 32.0 / 3.0 + 8.0 + 5.0 * 0.5;  // cB + fallback walls + source
  expectSystem(assemble(mesh, noFlux, bcs),
               Expected{{0, 2, 4}, {0, 1, 0, 1}, {diag, off, off, diag}, {rhs, rhs}});
  expectDeterministic(mesh, noFlux, bcs);
}

// M2 with a uniform +x mass flux of 3 per unit area (x-faces: mdot = 3 * A = 3
// owner-oriented, i.e. -3 on the xmin face whose area vector points -x;
// y/z faces 0) and FixedGradient(0) on xmax (FixedValue 1 elsewhere).
// P12-DIFF-002 A5, entry 10. Only cell 0's xmin wall is value-prescribing AND on the 2-cell x axis,
// so it alone is reconstructed (cP = 12, cF = 4/3, cB = 32/3 -- see TwoCellsInX above). Cell 1's
// xmax face is FixedGradient(0): a gradient-type condition is never reconstructed and a zero
// prescribed gradient contributes nothing, so EVERY value in cell 1's row is unchanged from the
// historical operator. That makes this test its own negative control -- derived in
// a5/tools/derive_expected.py, block C4.
//   cell 0: diffusion diag cP + 8 (y/z fallback) + 4 (internal) = 24, rhs cB + 8; convection: the
//           internal face (owner 0, mdot 3 >= 0) adds +3 to A(0,0) and -3 to
//           A(1,0); the xmin inflow adds -(-3) * 1 = +3 to rhs(0).
//           -> A(0,0) = 27, A(0,1) = -(4 + 4/3) = -16/3, rhs = cB + 8 + 3 + 2.5
//   cell 1: diffusion diag 4 + 0 (zero-gradient xmax) + 8 = 12, rhs 8; the xmax
//           outflow (mdot 3) adds +3 to A(1,1).
//           -> A(1,0) = -4 - 3 = -7, A(1,1) = 15, rhs = 8 + 2.5 = 10.5   (all UNCHANGED)
TEST(SparseAssembly3DTest, TwoCellsWithUpwindConvectionAndAZeroGradientOutlet) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 1, 1, 1.0, 1.0, 1.0);
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = 3.0 * face.areaVector().x;  // rho (u . Sf) with u = (3, 0, 0)
  }
  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() == "xmax") {
      bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    } else {
      bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(1.0));
    }
  }
  expectSystem(assemble(mesh, flux, bcs),
               Expected{{0, 2, 4},
                        {0, 1, 0, 1},
                        {12.0 + 8.0 + 4.0 + 3.0, -(4.0 + 4.0 / 3.0), -7.0, 15.0},
                        {32.0 / 3.0 + 8.0 + 3.0 + 2.5, 10.5}});
  expectDeterministic(mesh, flux, bcs);
}

// M3, 2 x 2 x 1: dx = dy = 0.5, dz = 1, V = 0.25.
//   internal x- and y-faces: A = 0.5, d = 0.5 -> D = 2
//   z faces:                 A = 0.25, d = 0.5 -> D = 1 (two per cell)
// Cells 0 (i0 j0), 1 (i1 j0), 2 (i0 j1), 3 (i1 j1).
//
// P12-DIFF-002 A5, entry 11. The x and y axes both have 2 cells, so both of each cell's x/y walls
// are reconstructed; the z axis is one cell thick, so its two walls fall back. Derived in
// a5/tools/derive_expected.py, block C2, with k = 2, A = 0.5, h1 = 0.25, h2 = 0.75:
//   cP = 2 * 0.5 * 0.75/(0.25*0.5) = 6      (two-point would be 4)
//   cF = 2 * 0.5 * 0.25/(0.75*0.5) = 2/3
//   cB = 2 * 0.5 * (4 + 4/3)       = 16/3   and cP - cF = cB
// Every cell is a corner: diag = 2*cP + 2 (internal x and y) + 2*1 (z fallback) = 18; each
// off-diagonal carries the internal coupling plus that row's far-cell coefficient, -(2 + 2/3);
// rhs = 2*cB + 2*1*1 + 5*0.25.
TEST(SparseAssembly3DTest, TwoByTwoByOne) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 2, 1, 1.0, 1.0, 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const auto bcs = allFixedValue(mesh, 1.0);
  const Real diag = 2.0 * 6.0 + 2.0 * 2.0 + 2.0 * 1.0;   // 18
  const Real off = -(2.0 + 2.0 / 3.0);                   // -8/3
  const Real rhs = 2.0 * 16.0 / 3.0 + 2.0 + 5.0 * 0.25;  // 32/3 + 3.25
  expectSystem(assemble(mesh, noFlux, bcs),
               Expected{{0, 3, 6, 9, 12},
                        {0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3},
                        {diag, off, off, off, diag, off, off, diag, off, off, off, diag},
                        {rhs, rhs, rhs, rhs}});
  expectDeterministic(mesh, noFlux, bcs);
}

// M4, 2 x 2 x 2: h = 0.5, A = 0.25, V = 0.125.
//   internal faces: D = 2 * 0.25 / 0.5 = 1
//
// P12-DIFF-002 A5, entry 12. Every axis has 2 cells, so every one of the three walls per cell is
// reconstructed -- no fallback anywhere. Derived in a5/tools/derive_expected.py, block C3, with
// k = 2, A = 0.25, h1 = 0.25, h2 = 0.75:
//   cP = 2 * 0.25 * 0.75/(0.25*0.5) = 3      (two-point would be 2)
//   cF = 2 * 0.25 * 0.25/(0.75*0.5) = 1/3
//   cB = 2 * 0.25 * (4 + 4/3)       = 8/3    and cP - cF = cB
// Every cell is a corner with 3 internal and 3 boundary faces:
// diag = 3*1 + 3*cP = 12, off-diagonal -(1 + 1/3) to each neighbour (id ^ 1, id ^ 2, id ^ 4),
// rhs = 3 * cB + 5 * 0.125 = 8.625.
TEST(SparseAssembly3DTest, TwoByTwoByTwo) {
  const Mesh mesh = MeshGeometry::createCartesian3D(2, 2, 2, 1.0, 1.0, 1.0);
  const SurfaceField noFlux(mesh.numberOfFaces(), 0.0);
  const auto bcs = allFixedValue(mesh, 1.0);
  Expected e;
  e.rowOffsets = {0, 4, 8, 12, 16, 20, 24, 28, 32};
  // Row r: its columns {r, r^1, r^2, r^4} in increasing order.
  const std::vector<std::vector<Index>> columns = {{0, 1, 2, 4}, {0, 1, 3, 5}, {0, 2, 3, 6},
                                                   {1, 2, 3, 7}, {0, 4, 5, 6}, {1, 4, 5, 7},
                                                   {2, 4, 6, 7}, {3, 5, 6, 7}};
  for (Index r = 0; r < 8; ++r) {
    for (const Index c : columns[r]) {
      e.columns.push_back(c);
      e.values.push_back(c == r ? 3.0 * 1.0 + 3.0 * 3.0 : -(1.0 + 1.0 / 3.0));
    }
    e.rhs.push_back(3.0 * 8.0 / 3.0 + 5.0 * 0.125);
  }
  expectSystem(assemble(mesh, noFlux, bcs), e);
  expectDeterministic(mesh, noFlux, bcs);
}
