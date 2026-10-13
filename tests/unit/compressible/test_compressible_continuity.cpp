// P3-PHYS-006 sections 10, 28, 33: the transient compressible continuity
// diagnostic, hand-derived, plus the 1D-duct constant-mDot conservation
// check section 33 asks for ("this test should not require the entire
// nonlinear CFD solver").
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "cfd/compressible/CompressibleContinuity.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::CompressibleContinuityResult;
using cfd::compressible::evaluateCompressibleContinuity;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(CompressibleContinuityTest, TimeOnlyImbalanceWithZeroFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 2.0, 2.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  const ScalarField densityOld(n, 1.0);
  ScalarField densityNew(n, 1.0);
  densityNew[0] = 1.2;
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const Real dt = 0.1;

  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, dt);

  // cellImbalance[0] = (1.2-1.0)*1.0/0.1 = 2.0; all other cells 0.
  EXPECT_NEAR(result.cellImbalance[0], 2.0, 1e-12);
  for (Index i = 1; i < n; ++i) {
    EXPECT_NEAR(result.cellImbalance[i], 0.0, 1e-12) << "cell " << i;
  }
  EXPECT_NEAR(result.totalMassOld, 4.0, 1e-12);  // 4 cells * rho=1 * V=1.
  EXPECT_NEAR(result.totalMassNew, 4.2, 1e-12);  // one cell's rho raised by 0.2.
  EXPECT_NEAR(result.maxCellImbalance, 2.0, 1e-12);
}

TEST(CompressibleContinuityTest, SteadyStateWithBalancedFluxGivesZeroImbalance) {
  // densityOld == densityNew (no time term); massFlux is a genuinely
  // steady, mass-conservative field (constant on every internal face) --
  // the FVM face-once assembly already guarantees this telescopes to
  // exactly zero at every interior cell, same identity proven for every
  // other transport equation in this codebase.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 2, 4.0, 1.0);  // a 1D duct, 4 cells long.
  const Index n = mesh.numberOfCells();
  const ScalarField density(n, 1.0);
  // A uniform mDot on every x-normal face (internal or boundary) in the
  // +x sense -- the 1D-duct constant-mass-flow-rate condition, section
  // 33's own "mDot = rho u A = constant"; y-normal (top/bottom) faces
  // get exactly 0 (no cross-duct flow).
  // Owner-oriented: a face's own Sf sets its positive sense. Internal
  // x-normal faces and the right (outlet) boundary all have Sf pointing
  // in +x, so +2 there means "flow in the +x sense"; the left (inlet)
  // boundary's Sf points in -x (outward from the domain, to the left),
  // so the SAME physical +x-directed flow is -2 there (mass entering,
  // not leaving) -- this is exactly what makes the duct's mass flow rate
  // actually constant in the physical sense, not merely numerically
  // uniform in the raw (sign-ambiguous) owner-oriented array.
  SurfaceField ductFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (std::abs(face.areaVector().x) > 1e-12) {
      ductFlux[face.id()] = (face.areaVector().x > 0.0) ? 2.0 : -2.0;
    }
  }

  const Real dt = 0.05;
  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, density, density, ductFlux, dt);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(result.cellImbalance[cell.id()], 0.0, 1e-10)
        << "cell " << cell.id() << " (constant duct mass flow rate must balance exactly)";
  }
  EXPECT_NEAR(result.totalMassOld, result.totalMassNew, 1e-12);
}

TEST(CompressibleContinuityTest, CombinedTimeAndFluxHandDerived) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);  // cell volume = 1 each.
  const Index n = mesh.numberOfCells();
  ScalarField densityOld(n, 1.0);
  ScalarField densityNew(n);
  densityNew[0] = 1.1;
  densityNew[1] = 0.9;
  const Real dt = 0.2;

  Index internalFaceId = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) internalFaceId = face.id();
  }
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[internalFaceId] = 3.0;  // owner (cell0) -> neighbor (cell1).

  const CompressibleContinuityResult result =
      evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, dt);

  // cell0: (1.1-1.0)*1/0.2 + 3.0 = 0.5 + 3.0 = 3.5.
  EXPECT_NEAR(result.cellImbalance[0], 3.5, 1e-10);
  // cell1: (0.9-1.0)*1/0.2 - 3.0 = -0.5 - 3.0 = -3.5.
  EXPECT_NEAR(result.cellImbalance[1], -3.5, 1e-10);
}

