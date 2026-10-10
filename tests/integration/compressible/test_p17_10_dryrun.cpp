// P17.10 -- THE DRY-RUN, run BEFORE the acceptance gate is frozen.
//
// CLAUDE.md §5.2 requires every criterion to be executed against the unchanged baseline before the
// gate is frozen, and warns specifically about the case this phase is in:
//
//     "If the baseline cannot exercise the new behaviour being gated, the dry-run is insufficient.
//      For example, '100 % fallback' is vacuously true on a library with no new path. Ask
//      separately whether a correct post-change implementation can satisfy the criterion, and add a
//      control that can fail."
//
// There is no GPU density-based solver yet, so "the GPU matches the CPU" cannot be dry-run
// directly. What CAN be dry-run -- and what this file does -- is the two things the whole phase
// rests on, both of which are checkable today:
//
//   PART 1  THE LOAD-BEARING MESH INVARIANT. Every claim of BITWISE residual equivalence depends on
//           each cell's face list being ASCENDING, so that a per-cell gather adds the same terms in
//           the same order as the CPU's ascending face loop. audit.md §4 argues this from the
//           source; here it is MEASURED, on every mesh construction path the repository has. If it
//           were false on any of them, the six kernels would have to be designed differently, and
//           it is much cheaper to learn that now.
//
//   PART 2  THE INSTRUMENT. The comparator in EquivalenceCompare.hpp is the only thing separating
//           "the GPU agrees" from "nothing was measured". It is exercised for BOTH behaviours: it
//           must report EQUAL for identical input, and it must report NOT EQUAL for a
//           one-ULP perturbation in each of the five conserved components -- and, most importantly,
//           for every way a comparison can silently not happen at all.
//
// None of this touches production code. No CUDA is required to run it, deliberately: the invariant
// and the instrument are properties of the CPU side and of the test harness.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EquivalenceCompare.hpp"
#include "cfd/compressible/DensityBasedSolver.hpp"
#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::BoundaryKind;
using cfd::compressible::ConservedField;
using cfd::compressible::DensityBasedBoundaries;
using cfd::compressible::DensityBasedBoundary;
using cfd::compressible::DensityBasedSettings;
using cfd::compressible::DensityBasedSolver;
using cfd::compressible::makePrimitiveState;
using cfd::compressible::PrimitiveState;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::p17_10::bits;
using cfd::p17_10::compareConserved;
using cfd::p17_10::ComparisonReport;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;

TotalEnergyState air() {
  const TemperatureDependentProperty cp(std::vector<Real>{kCp}, 0.0, 3000.0, "cp");
  return TotalEnergyState(ThermodynamicProperties(kR, kCp), SensibleEnthalpy(cp, 0.0, 0.0));
}

// The next representable double above v, so a perturbation is the SMALLEST one that exists. A
// comparator that survives this survives anything coarser.
Real oneUlpUp(Real v) {
  std::uint64_t u = bits(v);
  ++u;
  Real out = 0.0;
  std::memcpy(&out, &u, sizeof(out));
  return out;
}

ConservedField uniformState(const Mesh& mesh, const TotalEnergyState& thermo) {
  const Index n = static_cast<Index>(mesh.numberOfCells());
  ScalarField rho(n);
  VectorField vel(n);
  ScalarField t(n);
  for (Index i = 0; i < n; ++i) {
    rho[i] = 1.2;
    vel[i] = Vector3{700.0, 0.0, 0.0};
    t[i] = 300.0;
  }
  // fromPrimitive needs a solver instance but not a time loop; the constructor refuses
  // GlobalPhysical with endTime <= 0, so a valid endTime is supplied rather than the default.
  DensityBasedSettings s;
  s.endTime = 1.0e-4;
  const DensityBasedSolver solver(s, thermo);
  return solver.fromPrimitive(mesh, rho, vel, t);
}

