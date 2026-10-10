// P14 migration -- FROZEN CHARACTERIZATION of the scalar transport assembly.
//
// WHAT THESE NUMBERS ARE, AND WHAT THEY ARE NOT
// ---------------------------------------------
// They are CHARACTERIZATION values: a record of what the assembler produced at
// the moment its output was PROVED bitwise identical to the
// `cfd::turbulence` implementation it replaced. They are a regression oracle,
// NOT mathematical reference data and NOT an independent derivation.
//
// So if a future change turns these red, the correct response is to work out
// whether the discretisation was meant to change, and if it was, to RE-DERIVE
// and JUSTIFY new values -- not to preserve these bits forever, and not to
// adjust them until the test passes. A red result here means "the assembled
// system moved"; it does not by itself mean "the assembler is wrong".
//
// HOW THEY WERE OBTAINED
//   1. all six production turbulence equations were migrated onto
//      cfd::transport, with bitwise equivalence gated at every step;
//   2. the two implementations were asserted bitwise identical ON THIS EXACT
//      CASE, immediately before capture -- so nothing unproven was frozen;
//   3. the CSR arrays, RHS and solved field were emitted at %.17g, which
//      round-trips double exactly;
//   4. only then was the duplicate implementation deleted.
//
// WHY RAW CSR RATHER THAN A DENSE COMPARISON
// The row offsets and column indices are frozen alongside the values because
// they detect STRUCTURAL change -- duplicate-entry handling, sparsity
// reordering, an entry that stops being stored at all -- which a value-only or
// dense comparison can miss while the algebra stays numerically equivalent.
//
// THE CASE: 4x3 Cartesian, chosen so these arrays stay human-reviewable while
// still covering interior faces, all four boundaries, a variable diffusivity,
// both upwind branches, and (in the second test) the non-orthogonal correction.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Index kSteadyRowOffsets[] = {
    0, 3, 7, 11, 14, 18, 23, 28, 32, 35, 39, 43, 46,
};

constexpr Index kSteadyColumnIndices[] = {
    0, 1, 4, 0, 1,  2, 5, 1, 2,  3, 6, 2, 3, 7, 0, 4,  5, 8, 1,  4,  5, 6,  9,
    2, 5, 6, 7, 10, 3, 6, 7, 11, 4, 8, 9, 5, 8, 9, 10, 6, 9, 10, 11, 7, 10, 11,
};

constexpr Real kSteadyValues[] = {
    6.8883333333333319,
    -1.2511111111111111,
    -0.71499999999999975,
    -1.29,
    5.4299999999999979,
    -0.95333333333333325,
    -0.72499999999999987,
    -1.3033333333333332,
    4.794999999999999,
    -1.3166666666666664,
    -1.0849999999999997,
    -1.2911111111111109,
    7.4999999999999991,
    -0.74499999999999988,
    -0.88999999999999979,
    6.129999999999999,
    -1.6722222222222221,
    -0.57000000000000006,
    -0.89749999999999985,
    -0.99333333333333329,
    4.2000000000000002,
    -1.0066666666666666,
    -0.9275000000000001,
    -0.55499999999999983,
    -1.3566666666666665,
    4.5999999999999996,
    -1.02,
    -0.58500000000000019,
    -0.91249999999999987,
    -1.7122222222222221,
    5.9966666666666661,
    -0.59250000000000003,
    -1.1150000000000002,
    7.591666666666665,
    -1.3933333333333331,
    -0.77500000000000024,
    -1.3966666666666665,
    5.5699999999999985,
    -1.4099999999999997,
    -1.1350000000000002,
    -1.0599999999999998,
    5.9849999999999985,
    -1.0733333333333333,
    -1.145,
    -1.7833333333333332,
    7.5033333333333321,
};

constexpr Real kSteadyRhs[] = {
    1330.9444444444446, 411.98749999999995,  417.80833333333328,  1277.8402777777778,
    763.27222222222213, 0.27083333333333331, 0.29166666666666663, 895.76805555555552,
    1358.4999999999995, 458.5541666666665,   464.37499999999983,  1406.8958333333328,
};

constexpr Index kNonOrthoRowOffsets[] = {
    0, 3, 7, 11, 14, 18, 23, 28, 32, 35, 39, 43, 46,
};