TEST(CompressibleContinuityTest, MismatchedDensityOldSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField densityOld(mesh.numberOfCells() + 1, 1.0);
  const ScalarField densityNew(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleContinuityTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, 0.1),
               InvalidArgumentError);
}

TEST(CompressibleContinuityTest, RejectsNonPositiveOrNonFiniteDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField density(mesh.numberOfCells(), 1.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux, -0.1),
               InvalidArgumentError);
  EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, density, density, massFlux,
                                                    std::numeric_limits<Real>::quiet_NaN()),
               InvalidArgumentError);
}

// ---------------------------------------------------------------------------
// P17.3 -- globalNetFlux on the transient result, the value preconditions, and the
// steady/transient/manufactured verification the gate requires.
//
// Measured on the pre-P17.3 tree (results/p17/compressible_continuity/logs/00_dryrun.log):
//
//   globalNetFlux                       ABSENT -- a caller had to re-loop the boundary
//   rhoNew[0] = NaN   -> maxCellImbalance 0   "perfectly conserved"
//   massFlux[0] = NaN -> maxCellImbalance 0   "perfectly conserved"
//   rhoNew[0] = -2.0  -> totalMassNew 0       finite, plausible, catastrophic
//
// The NaN results are why the preconditions are here: std::max(a,b) is (a<b)?b:a, so with
// a running maximum of 0 and b = NaN the comparison 0 < NaN is false and the NaN is
// SILENTLY SKIPPED -- the max reduction reports zero imbalance for a non-finite field.
// ---------------------------------------------------------------------------

namespace {

// A uniform x-directed mass flux on a Cartesian mesh: every x-normal face carries the same
// mDot, every y-normal face carries zero. Divergence-free by construction, so the operator
// must return EXACTLY zero -- which is what section 11C of the authorization asks for,
// testing the discrete operator directly rather than through a solver.
SurfaceField uniformXFlux(const Mesh& mesh, Real mDot) {
  SurfaceField flux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    // areaVector carries the normal AND the area; an x-normal face has a non-zero x
    // component. Sign follows the outward normal, so a uniform field is +mDot*sign.
    if (face.areaVector().x != 0.0) {
      flux[f] = mDot * (face.areaVector().x > 0.0 ? 1.0 : -1.0);
    }
  }
  return flux;
}

}  // namespace

TEST(CompressibleContinuityTest, GlobalNetFluxSumsBoundaryFacesOnly) {
  // The domain's net in/outflow. Internal faces cancel pairwise and must contribute
  // NOTHING -- control N3 (summing all faces instead) breaks exactly this.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 2.0);
  const Real mDot = 3.0;
  const SurfaceField flux = uniformXFlux(mesh, mDot);

  const CompressibleContinuityResult r = evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0);

  // Inflow on the left boundary balances outflow on the right, so the net is exactly zero
  // even though individual boundary faces carry +-mDot.
  EXPECT_EQ(r.globalNetFlux, 0.0);

  // And it really is the boundary sum: recompute it independently.
  Real expected = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) expected += flux[f];
  }
  EXPECT_EQ(r.globalNetFlux, expected);

  // A sum over ALL faces would be a different (and meaningless) number here; assert they
  // differ so the test would catch an all-faces implementation.
  Real allFaces = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) allFaces += flux[f];
  EXPECT_NE(allFaces, 0.0) << "the all-faces sum must differ, or N3 could not be detected";
}

TEST(CompressibleContinuityTest, UniformDivergenceFreeFluxGivesExactlyZeroImbalance) {
  // Section 11A/11C: the discrete operator on an exactly divergence-free field.
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 5.0, 4.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 1.7);
  const SurfaceField flux = uniformXFlux(mesh, 2.5);

  const CompressibleContinuityResult r = evaluateCompressibleContinuity(mesh, rho, rho, flux, 0.5);

  for (const auto& cell : mesh.cells()) {
    EXPECT_EQ(r.cellImbalance[cell.id()], 0.0) << "cell " << cell.id();
  }
  EXPECT_EQ(r.maxCellImbalance, 0.0);
  EXPECT_EQ(r.totalAbsoluteImbalance, 0.0);
  EXPECT_EQ(r.globalNetFlux, 0.0);
}