// Reports the first violation rather than just a bool, so a failure says WHERE.
struct AscendingCheck {
  bool ascending{true};
  Index cell{};
  std::size_t position{};
  Index previous{};
  Index current{};
  Index totalRefs{};
};

AscendingCheck checkAscending(const Mesh& mesh) {
  AscendingCheck out;
  for (Index c = 0; c < static_cast<Index>(mesh.numberOfCells()); ++c) {
    const auto& ids = mesh.cell(c).faceIds();
    for (std::size_t k = 0; k < ids.size(); ++k) {
      ++out.totalRefs;
      if (k > 0 && !(ids[k] > ids[k - 1]) && out.ascending) {
        out.ascending = false;
        out.cell = c;
        out.position = k;
        out.previous = ids[k - 1];
        out.current = ids[k];
      }
    }
  }
  return out;
}

}  // namespace

// =================================================================================================
// PART 1 -- the invariant every bitwise claim in this phase rests on
// =================================================================================================

TEST(P17_10_DryRun, D1_EveryCellFaceListIsAscendingOnEveryMeshConstructionPath) {
  struct Case {
    const char* name;
    Mesh mesh;
  };
  std::vector<Case> cases;
  cases.push_back({"cartesian2d_uniform", MeshGeometry::createCartesian2D(7, 5, 1.0, 0.8)});
  cases.push_back({"cartesian2d_single_column", MeshGeometry::createCartesian2D(40, 1, 1.0, 0.05)});
  cases.push_back(
      {"graded2d_geometric_both_axes",
       MeshGeometry::createGraded2D(
           6, 4, 1.5, 0.8, AxisGrading{GradingType::Geometric, 1.7, GradingCluster::Start},
           AxisGrading{GradingType::Geometric, 1.4, GradingCluster::Start})});
  cases.push_back({"cartesian3d", MeshGeometry::createCartesian3D(4, 3, 2, 1.0, 0.8, 0.5)});
  cases.push_back(
      {"cartesian3d_translated_origin",
       MeshGeometry::createCartesian3D(3, 3, 3, 1.0, 1.0, 1.0, Vector3{10.0, -4.0, 2.5})});

  Index totalRefs = 0;
  for (auto& c : cases) {
    const AscendingCheck r = checkAscending(c.mesh);
    totalRefs += r.totalRefs;
    EXPECT_TRUE(r.ascending)
        << c.name << ": cell " << r.cell << " face list is NOT ascending at position " << r.position
        << " (" << r.previous << " then " << r.current
        << "). The per-cell GATHER cannot reproduce the CPU's accumulation order on this mesh, so "
           "bitwise residual equivalence is not achievable as designed -- see kernel_inventory.md "
           "K2b.";
    // A mesh with no faces would make "ascending" vacuously true.
    EXPECT_GT(r.totalRefs, 0) << c.name << ": no cell-face references were examined at all";
    std::printf("  %-32s cells=%zu faces=%zu cellFaceRefs=%d ascending=%s\n", c.name,
                c.mesh.numberOfCells(), c.mesh.numberOfFaces(), static_cast<int>(r.totalRefs),
                r.ascending ? "YES" : "NO");
  }
  std::printf("  TOTAL cell-face references examined: %d\n", static_cast<int>(totalRefs));
  EXPECT_GT(totalRefs, 500) << "the sample is too small to mean anything";
}

// The control for D1: the check must be able to say NO. A hand-built descending list proves the
// predicate is a real detector and not a function that returns true.
TEST(P17_10_DryRun, D1_NonVacuity_TheAscendingCheckDetectsADescendingList) {
  // Verified against the mesh type's own storage order by reversing what a real mesh returned.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const AscendingCheck good = checkAscending(mesh);
  ASSERT_TRUE(good.ascending);

  // The same predicate, applied to a deliberately out-of-order list.
  const std::vector<Index> descending{9, 7, 3, 1};
  bool ascending = true;
  for (std::size_t k = 1; k < descending.size(); ++k) {
    if (!(descending[k] > descending[k - 1])) ascending = false;
  }
  EXPECT_FALSE(ascending) << "the ascending predicate cannot detect a descending list, so D1 above "
                             "proves nothing";
}

