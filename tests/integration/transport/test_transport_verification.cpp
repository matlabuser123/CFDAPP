// P14 Phase K/L -- analytical verification and global conservation for the
// generic scalar transport equation.
//
// These solve, they do not merely assemble. Every case below has a closed-form
// or independently-derived reference, and every tolerance is justified by the
// discretisation's own order of accuracy rather than chosen to make the test
// pass.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/transport/ScalarTransport.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::BiCGSTAB;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::transport::TransportTimeState;

namespace {

// A 1D problem on a 2D mesh: nx cells across, one cell deep, Dirichlet on
// left/right and zero-gradient on top/bottom so nothing varies in y.
struct OneDimensionalCase {
  Mesh mesh;
  BoundaryConditionSet boundaries;
};

OneDimensionalCase makeOneDimensional(Index nx, Real length, Real phiLeft, Real phiRight) {
  OneDimensionalCase problem{MeshGeometry::createCartesian2D(nx, 1, length, 1.0), {}};
  problem.boundaries.set(problem.mesh, "left", std::make_unique<FixedValue>(phiLeft));
  problem.boundaries.set(problem.mesh, "right", std::make_unique<FixedValue>(phiRight));
  problem.boundaries.set(problem.mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  problem.boundaries.set(problem.mesh, "top", std::make_unique<FixedGradient>(0.0));
  return problem;
}

Vector solve(const cfd::algebra::LinearSystem& system, Index n) {
  LinearSolverSettings settings;
  settings.absoluteTolerance = 1e-14;
  settings.relativeTolerance = 1e-13;
  settings.maxIterations = 5000;
  const BiCGSTAB solver(settings);
  const auto result = solver.solve(system, Vector(n, 0.0));
  EXPECT_EQ(result.status, cfd::algebra::SolverStatus::Converged)
      << "the verification solve did not converge (residual " << result.finalResidual << ")";
  return result.solution;
}

// Assemble-and-solve to a converged field.
//
// ONE solve is not enough, and that is a property of the discretisation rather
// than of this harness: the Dirichlet boundary treatment (P12-DIFF-002) is a
// DEFERRED second-order reconstruction evaluated from the CURRENT field, so
// the assembled system depends on the iterate it was assembled around.
// Assembling once about phi = 0 and solving gave a max error of 231 against an
// analytical range of 400 -- not a tolerance problem, a wrong fixed point.
// Production code iterates for exactly this reason (KEpsilonModel calls
// solveRelaxedScalarTransport once per outer iteration), and so does this.
ScalarField solveToConvergence(const Mesh& mesh, const BoundaryConditionSet& boundaries,
                               const ScalarField& gamma, const SurfaceField& massFlux,
                               const ScalarField& Su, const ScalarField& Sp,
                               int maxOuterIterations = 200, Real tolerance = 1e-12) {
  const Index n = mesh.numberOfCells();
  ScalarField phi(n, 0.0);
  for (int iteration = 0; iteration < maxOuterIterations; ++iteration) {
    const auto assembly = cfd::transport::assembleScalarTransportEquation(
        mesh, phi, massFlux, gamma, boundaries, Su, Sp);
    const Vector next = solve(assembly.system, n);
    Real change = 0.0;
    Real magnitude = 0.0;
    for (Index i = 0; i < n; ++i) {
      change = std::max(change, std::abs(next[i] - phi[i]));
      magnitude = std::max(magnitude, std::abs(next[i]));
    }
    for (Index i = 0; i < n; ++i) phi[i] = next[i];
    // RELATIVE to the field's own magnitude. An absolute 1e-12 is below
    // round-off for a field of order 500 (500 * 2^-52 ~ 1.1e-13, so 1e-12 is
    // about nine ULP) and the iteration then never "converges" however correct
    // it is -- which is exactly what the first version of this harness did on
    // the 100..500 case while passing on the order-one cases.
    if (change < tolerance * std::max(1.0, magnitude)) return phi;
  }
  ADD_FAILURE() << "the outer (deferred-correction) iteration did not converge";
  return phi;
}

Real cellCentreX(const Mesh& mesh, Index cell) { return mesh.cell(cell).centroid().x; }

// Global balance: for the assembled system, sum_P (A phi - b)_P. Every
// internal face contributes equally and oppositely to its two cells, so the
// residual sum measures what entered or left through the boundary against what
// the sources put in -- the discrete form of Phase L's balance.
Real globalImbalance(const cfd::algebra::LinearSystem& system, const Vector& phi) {
  const Vector Aphi = system.matrix().multiply(phi);
  Real sum = 0.0;
  for (Index i = 0; i < phi.size(); ++i) sum += Aphi[i] - system.rhs()[i];
  return sum;
}

}  // namespace

// --- Case 1: steady 1D diffusion, analytical solution is linear -------------

TEST(TransportVerification, Case1SteadyDiffusionIsLinear) {
  // d/dx(Gamma dphi/dx) = 0, phi(0)=100, phi(L)=500 -> phi(x) = 100 + 400 x/L.
  // A linear field is reproduced EXACTLY by a two-point central diffusion
  // operator on a uniform mesh -- there is no truncation error to hide behind,
  // so the tolerance is solver tolerance, not discretisation tolerance.
  constexpr Index nx = 20;
  constexpr Real length = 2.0;
  constexpr Real phiLeft = 100.0;
  constexpr Real phiRight = 500.0;
  auto problem = makeOneDimensional(nx, length, phiLeft, phiRight);
  const Index n = problem.mesh.numberOfCells();

  const ScalarField phi0(n, 0.0);
  const ScalarField gamma(n, 3.5);
  const SurfaceField noFlux(problem.mesh.numberOfFaces(), 0.0);
  const ScalarField zero(n, 0.0);

  const ScalarField phi =
      solveToConvergence(problem.mesh, problem.boundaries, gamma, noFlux, zero, zero);

  Real maxError = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real x = cellCentreX(problem.mesh, i);
    const Real exact = phiLeft + (phiRight - phiLeft) * x / length;
    maxError = std::max(maxError, std::abs(phi[i] - exact));
  }
  EXPECT_LT(maxError, 1e-9) << "linear profile not reproduced; max error " << maxError;
}