TEST(CompressibleContinuityTest, InteriorFaceContributionsCancelGlobally) {
  // Section 9: for every internal face the owner gets +F and the neighbour -F, so the
  // interior contributes exactly nothing to the sum of cellImbalance over all cells.
  // Control N5 (flipping the neighbour sign) breaks this.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 4.0, 4.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 1.0);

  // A deliberately IRREGULAR flux: every face a distinct magnitude, so nothing cancels by
  // symmetry and an orientation error cannot hide.
  SurfaceField flux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    flux[f] = 0.125 * static_cast<Real>(f + 1);
  }

  const CompressibleContinuityResult r = evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0);

  Real sumOfImbalances = 0.0;
  for (const auto& cell : mesh.cells()) sumOfImbalances += r.cellImbalance[cell.id()];

  // Summed over the whole mesh, only BOUNDARY faces survive -- which is globalNetFlux.
  EXPECT_NEAR(sumOfImbalances, r.globalNetFlux, 1e-10 * std::abs(r.globalNetFlux));
}

TEST(CompressibleContinuityTest, ReducesToTheSteadyEvaluatorWhenDensityIsUnchanged) {
  // Gate O1/O2. The two evaluators sum the same terms in DIFFERENT loop orders --
  // physics::evaluateContinuity per cell over cell.faceIds(), this one per face into owner
  // and neighbour -- and floating-point addition is not associative, so agreement is
  // MEASURED, not assumed. The codebase already treats this order as load-bearing:
  // cuda/kernels/DevicePressureCorrectionKernel.cu:28 records that the GPU port gathers
  // "in cell.faceIds() order" because those are "the two orders the CPU actually uses".
  //
  // An IRREGULAR flux is used deliberately: with equal-and-opposite or zero contributions
  // the orders cannot be distinguished.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 4.0, 4.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 1.0);
  SurfaceField flux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    flux[f] = 0.3 * static_cast<Real>(f + 1) - 0.07 * static_cast<Real>((f * 7) % 11);
  }

  const CompressibleContinuityResult trans =
      evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0);
  const cfd::physics::ContinuityResult steady = cfd::physics::evaluateContinuity(mesh, flux);

  constexpr Real kEps = std::numeric_limits<Real>::epsilon();
  Real maxFlux = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) maxFlux = std::max(maxFlux, std::abs(flux[f]));

  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    // Bounded at 4 eps of the flux scale: the two orders may differ by round-off, but not
    // by more than a few roundings of the largest contribution.
    EXPECT_NEAR(trans.cellImbalance[i], steady.cellImbalance[i], 4.0 * kEps * maxFlux)
        << "cell " << i;
  }
  EXPECT_NEAR(trans.globalNetFlux, steady.globalNetFlux, 4.0 * kEps * maxFlux);
}