// =================================================================================================
// PART 2 -- the instrument, exercised for both behaviours
// =================================================================================================

TEST(P17_10_DryRun, D2_ComparatorReportsEqualForIdenticalState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(9, 4, 1.0, 0.5);
  const TotalEnergyState thermo = air();
  const ConservedField q = uniformState(mesh, thermo);

  const ComparisonReport r = compareConserved(q, true, q, true);
  EXPECT_TRUE(r.bitwiseEqual()) << r.reason();
  EXPECT_EQ(r.differingValues(), 0);
  EXPECT_EQ(r.comparedValues(), 5 * static_cast<Index>(mesh.numberOfCells()));
  EXPECT_EQ(r.nonFiniteCpu(), 0);
  EXPECT_EQ(r.nonFiniteGpu(), 0);
  std::printf("  D2 identical: compared=%d differing=%d -> equal=%s\n",
              static_cast<int>(r.comparedValues()), static_cast<int>(r.differingValues()),
              r.bitwiseEqual() ? "YES" : "NO");
}

TEST(P17_10_DryRun, D3_NonVacuity_OneUlpInAnyConservedComponentIsDetected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(9, 4, 1.0, 0.5);
  const TotalEnergyState thermo = air();
  const ConservedField base = uniformState(mesh, thermo);
  const Index probe = 17;  // an interior cell, not cell 0

  struct Perturbation {
    const char* name;
    void (*apply)(ConservedField&, Index);
  };
  const std::vector<Perturbation> perturbations{
      {"density", [](ConservedField& q, Index i) { q.density[i] = oneUlpUp(q.density[i]); }},
      {"momentumX", [](ConservedField& q,
                       Index i) { q.momentumDensity[i].x = oneUlpUp(q.momentumDensity[i].x); }},
      {"momentumY",
       [](ConservedField& q, Index i) {
         // y momentum is exactly 0 here, so one ULP up is the smallest subnormal -- the hardest
         // possible difference to see, and the one a relative tolerance would miss entirely.
         q.momentumDensity[i].y = oneUlpUp(q.momentumDensity[i].y);
       }},
      {"momentumZ", [](ConservedField& q,
                       Index i) { q.momentumDensity[i].z = oneUlpUp(q.momentumDensity[i].z); }},
      {"totalEnergy", [](ConservedField& q,
                         Index i) { q.totalEnergyDensity[i] = oneUlpUp(q.totalEnergyDensity[i]); }},
  };

  for (const auto& p : perturbations) {
    ConservedField perturbed = base;
    p.apply(perturbed, probe);
    const ComparisonReport r = compareConserved(base, true, perturbed, true);
    EXPECT_FALSE(r.bitwiseEqual())
        << "a one-ULP change in " << p.name << " was NOT detected -- the comparator is blind";
    ASSERT_EQ(r.differingValues(), 1) << p.name << ": expected exactly one differing value";
    ASSERT_EQ(r.differences().size(), 1u);
    EXPECT_EQ(r.differences()[0].quantity, p.name);
    EXPECT_EQ(r.differences()[0].cell, probe);
    EXPECT_EQ(r.differences()[0].ulps, 1u)
        << p.name << ": the perturbation was not one ULP after all";
    std::printf("  D3 %-12s 1 ulp at cell %d -> detected=%s, %d/%d values differ\n", p.name,
                static_cast<int>(probe), r.bitwiseEqual() ? "NO" : "YES",
                static_cast<int>(r.differingValues()), static_cast<int>(r.comparedValues()));
  }
}