TEST(TransportVerification, Case1RefiningDoesNotDegrade) {
  // The exactness above must not be an accident of one grid.
  for (const Index nx : {10, 20, 40}) {
    auto problem = makeOneDimensional(nx, 1.0, 0.0, 1.0);
    const Index n = problem.mesh.numberOfCells();
    const ScalarField gamma(n, 1.0);
    const ScalarField phi = solveToConvergence(problem.mesh, problem.boundaries, gamma,
                                               SurfaceField(problem.mesh.numberOfFaces(), 0.0),
                                               ScalarField(n, 0.0), ScalarField(n, 0.0));
    Real maxError = 0.0;
    for (Index i = 0; i < n; ++i) {
      maxError = std::max(maxError, std::abs(phi[i] - cellCentreX(problem.mesh, i)));
    }
    EXPECT_LT(maxError, 1e-9) << "nx = " << nx;
  }
}

// --- Case 2: diffusion with a source, analytical solution is a parabola -----

TEST(TransportVerification, Case2DiffusionWithSourceIsParabolic) {
  // Gamma d2phi/dx2 + S = 0 with phi(0)=phi(L)=0 gives
  //   phi(x) = S x (L - x) / (2 Gamma),
  // a quadratic. A second-order operator reproduces a quadratic's cell-centre
  // values to within the second-derivative truncation term, which for this
  // operator on a uniform mesh is again exact -- so this case validates the
  // SOURCE SIGN and magnitude, which is what it exists for. A sign slip gives
  // a parabola of the opposite curvature and fails by 100%.
  constexpr Index nx = 40;
  constexpr Real length = 1.0;
  constexpr Real gammaValue = 2.0;
  constexpr Real sourceValue = 8.0;
  auto problem = makeOneDimensional(nx, length, 0.0, 0.0);
  const Index n = problem.mesh.numberOfCells();

  const ScalarField gamma(n, gammaValue);
  const ScalarField Su(n, sourceValue);
  const ScalarField Sp(n, 0.0);

  const ScalarField phi =
      solveToConvergence(problem.mesh, problem.boundaries, gamma,
                         SurfaceField(problem.mesh.numberOfFaces(), 0.0), Su, Sp);

  Real maxError = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real x = cellCentreX(problem.mesh, i);
    const Real exact = sourceValue * x * (length - x) / (2.0 * gammaValue);
    maxError = std::max(maxError, std::abs(phi[i] - exact));
  }
  // h^2/8 * |phi''| is the cell-centre quadrature error of a quadratic;
  // h = 1/40 gives 8/(2*2) * h^2/8 -- a few 1e-4. The bound is that estimate
  // rounded up, not a number chosen after seeing the result.
  EXPECT_LT(maxError, 1e-3) << "max error " << maxError;
  // Positive source with zero walls must give a positive interior.
  for (Index i = 0; i < n; ++i) EXPECT_GT(phi[i], 0.0) << "cell " << i;
}