TEST(CompressibleContinuityTest, TransientIntegralMassConservationHolds) {
  // Section 8's strongest statement:
  //     M(t2) - M(t1) + dt * sum_boundary(mDot) == 0
  // Constructed so it holds EXACTLY: a uniform x-flux carries mDot in through the left
  // boundary and out through the right, so the boundary sum is zero and the mass must not
  // change. Then a second case where mass DOES change and the relation still closes.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 2, 4.0, 2.0);
  const Index n = mesh.numberOfCells();
  const Real dt = 0.25;

  // Case 1: balanced boundary flux, so mass is unchanged.
  {
    const ScalarField rho(n, 1.5);
    const SurfaceField flux = uniformXFlux(mesh, 2.0);
    const CompressibleContinuityResult r = evaluateCompressibleContinuity(mesh, rho, rho, flux, dt);
    EXPECT_EQ(r.globalNetFlux, 0.0);
    EXPECT_EQ(r.totalMassNew - r.totalMassOld, 0.0);
    const Real residual = (r.totalMassNew - r.totalMassOld) + dt * r.globalNetFlux;
    EXPECT_EQ(residual, 0.0);
  }

  // Case 2: mass genuinely changes in time, and the relation must still close.
  // Choose the density change so that dM = -dt * netFlux exactly.
  {
    const ScalarField rhoOld(n, 1.5);
    // Net OUTflow of mDotNet through the boundary over dt removes dt*mDotNet of mass.
    // Put all of it on one boundary face by using a flux field with an imbalance.
    SurfaceField flux(mesh.numberOfFaces(), 0.0);
    const Real mDotNet = 0.8;
    // the first boundary face of the "right" patch carries the entire outflow
    const Index outFace = mesh.boundaryPatch("right").faceIds().front();
    flux[outFace] = mDotNet;

    const Real dM = -dt * mDotNet;                        // required mass change
    const Real totalVolume = static_cast<Real>(n) * 1.0;  // unit cells on this mesh
    ScalarField rhoNew(n, 1.5 + dM / totalVolume);

    const CompressibleContinuityResult r =
        evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, flux, dt);

    EXPECT_EQ(r.globalNetFlux, mDotNet);
    const Real residual = (r.totalMassNew - r.totalMassOld) + dt * r.globalNetFlux;
    // Derived bound: a sum of n products plus one multiply, so a few eps of the mass scale.
    constexpr Real kEps = std::numeric_limits<Real>::epsilon();
    EXPECT_NEAR(residual, 0.0, 8.0 * kEps * r.totalMassOld)
        << "M2-M1 = " << (r.totalMassNew - r.totalMassOld) << ", dt*net = " << dt * r.globalNetFlux;
    EXPECT_NE(r.totalMassNew, r.totalMassOld) << "the mass must genuinely change";
  }
}

TEST(CompressibleContinuityTest, RejectsNonFiniteOrNonPositiveDensity) {
  // The NaN case is the one that mattered: before P17.3 it made maxCellImbalance report
  // EXACTLY 0 -- "perfectly conserved" -- because std::max silently skips NaN.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const Index n = mesh.numberOfCells();
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);
  const Real qnan = std::numeric_limits<Real>::quiet_NaN();
  const Real pinf = std::numeric_limits<Real>::infinity();

  for (const Real bad : {-2.0, 0.0, qnan, pinf, -pinf}) {
    ScalarField rhoNew(n, 2.0);
    rhoNew[0] = bad;
    const ScalarField rhoOld(n, 2.0);
    EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, flux, 1.0),
                 InvalidArgumentError)
        << "densityNew = " << bad;

    ScalarField rhoOldBad(n, 2.0);
    rhoOldBad[1] = bad;
    const ScalarField rhoNewOk(n, 2.0);
    EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, rhoOldBad, rhoNewOk, flux, 1.0),
                 InvalidArgumentError)
        << "densityOld = " << bad;
  }
}

TEST(CompressibleContinuityTest, RejectsNonFiniteMassFlux) {
  // A NaN flux had the same effect on maxCellImbalance: exactly 0.
  // Finiteness ONLY -- a face flux is legitimately negative (inflow) and legitimately
  // zero (a wall), so positivity would be the wrong rule here.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 2.0);
  const Real qnan = std::numeric_limits<Real>::quiet_NaN();
  const Real pinf = std::numeric_limits<Real>::infinity();

  for (const Real bad : {qnan, pinf, -pinf}) {
    SurfaceField flux(mesh.numberOfFaces(), 0.0);
    flux[0] = bad;
    EXPECT_THROW((void)evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0),
                 InvalidArgumentError)
        << "massFlux = " << bad;
  }

  // A negative flux is VALID and must be accepted.
  SurfaceField inflow(mesh.numberOfFaces(), 0.0);
  inflow[0] = -3.0;
  EXPECT_NO_THROW((void)evaluateCompressibleContinuity(mesh, rho, rho, inflow, 1.0));
}