constexpr Index kNonOrthoColumnIndices[] = {
    0, 1, 4, 0, 1,  2, 5, 1, 2,  3, 6, 2, 3, 7, 0, 4,  5, 8, 1,  4,  5, 6,  9,
    2, 5, 6, 7, 10, 3, 6, 7, 11, 4, 8, 9, 5, 8, 9, 10, 6, 9, 10, 11, 7, 10, 11,
};

constexpr Real kNonOrthoValues[] = {
    6.8461631783971511,   -1.414336289176819,   -0.83156890070013656, -1.4476726574775061,
    5.5654200136809759,   -0.96796360481848109, -0.76904253264607902, -1.3179636048184811,
    4.7826243853048025,   -1.1735540970799496,  -1.0970813513495028,  -1.13573979963753,
    7.7404418184624397,   -0.7028193579090205,  -1.0021200984285699,  5.9546820569261065,
    -1.5295712607401875,  -0.74883337841971487, -0.93907228793512332, -0.87939772098253277,
    4.2866111266762346,   -1.0071417309323323,  -1.0574634279162742,  -0.56465941744387094,
    -1.3571417309323324,  4.7098539889556106,   -1.1727190965472836,  -0.56117050202898522,
    -0.87662544506661466, -1.8994777542158592,  6.5574926329046974,   -0.493037782512179,
    -1.3068761129450355,  7.420840947602624,    -1.0936351041916557,  -0.91037843376837191,
    -1.1365465747203929,  5.626872601828838,    -1.4818686959586675,  -1.1024648552671312,
    -1.1318686959586677,  6.2869769526468007,   -1.4330814483827305,  -1.0337789098881611,
    -2.1902204999350263,  7.9021393312451265,
};

constexpr Real kNonOrthoRhs[] = {
    1217.1650376960938, 373.55735982448266, 458.0787851641918,   1424.3375791070666,
    651.46258086485307, 2.7077453196498658, 0.28756880238233484, 1063.8476710309799,
    1339.7172156133374, 495.07357719856412, 436.29645976081412,  1434.4248022004024,
};

// The frozen case. Identical construction to the capture harness that produced
// the arrays above; changing any of it invalidates them.
struct FrozenCase {
  Mesh mesh;
  ScalarField phi;
  ScalarField gamma;
  SurfaceField flux;
  ScalarField su;
  ScalarField sp;
  BoundaryConditionSet boundaries;
};

ScalarField varying(const Mesh& mesh, Real base, Real slope) {
  ScalarField field(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    field[i] = base + (slope * static_cast<Real>(i));
  }
  return field;
}

FrozenCase makeFrozenCase(bool distorted) {
  // The steady case is Cartesian; the non-orthogonal case is DISTORTED, because
  // on a Cartesian mesh the correction is identically zero and freezing it there
  // would be vacuous. Amplitude follows this repository's own 0.3/nx convention.
  Mesh mesh = distorted ? cfd::test::createDistortedQuad2D(4, 3, 1.0, 1.0, 0.3 / 4.0)
                        : MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  ScalarField phi = varying(mesh, 300.0, 1.5);
  ScalarField gamma = varying(mesh, 0.7, 0.01);
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    // Alternating sign so BOTH upwind branches are exercised.
    flux[f] = (f % 3 == 0) ? -0.35 : 0.35;
  }
  ScalarField su = varying(mesh, 2.0, 0.25);
  ScalarField sp = varying(mesh, -4.0, -0.1);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedValue>(290.0));
  }
  return FrozenCase{std::move(mesh), std::move(phi), std::move(gamma),     std::move(flux),
                    std::move(su),   std::move(sp),  std::move(boundaries)};
}

void expectMatchesFrozen(const cfd::algebra::LinearSystem& system, const Index* rowOffsets,
                         Index rowOffsetCount, const Index* columnIndices, Index nonZeroCount,
                         const Real* values, const Real* rhs, Index rhsCount) {
  const auto& m = system.matrix();
  ASSERT_EQ(m.rows() + 1, rowOffsetCount) << "row count changed";
  ASSERT_EQ(m.nonZeros(), nonZeroCount) << "SPARSITY changed: the number of stored entries moved";
  for (Index i = 0; i < rowOffsetCount; ++i) {
    EXPECT_EQ(m.rowOffsetsData()[i], rowOffsets[i]) << "row offset " << i;
  }
  for (Index k = 0; k < nonZeroCount; ++k) {
    EXPECT_EQ(m.columnIndicesData()[k], columnIndices[k]) << "column index " << k;
    // EXPECT_EQ on doubles, deliberately: this is the bitwise oracle.
    EXPECT_EQ(m.valuesData()[k], values[k]) << "value " << k;
  }
  ASSERT_EQ(system.rhs().size(), rhsCount);
  for (Index i = 0; i < rhsCount; ++i) {
    EXPECT_EQ(system.rhs()[i], rhs[i]) << "rhs " << i;
  }
}

}  // namespace