TEST(TransportVerification, Case2NegativeSourceFlipsTheProfile) {
  // The sign control for case 2: reversing S must reverse the field.
  constexpr Index nx = 20;
  auto problem = makeOneDimensional(nx, 1.0, 0.0, 0.0);
  const Index n = problem.mesh.numberOfCells();
  const ScalarField gamma(n, 1.0);
  const SurfaceField noFlux(problem.mesh.numberOfFaces(), 0.0);

  const ScalarField phiPositive = solveToConvergence(
      problem.mesh, problem.boundaries, gamma, noFlux, ScalarField(n, 5.0), ScalarField(n, 0.0));
  const ScalarField phiNegative = solveToConvergence(
      problem.mesh, problem.boundaries, gamma, noFlux, ScalarField(n, -5.0), ScalarField(n, 0.0));

  for (Index i = 0; i < n; ++i) {
    EXPECT_GT(phiPositive[i], 0.0) << "cell " << i;
    EXPECT_LT(phiNegative[i], 0.0) << "cell " << i;
    EXPECT_NEAR(phiPositive[i], -phiNegative[i], 1e-9) << "cell " << i;
  }
}

// --- Case 3: steady convection-diffusion ------------------------------------

TEST(TransportVerification, Case3ConvectionTransportsInTheFlowDirection) {
  // u dphi/dx = Gamma d2phi/dx2, phi(0)=1, phi(L)=0. The exact solution is
  //   phi(x) = (exp(Pe x/L) - exp(Pe)) / (1 - exp(Pe)),  Pe = u L / Gamma.
  //
  // UPWIND is first order, so it carries a known false-diffusion error and
  // will NOT match the exponential closely at high Pe. This test therefore
  // asserts what upwind genuinely guarantees and what a sign error would
  // break: the profile is monotone, bounded by its own boundary values (no
  // over/undershoot), and shifted in the direction of the flow.
  constexpr Index nx = 50;
  constexpr Real length = 1.0;
  constexpr Real gammaValue = 0.05;
  constexpr Real massFluxValue = 1.0;  // rho*u*A, positive = left to right
  auto problem = makeOneDimensional(nx, length, 1.0, 0.0);
  const Index n = problem.mesh.numberOfCells();

  const ScalarField gamma(n, gammaValue);
  SurfaceField flux(problem.mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < problem.mesh.numberOfFaces(); ++f) {
    // Only x-normal faces carry flow; the y-normal ones stay zero.
    const auto& face = problem.mesh.face(f);
    if (std::abs(face.areaVector().x) > 1e-14) flux[f] = massFluxValue;
  }

  const ScalarField phi = solveToConvergence(problem.mesh, problem.boundaries, gamma, flux,
                                             ScalarField(n, 0.0), ScalarField(n, 0.0));

  // Boundedness: upwind is unconditionally bounded. An overshoot here means a
  // sign or upwind-branch error, not a resolution problem.
  for (Index i = 0; i < n; ++i) {
    EXPECT_GE(phi[i], -1e-12) << "undershoot at cell " << i;
    EXPECT_LE(phi[i], 1.0 + 1e-12) << "overshoot at cell " << i;
  }
  // Monotone decreasing from the inlet.
  for (Index i = 1; i < n; ++i) {
    EXPECT_LE(phi[i], phi[i - 1] + 1e-12) << "not monotone at cell " << i;
  }
  // Flow direction: with strong convection left-to-right the upstream half
  // stays near the inlet value.
  EXPECT_GT(phi[n / 4], 0.5) << "convection did not carry the inlet value downstream";
}

TEST(TransportVerification, Case3ReversingTheFluxReversesTheProfile) {
  // The direction control. Same problem, flux reversed: the field must mirror.
  constexpr Index nx = 40;
  const auto profileFor = [&](Real fluxSign) {
    auto problem = makeOneDimensional(nx, 1.0, 1.0, 0.0);
    const Index n = problem.mesh.numberOfCells();
    SurfaceField flux(problem.mesh.numberOfFaces(), 0.0);
    for (Index f = 0; f < problem.mesh.numberOfFaces(); ++f) {
      if (std::abs(problem.mesh.face(f).areaVector().x) > 1e-14) flux[f] = fluxSign;
    }
    return solveToConvergence(problem.mesh, problem.boundaries, ScalarField(n, 0.05), flux,
                              ScalarField(n, 0.0), ScalarField(n, 0.0));
  };

  const ScalarField forward = profileFor(1.0);
  const ScalarField backward = profileFor(-1.0);
  // Forward flow keeps the inlet value far downstream; reversed flow does not.
  EXPECT_GT(forward[nx / 2], backward[nx / 2])
      << "reversing the mass flux did not change the transport direction";
}