TEST(CompressibleContinuityTest, RejectionMessagesNameTheOffendingIndex) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 1, 3.0, 1.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 2.0);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);

  ScalarField rhoBad(n, 2.0);
  rhoBad[2] = -1.0;
  try {
    (void)evaluateCompressibleContinuity(mesh, rho, rhoBad, flux, 1.0);
    FAIL() << "a negative densityNew must be rejected";
  } catch (const InvalidArgumentError& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("cell 2"), std::string::npos) << what;
    EXPECT_NE(what.find("densityNew"), std::string::npos) << what;
  }

  SurfaceField fluxBad(mesh.numberOfFaces(), 0.0);
  fluxBad[1] = std::numeric_limits<Real>::quiet_NaN();
  try {
    (void)evaluateCompressibleContinuity(mesh, rho, rho, fluxBad, 1.0);
    FAIL() << "a non-finite massFlux must be rejected";
  } catch (const InvalidArgumentError& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("face 1"), std::string::npos) << what;
    EXPECT_NE(what.find("massFlux"), std::string::npos) << what;
  }
}

TEST(CompressibleContinuityTest, QuiescentClosedBoxConservesExactly) {
  // Section 11D. Quiescent deliberately: a closed box with a NON-ZERO uniform interior
  // velocity is not solenoidal, so its per-cell imbalance is legitimately non-zero -- the
  // P17.3 dry-run measured maxCellImbalance = 4 for exactly that reason. The canonical
  // closed-box case is the one where nothing moves.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 3.0, 3.0);
  const Index n = mesh.numberOfCells();
  const ScalarField rho(n, 1.25);
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);

  const CompressibleContinuityResult r = evaluateCompressibleContinuity(mesh, rho, rho, flux, 1.0);

  EXPECT_EQ(r.globalNetFlux, 0.0);
  EXPECT_EQ(r.maxCellImbalance, 0.0);
  EXPECT_EQ(r.totalAbsoluteImbalance, 0.0);
  EXPECT_EQ(r.totalMassNew, r.totalMassOld);
}

TEST(CompressibleContinuityTest, TotalMassMatchesSumOfRhoTimesVolume) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = mesh.numberOfCells();
  ScalarField rhoOld(n), rhoNew(n);
  for (const auto& cell : mesh.cells()) {
    rhoOld[cell.id()] = 1.0 + 0.1 * static_cast<Real>(cell.id());
    rhoNew[cell.id()] = rhoOld[cell.id()] * 1.05;
  }
  const SurfaceField flux(mesh.numberOfFaces(), 0.0);

  const CompressibleContinuityResult r =
      evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, flux, 1.0);

  Real expectedOld = 0.0, expectedNew = 0.0;
  for (const auto& cell : mesh.cells()) {
    expectedOld += rhoOld[cell.id()] * cell.volume();
    expectedNew += rhoNew[cell.id()] * cell.volume();
  }
  constexpr Real kEps = std::numeric_limits<Real>::epsilon();
  EXPECT_NEAR(r.totalMassOld, expectedOld, 4.0 * kEps * expectedOld);
  EXPECT_NEAR(r.totalMassNew, expectedNew, 4.0 * kEps * expectedNew);
  EXPECT_GT(r.totalMassNew, r.totalMassOld) << "the density must genuinely change";
}

TEST(CompressibleContinuityTest, RepeatedEvaluationIsBitwiseIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const Index n = mesh.numberOfCells();
  ScalarField rhoOld(n), rhoNew(n);
  for (const auto& cell : mesh.cells()) {
    rhoOld[cell.id()] = 1.0 + 0.03 * static_cast<Real>(cell.id());
    rhoNew[cell.id()] = rhoOld[cell.id()] * 1.01;
  }
  const SurfaceField flux = uniformXFlux(mesh, 1.75);

  const CompressibleContinuityResult a =
      evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, flux, 0.2);
  for (int repeat = 0; repeat < 8; ++repeat) {
    const CompressibleContinuityResult b =
        evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, flux, 0.2);
    for (Index i = 0; i < n; ++i) {
      EXPECT_EQ(a.cellImbalance[i], b.cellImbalance[i]) << "repeat " << repeat << " cell " << i;
    }
    EXPECT_EQ(a.globalNetFlux, b.globalNetFlux);
    EXPECT_EQ(a.totalMassOld, b.totalMassOld);
    EXPECT_EQ(a.totalMassNew, b.totalMassNew);
    EXPECT_EQ(a.maxCellImbalance, b.maxCellImbalance);
  }
}