TEST(TransportFoundation, FrozenTransportCharacterization) {
  const FrozenCase c = makeFrozenCase(/*distorted=*/false);
  const auto assembly = cfd::transport::assembleScalarTransportEquation(
      c.mesh, c.phi, c.flux, c.gamma, c.boundaries, c.su, c.sp);
  expectMatchesFrozen(assembly.system, kSteadyRowOffsets, std::size(kSteadyRowOffsets),
                      kSteadyColumnIndices, std::size(kSteadyColumnIndices), kSteadyValues,
                      kSteadyRhs, std::size(kSteadyRhs));
}

TEST(TransportFoundation, FrozenNonOrthogonalCharacterization) {
  // Frozen SEPARATELY from the steady case on purpose: the correction adds an
  // explicit RHS term and a far-cell implicit entry, so a change that
  // redistributed work between the implicit matrix and the explicit RHS could
  // leave the steady case and even the final solution close while altering how
  // the system is decomposed. Only a separate frozen pair catches that.
  const FrozenCase c = makeFrozenCase(/*distorted=*/true);
  cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonal;
  nonOrthogonal.enabled = true;
  const auto assembly = cfd::transport::assembleScalarTransportEquation(
      c.mesh, c.phi, c.flux, c.gamma, c.boundaries, c.su, c.sp, nonOrthogonal);
  expectMatchesFrozen(assembly.system, kNonOrthoRowOffsets, std::size(kNonOrthoRowOffsets),
                      kNonOrthoColumnIndices, std::size(kNonOrthoColumnIndices), kNonOrthoValues,
                      kNonOrthoRhs, std::size(kNonOrthoRhs));

  // AND the correction must still be doing something here. Without this the
  // test would silently become vacuous if the mesh generator, the amplitude or
  // the correction itself ever stopped producing a distorted system.
  const auto uncorrected = cfd::transport::assembleScalarTransportEquation(
      c.mesh, c.phi, c.flux, c.gamma, c.boundaries, c.su, c.sp);
  bool differs = false;
  for (Index k = 0; k < assembly.system.matrix().nonZeros(); ++k) {
    if (assembly.system.matrix().valuesData()[k] != uncorrected.system.matrix().valuesData()[k]) {
      differs = true;
    }
  }
  for (Index i = 0; i < assembly.system.rhs().size(); ++i) {
    if (assembly.system.rhs()[i] != uncorrected.system.rhs()[i]) differs = true;
  }
  EXPECT_TRUE(differs) << "the non-orthogonal correction changes nothing on this mesh, so this "
                          "frozen case is indistinguishable from the steady one";
}

TEST(TransportFoundation, TheFrozenCharacterizationCanDetectAnAssemblyChange) {
  // NON-VACUITY CONTROL, carried over from the migration gate and kept for the
  // same reason: the two tests above compare against constants, and constants
  // that happened to be insensitive would pass forever. Harmonic face
  // diffusivity is a REAL discretisation change on a case built with a variable
  // gamma, and it must be detected.
  const FrozenCase c = makeFrozenCase(/*distorted=*/false);
  const auto harmonic = cfd::transport::assembleScalarTransportEquation(
      c.mesh, c.phi, c.flux, c.gamma, c.boundaries, c.su, c.sp, {}, {},
      cfd::transport::FaceDiffusivityInterpolation::Harmonic);

  bool anyDifference = false;
  ASSERT_EQ(harmonic.system.matrix().nonZeros(), std::size(kSteadyValues));
  for (Index k = 0; k < harmonic.system.matrix().nonZeros(); ++k) {
    if (harmonic.system.matrix().valuesData()[k] != kSteadyValues[k]) anyDifference = true;
  }
  EXPECT_TRUE(anyDifference)
      << "harmonic face diffusivity produced the frozen arithmetic values, so these "
         "characterization tests cannot detect an assembly change and prove nothing";
}