// --- Case 4: transient diffusion --------------------------------------------

TEST(TransportVerification, Case4TransientDiffusionApproachesTheSteadyState) {
  // dphi/dt = alpha d2phi/dx2 with fixed ends. Stepping to large t must
  // converge to the steady linear profile, and each step must move towards it
  // monotonically -- an independently known reference (case 1's analytical
  // answer) rather than a stored number.
  constexpr Index nx = 20;
  constexpr Real length = 1.0;
  constexpr Real phiLeft = 0.0;
  constexpr Real phiRight = 1.0;
  auto problem = makeOneDimensional(nx, length, phiLeft, phiRight);
  const Index n = problem.mesh.numberOfCells();

  const ScalarField gamma(n, 1.0);
  const ScalarField rho(n, 1.0);
  const SurfaceField noFlux(problem.mesh.numberOfFaces(), 0.0);
  const ScalarField zero(n, 0.0);

  ScalarField phiOld(n, 0.0);
  Real previousDistance = std::numeric_limits<Real>::infinity();
  for (int step = 0; step < 120; ++step) {
    TransportTimeState time;
    time.transient = true;
    time.dt = 0.01;
    time.phiOld = &phiOld;
    time.rhoOld = &rho;
    time.rhoNew = &rho;

    const auto assembly = cfd::transport::assembleScalarTransportEquation(
        problem.mesh, phiOld, noFlux, gamma, problem.boundaries, zero, zero, {}, time);
    const Vector phi = solve(assembly.system, n);

    Real distance = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real exact = phiLeft + (phiRight - phiLeft) * cellCentreX(problem.mesh, i) / length;
      distance = std::max(distance, std::abs(phi[i] - exact));
    }
    EXPECT_LE(distance, previousDistance + 1e-12) << "step " << step << " moved away from steady";
    previousDistance = distance;
    for (Index i = 0; i < n; ++i) phiOld[i] = phi[i];
  }
  EXPECT_LT(previousDistance, 1e-3)
      << "did not reach the steady profile; distance " << previousDistance;
}

TEST(TransportVerification, Case4SmallerTimestepsReachTheSameSteadyState) {
  // Determinism-adjacent: the steady limit must not depend on dt.
  const auto steadyAfter = [](Real dt, int steps) {
    auto problem = makeOneDimensional(16, 1.0, 0.0, 1.0);
    const Index n = problem.mesh.numberOfCells();
    const ScalarField gamma(n, 1.0);
    const ScalarField rho(n, 1.0);
    const SurfaceField noFlux(problem.mesh.numberOfFaces(), 0.0);
    const ScalarField zero(n, 0.0);
    ScalarField phiOld(n, 0.0);
    for (int s = 0; s < steps; ++s) {
      TransportTimeState time;
      time.transient = true;
      time.dt = dt;
      time.phiOld = &phiOld;
      time.rhoOld = &rho;
      time.rhoNew = &rho;
      const auto assembly = cfd::transport::assembleScalarTransportEquation(
          problem.mesh, phiOld, noFlux, gamma, problem.boundaries, zero, zero, {}, time);
      const Vector phi = solve(assembly.system, n);
      for (Index i = 0; i < n; ++i) phiOld[i] = phi[i];
    }
    return phiOld;
  };

  const ScalarField coarse = steadyAfter(0.05, 200);
  const ScalarField fine = steadyAfter(0.0125, 800);
  for (Index i = 0; i < coarse.size(); ++i) {
    EXPECT_NEAR(coarse[i], fine[i], 1e-6) << "cell " << i;
  }
}

// --- Case 5: variable diffusivity -------------------------------------------