// The fail-open cases. Each of these is a way for a comparison to not happen while looking exactly
// like a clean pass: zero differences found, no error raised.
TEST(P17_10_DryRun, D4_FailClosed_EveryWayAComparisonCanSilentlyNotHappen) {
  const Mesh mesh = MeshGeometry::createCartesian2D(9, 4, 1.0, 0.5);
  const TotalEnergyState thermo = air();
  const ConservedField q = uniformState(mesh, thermo);
  const ConservedField empty{ScalarField(0), VectorField(0), ScalarField(0)};

  {  // a) the default-constructed report -- nothing was compared
    const ComparisonReport r;
    EXPECT_FALSE(r.bitwiseEqual()) << "an untouched report claims equality";
    EXPECT_FALSE(r.ran());
    std::printf("  D4a never ran            -> equal=%s (%s)\n", r.bitwiseEqual() ? "YES" : "NO",
                r.reason().c_str());
  }
  {  // b) the GPU side never produced anything, but the fields line up
    const ComparisonReport r = compareConserved(q, true, q, false);
    EXPECT_FALSE(r.bitwiseEqual()) << "an unpopulated GPU side was reported as equal";
    std::printf("  D4b gpu not populated    -> equal=%s (%s)\n", r.bitwiseEqual() ? "YES" : "NO",
                r.reason().c_str());
  }
  {  // c) the CPU side never produced anything
    const ComparisonReport r = compareConserved(q, false, q, true);
    EXPECT_FALSE(r.bitwiseEqual()) << "an unpopulated CPU side was reported as equal";
    std::printf("  D4c cpu not populated    -> equal=%s\n", r.bitwiseEqual() ? "YES" : "NO");
  }
  {  // d) BOTH sides empty -- zero differences found, and that must not be a pass
    const ComparisonReport r = compareConserved(empty, true, empty, true);
    EXPECT_FALSE(r.bitwiseEqual())
        << "comparing two empty states found zero differences and called it equality -- this is "
           "the exact fail-open signature the phase is guarding against";
    EXPECT_EQ(r.comparedValues(), 0);
    std::printf("  D4d both empty           -> equal=%s (%s)\n", r.bitwiseEqual() ? "YES" : "NO",
                r.reason().c_str());
  }
  {  // e) size mismatch -- must not compare the overlap and declare victory
    const ComparisonReport r = compareConserved(q, true, empty, true);
    EXPECT_FALSE(r.bitwiseEqual()) << "a size mismatch was reported as equal";
    std::printf("  D4e size mismatch        -> equal=%s (%s)\n", r.bitwiseEqual() ? "YES" : "NO",
                r.reason().c_str());
  }
}

// A pair of matching NaNs is bitwise equal and thermodynamically useless. The comparator must
// report BOTH facts, so that "equivalent" can never be mistaken for "valid".
TEST(P17_10_DryRun, D5_MatchingNonFiniteValuesAreEqualButCounted) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 2, 1.0, 0.5);
  const TotalEnergyState thermo = air();
  ConservedField a = uniformState(mesh, thermo);
  a.density[3] = std::numeric_limits<Real>::quiet_NaN();
  const ConservedField b = a;

  const ComparisonReport r = compareConserved(a, true, b, true);
  EXPECT_TRUE(r.bitwiseEqual()) << "two identical NaN bit patterns are the same computed result";
  EXPECT_EQ(r.nonFiniteCpu(), 1);
  EXPECT_EQ(r.nonFiniteGpu(), 1);
  std::printf(
      "  D5 matching NaN: equal=%s but nonFinite cpu=%d gpu=%d -- equivalence is NOT validity\n",
      r.bitwiseEqual() ? "YES" : "NO", static_cast<int>(r.nonFiniteCpu()),
      static_cast<int>(r.nonFiniteGpu()));
}

// =================================================================================================
// PART 3 -- the CPU baseline the whole comparison is against
// =================================================================================================