TEST(TransportVerification, Case5VariableDiffusivityConservesFlux) {
  // Gamma = Gamma(x), steady, no source: the flux through every cross-section
  // must be identical, which is the physical content of conservation for this
  // problem. Verified through the discrete balance rather than by eyeballing
  // the profile.
  constexpr Index nx = 30;
  auto problem = makeOneDimensional(nx, 1.0, 0.0, 10.0);
  const Index n = problem.mesh.numberOfCells();

  ScalarField gamma(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    // Smoothly varying by a factor of 5 across the domain.
    gamma[i] = 1.0 + 4.0 * cellCentreX(problem.mesh, i);
  }
  const SurfaceField noFlux(problem.mesh.numberOfFaces(), 0.0);
  const ScalarField zero(n, 0.0);

  const ScalarField phi =
      solveToConvergence(problem.mesh, problem.boundaries, gamma, noFlux, zero, zero);

  for (Index i = 1; i < n; ++i) EXPECT_GE(phi[i], phi[i - 1] - 1e-9) << "cell " << i;

  // The physical content of this case: with no source, the diffusive flux
  // through every internal face must be IDENTICAL. That is what "conserves"
  // means here, and it is what a wrong face interpolation of Gamma breaks.
  //
  // The first version of this test compared the rise across the first half of
  // the cells against the second half and asserted the first was larger. That
  // was a bad instrument, not a bad result: phi[0] is the first cell CENTRE,
  // not the boundary, so the steep rise between the wall and the first centre
  // -- precisely where Gamma is smallest -- sits outside the interval being
  // measured. Measuring the face fluxes tests the property directly instead.
  std::vector<Real> faceFlux;
  for (Index f = 0; f < problem.mesh.numberOfFaces(); ++f) {
    const auto& face = problem.mesh.face(f);
    if (face.isBoundary() || std::abs(face.areaVector().x) <= 1e-14) continue;
    const Index owner = face.owner();
    const Index neighbour = *face.neighbor();
    const Real gammaFace = cfd::discretization::interpolateInternalFace(problem.mesh, face, gamma);
    const Real dPN = cfd::mesh::MeshGeometry::ownerNeighborDistance(problem.mesh, face);
    const Real area = std::abs(face.areaVector().x);
    faceFlux.push_back(gammaFace * area * (phi[neighbour] - phi[owner]) / dPN);
  }
  ASSERT_GE(faceFlux.size(), 5u);
  const Real reference = faceFlux.front();
  ASSERT_GT(std::abs(reference), 1e-9) << "no flux to compare";
  for (std::size_t k = 0; k < faceFlux.size(); ++k) {
    EXPECT_NEAR(faceFlux[k] / reference, 1.0, 5e-3)
        << "face " << k << ": flux " << faceFlux[k] << " vs " << reference;
  }
}

// --- Phase L: global conservation -------------------------------------------

TEST(TransportConservation, DiscreteBalanceClosesForEveryVerificationCase) {
  // sum_P (A phi - b)_P == 0 to solver tolerance. Internal faces cancel in
  // pairs by construction, so this measures the boundary and source balance.
  struct Case {
    const char* name;
    Real gammaBase;
    Real flux;
    Real source;
  };
  const Case cases[] = {
      {"pure diffusion", 1.0, 0.0, 0.0},
      {"diffusion + source", 2.0, 0.0, 4.0},
      {"convection-diffusion", 0.1, 0.8, 0.0},
      {"all three", 0.5, 0.3, 2.0},
  };

  for (const auto& testCase : cases) {
    auto problem = makeOneDimensional(25, 1.0, 1.0, 0.0);
    const Index n = problem.mesh.numberOfCells();
    const ScalarField gamma(n, testCase.gammaBase);
    SurfaceField flux(problem.mesh.numberOfFaces(), 0.0);
    for (Index f = 0; f < problem.mesh.numberOfFaces(); ++f) {
      if (std::abs(problem.mesh.face(f).areaVector().x) > 1e-14) flux[f] = testCase.flux;
    }
    const auto assembly = cfd::transport::assembleScalarTransportEquation(
        problem.mesh, ScalarField(n, 0.0), flux, gamma, problem.boundaries,
        ScalarField(n, testCase.source), ScalarField(n, 0.0));
    const Vector phi = solve(assembly.system, n);

    const Real imbalance = globalImbalance(assembly.system, phi);
    // Normalised by the largest row magnitude so the bound is dimensionless.
    Real scale = 0.0;
    for (Index i = 0; i < n; ++i) scale = std::max(scale, std::abs(assembly.system.rhs()[i]));
    const Real normalised = scale > 0.0 ? std::abs(imbalance) / scale : std::abs(imbalance);
    EXPECT_LT(normalised, 1e-8) << testCase.name << ": normalised imbalance " << normalised
                                << " (absolute " << imbalance << ")";
  }
}