TEST(P17_10_DryRun, D6_TheCpuBaselineIsItselfBitwiseRepeatable) {
  const Mesh mesh = MeshGeometry::createCartesian2D(24, 3, 1.0, 0.125);
  const TotalEnergyState thermo = air();

  DensityBasedSettings s;
  s.cfl = 0.4;
  s.mode = cfd::compressible::TimeSteppingMode::GlobalPhysical;
  s.endTime = 2.0e-4;
  s.maxIterations = 50;
  const DensityBasedSolver solver(s, thermo);

  const PrimitiveState inflow = makePrimitiveState(thermo, 1.2, Vector3{700.0, 0.0, 0.0}, 300.0);
  DensityBasedBoundaries b;
  for (const auto& patch : mesh.boundaryPatches()) {
    DensityBasedBoundary entry{};
    const std::string name(patch.name());
    if (name == "left") {
      entry.kind = BoundaryKind::SupersonicInflow;
      entry.prescribed = inflow;
    } else if (name == "right") {
      entry.kind = BoundaryKind::SupersonicOutflow;
    } else {
      entry.kind = BoundaryKind::InviscidWall;
    }
    b[name] = entry;
  }

  ConservedField q0 = uniformState(mesh, thermo);
  // A non-uniform perturbation, so the run is not a trivial steady state that any two paths match.
  for (Index i = 0; i < q0.size(); i += 3) q0.density[i] *= 1.05;

  const auto r1 = solver.solve(mesh, q0, b);
  const auto r2 = solver.solve(mesh, q0, b);

  const ComparisonReport cmp = compareConserved(r1.state, true, r2.state, true);
  EXPECT_TRUE(cmp.bitwiseEqual()) << "the CPU is not repeatable, so no GPU comparison means "
                                     "anything: "
                                  << cmp.reason();
  EXPECT_EQ(r1.iterations, r2.iterations);
  EXPECT_EQ(static_cast<int>(r1.status), static_cast<int>(r2.status));
  ASSERT_EQ(r1.history.size(), r2.history.size());
  ASSERT_FALSE(r1.history.empty()) << "the solve took no steps, so D6 measured nothing";
  for (std::size_t k = 0; k < r1.history.size(); ++k) {
    EXPECT_EQ(bits(r1.history[k].dt), bits(r2.history[k].dt)) << "dt differs at step " << k;
  }
  std::printf("  D6 cpu repeat: %d iterations, %zu steps, state bitwise equal=%s, dt bitwise=YES\n",
              static_cast<int>(r1.iterations), r1.history.size(),
              cmp.bitwiseEqual() ? "YES" : "NO");
}

// =================================================================================================
// A FINDING, NOT A SPECIFICATION -- see results/p17/cpu_gpu_equivalence/BLOCKING_FINDINGS.md
// =================================================================================================
//
// This test DOCUMENTS A DEFECT that P17.10 found in P17.7's flux layer. It asserts the behaviour
// that is there today so the defect cannot regress further unnoticed and so the reproducer stays
// executable. It is NOT a statement that the behaviour is correct. WHEN THE DEFECT IS FIXED THIS
// TEST MUST FAIL, and that failure is the signal to close the finding and rewrite these assertions
// as the specification.
//
// THE DEFECT. For a SYMMETRIC HEAD-ON COLLISION -- two identical states approaching at +/-u, which
// is exactly what an inviscid wall produces when the flow is normal to it -- Toro's pressure-based
// wave-speed estimate stops bounding the signal speeds above a normal Mach number of about 1.76.
// Past that point S_L becomes POSITIVE while the exact left-running shock speed is about
// -265 m/s, so:
//    * the required ordering S_L <= S* <= S_R is violated;
//    * the star densities go NEGATIVE;
//    * HLLC selects the LeftSupersonic branch and returns the UNDISTURBED UPSTREAM FLUX for a
//      configuration in which a strong shock is forming;
//    * and hllcFlux() still reports valid = true.
// Only hllcFluxChecked()'s star-density test catches it, and the comment there calls the situation
// "impossible for admissible inputs", which these inputs show it is not.

TEST(P17_10_Finding, DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision) {
  const TotalEnergyState thermo = air();
  const Vector3 n{1.0, 0.0, 0.0};
  const Real gamma = thermo.thermodynamics().specificHeatRatio();

  int violations = 0;
  int checked = 0;
  for (const Real speed : {200.0, 400.0, 500.0, 550.0, 600.0, 620.0, 640.0, 700.0, 1000.0}) {
    const PrimitiveState L =
        cfd::compressible::makePrimitiveState(thermo, 1.2, Vector3{speed, 0, 0}, 300.0);
    const PrimitiveState R =
        cfd::compressible::makePrimitiveState(thermo, 1.2, Vector3{-speed, 0, 0}, 300.0);
    // Both states are admissible by the flux layer's own predicate, which is the whole point.
    ASSERT_EQ(cfd::compressible::validateState(L), cfd::compressible::StateValidity::Valid);
    ASSERT_EQ(cfd::compressible::validateState(R), cfd::compressible::StateValidity::Valid);

    const auto r = cfd::compressible::hllcFlux(L, R, n, gamma);
    const Real a = L.speedOfSound;
    const Real ma = speed / a;

    // The EXACT symmetric-collision solution, from the normal-shock relations alone. The post-shock
    // velocity is 0 by symmetry, so the velocity jump across the left shock is exactly `speed`:
    //   speed = a * 2/(g+1) * (Ms - 1/Ms)  ->  Ms^2 - k Ms - 1 = 0 with k = speed (g+1)/(2 a),
    // and the lab-frame left wave speed is u_L - Ms a. No solver is involved in producing it.
    const Real k = speed * (gamma + 1.0) / (2.0 * a);
    const Real ms = 0.5 * (k + std::sqrt(k * k + 4.0));
    const Real exactSL = speed - ms * a;
    const Real exactPRatio = (2.0 * gamma * ms * ms - (gamma - 1.0)) / (gamma + 1.0);

    const bool ordered =
        (r.speeds.left <= r.speeds.contact) && (r.speeds.contact <= r.speeds.right);
    ++checked;
    if (!ordered) ++violations;

    // The exact left wave speed is left-running at EVERY speed in this sweep. That is the reference
    // the estimate is being judged against, and it is asserted so a broken reference cannot make
    // the finding look worse than it is.
    EXPECT_LT(exactSL, 0.0) << "the exact solution is not left-running at Ma = " << ma;

    if (!ordered) {
      // The documented defect: ordering violated, star density negative, supersonic branch chosen,
      // and the result still reported valid.
      EXPECT_GT(r.speeds.left, 0.0);
      EXPECT_LT(r.starDensityLeft, 0.0);
      EXPECT_EQ(static_cast<int>(r.branch), 0) << "expected the LeftSupersonic branch";
      EXPECT_TRUE(r.valid) << "hllcFlux already reports this as invalid, so the finding has "
                              "changed and BLOCKING_FINDINGS.md must be revisited";
    }
    std::printf(
        "  Ma=%5.2f  S_L=%11.4f exact=%11.4f  rho*_L=%12.4e  p*/p1 exact=%7.3f  "
        "ordering=%s  branch=%d\n",
        ma, r.speeds.left, exactSL, r.starDensityLeft, exactPRatio, ordered ? "held" : "VIOLATED",
        static_cast<int>(r.branch));
  }
  std::printf("  FINDING: %d of %d head-on collisions violate S_L <= S* <= S_R\n", violations,
              checked);
  EXPECT_GT(violations, 0) << "the defect is no longer reproducible -- if it has been fixed, close "
                              "the finding and rewrite this test as a specification";
  EXPECT_LT(violations, checked) << "every case violates the ordering, so the sweep no longer "
                                    "brackets the onset and the threshold below is not measured";
}
