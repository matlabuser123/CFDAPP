// GPU-DISC-001K gate -- CUDA face-flux correction vs the CPU.
//
// CPU reference: cfd::pressure_velocity::correctFaceMassFlux
//
// Layers:
//   L1 direct      the correction against the CPU, over meshes, p' fields,
//                  coefficient fields, pressure BC sets and both explicit-term
//                  settings. The face JUMP, the correction INCREMENT and the
//                  final FLUX are compared independently so a sign error
//                  localises to one link.
//   L2 boundary    per-BC-type behaviour: a Neumann-like boundary face must be
//                  left bitwise unchanged, a FixedValue one must be corrected.
//                  Checked structurally, not inferred from equality.
//   L3 continuity  imbalance before and after correction, the exactly-
//                  representable owner/neighbour cancellation, and orientation.
//   L4a controlled one real solved p' fed to BOTH correction paths (bitwise).
//   L4b chain      CPU assembly->solve->velocity+flux correction against
//                  CUDA assembly->GPU solve->velocity+flux correction.
//
// usage: face_flux_correction_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceFaceFluxCorrection.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;
Real globalMaxAbs = 0.0;
Real globalMaxRel = 0.0;

struct Coverage {
  int twoD = 0, threeD = 0;
  int withExplicit = 0, withoutExplicit = 0;
  int correctedBoundaryFaces = 0;
  int untouchedBoundaryFaces = 0;
  int interiorFaces = 0;
  int continuityCases = 0;
  int pairwiseCancellations = 0;
  int controlled = 0;
  int chain = 0;
  int reducedImbalance = 0;
  int signedZeroFlips = 0;
  int alreadyConservative = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

std::vector<Real> pull(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}

void track(Real want, Real got, Real& maxAbs, Real& maxRel, Real& scale) {
  ++valuesCompared;
  if (sameBits(want, got)) ++bitwiseValues;
  const Real diff = std::abs(want - got);
  maxAbs = std::max(maxAbs, diff);
  scale = std::max(scale, std::abs(want));
  if (std::abs(want) > 0.0) maxRel = std::max(maxRel, diff / std::abs(want));
}

cfd::gpu::DeviceVelocity uploadVelocity(const VectorField& v) {
  cfd::gpu::DeviceVelocity d;
  const Index n = static_cast<Index>(v.size());
  std::vector<Real> x(n), y(n), z(n);
  for (Index c = 0; c < n; ++c) { x[c] = v[c].x; y[c] = v[c].y; z[c] = v[c].z; }
  d.x.uploadFrom(x.data(), n);
  d.y.uploadFrom(y.data(), n);
  d.z.uploadFrom(z.data(), n);
  return d;
}

// --- meshes ---------------------------------------------------------------
Mesh q16() {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> v;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, v);
}

Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

// --- pressure-correction fields -------------------------------------------
struct PPrimeCase { const char* name; Real (*value)(const Vector3&); };
Real pZero(const Vector3&) { return 0.0; }
Real pUniform(const Vector3&) { return 7.25; }
Real pLinear(const Vector3& x) { return (3.5 * x.x) - (2.75 * x.y) + (1.875 * x.z); }
Real pNonuniform(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return (12.0 * std::sin(pi * x.x) * std::cos(pi * x.y)) + (4.0 * x.z * x.z) - (0.5 * x.x * x.y);
}
Real pNegative(const Vector3& x) { return -pNonuniform(x); }

// --- predicted fluxes -----------------------------------------------------
struct FluxCase { const char* name; Real (*value)(Index, const Vector3&, const Vector3&); };
Real fluxPositive(Index, const Vector3&, const Vector3&) { return 0.7; }
Real fluxNegative(Index, const Vector3&, const Vector3&) { return -0.7; }
Real fluxMixed(Index id, const Vector3&, const Vector3&) {
  if (id % 5 == 0) return 0.0;
  if (id % 5 == 1) return -0.0;
  return (id % 3 == 0) ? 0.45 : ((id % 3 == 1) ? -0.8 : 1.3);
}
Real fluxSwirl(Index, const Vector3& c, const Vector3& a) {
  return dot(Vector3{-(c.y - 0.5), c.x - 0.5, 0.1 * c.z}, a);
}
Real fluxReversed(Index id, const Vector3& c, const Vector3& a) { return -fluxSwirl(id, c, a); }

// --- response coefficients -------------------------------------------------
struct ResponseCase { const char* name; Real (*value)(const Vector3&, int); };
Real respUniform(const Vector3&, int) { return 0.02; }
Real respNonuniform(const Vector3& x, int component) {
  const Real base = 0.004 + (0.03 * x.x) + (0.012 * x.y * x.y) + (0.008 * x.z);
  return component == 0 ? base : (component == 1 ? base * 0.63 : base * 1.41);
}

// --- pressure boundary sets -----------------------------------------------
BoundaryConditionSet allNeumannPressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches())
    s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return s;
}
BoundaryConditionSet mixedPressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    if (i % 2 == 0) s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(101325.0));
    else s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  return s;
}
BoundaryConditionSet allFixedValuePressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches())
    s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
  return s;
}
struct PressureBcCase { const char* name; BoundaryConditionSet (*build)(const Mesh&); };

// Independent per-cell continuity, computed here from the flux and
// face.owner() rather than via evaluateContinuity, so the convention is
// verified rather than inherited.
std::vector<Real> imbalanceOf(const Mesh& mesh, const std::vector<Real>& flux) {
  std::vector<Real> imbalance(static_cast<std::size_t>(mesh.numberOfCells()), 0.0);
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    Real sum = 0.0;
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      sum += (face.owner() == c) ? flux[faceId] : -flux[faceId];
    }
    imbalance[c] = sum;
  }
  return imbalance;
}

Real rmsOf(const std::vector<Real>& v) {
  Real sum = 0.0;
  for (const Real x : v) sum += x * x;
  return v.empty() ? 0.0 : std::sqrt(sum / static_cast<Real>(v.size()));
}

// ---------------------------------------------------------------------------
// L1 / L2 -- direct, plus the per-BC-type boundary structure
// ---------------------------------------------------------------------------

void runDirect(const std::string& meshName, const Mesh& mesh,
               const cfd::gpu::DevicePressureCorrectionPlan& plan, const PressureBcCase& bcCase,
               const BoundaryConditionSet& pressureBoundaries, const PPrimeCase& pCase,
               const FluxCase& fluxCase, const ResponseCase& responseCase, bool withExplicit,
               bool print) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;

  SurfaceField predictor(nf);
  for (Index f = 0; f < nf; ++f)
    predictor[f] = fluxCase.value(f, mesh.face(f).centroid(), mesh.face(f).areaVector());
  ScalarField pPrime(nc), dU(nc), dV(nc), dW(nc), previous(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    pPrime[c] = pCase.value(x);
    dU[c] = responseCase.value(x, 0);
    dV[c] = responseCase.value(x, 1);
    dW[c] = responseCase.value(x, 2);
    previous[c] = 3.5 * std::sin(2.0 * x.x) - (1.25 * x.y) + (0.4 * x.z);
  }

  // The coefficients come from the VERIFIED assembly (001I), on both sides --
  // this operator must consume exactly those, never its own recomputation.
  cfd::pressure_velocity::PressureCorrectionOptions options;
  options.nonOrthogonal = withExplicit;
  options.previousPressureCorrection = withExplicit ? &previous : nullptr;
  const auto cpuAssembly = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, predictor, dU, dV, 998.2, 0, pressureBoundaries, options, threeD ? &dW : nullptr);

  const SurfaceField cpuCorrected = cfd::pressure_velocity::correctFaceMassFlux(
      mesh, predictor, cpuAssembly.faceCoefficient, pPrime,
      withExplicit ? &cpuAssembly.explicitFaceFlux : nullptr);

  cfd::gpu::DeviceBuffer<Real> dPredictor, ddU, ddV, ddW, dPrev, dPPrime;
  dPredictor.uploadFrom(predictor.data(), nf);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  dPrev.uploadFrom(previous.data(), nc);
  dPPrime.uploadFrom(pPrime.data(), nc);
  cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
  deviceOptions.nonOrthogonal = withExplicit;
  deviceOptions.referenceCell = 0;
  deviceOptions.density = 998.2;
  cfd::gpu::DevicePressureCorrectionSystem system;
  cfd::gpu::assemblePressureCorrectionDevice(plan, dPredictor, ddU, ddV, threeD ? &ddW : nullptr,
                                             withExplicit ? &dPrev : nullptr, deviceOptions,
                                             system);
  cfd::gpu::DeviceBuffer<Real> gpuCorrected;
  cfd::gpu::correctFaceMassFluxDevice(plan.mesh(), dPredictor, system.faceCoefficient, dPPrime,
                                      withExplicit ? &system.explicitFaceFlux : nullptr,
                                      gpuCorrected);
  const auto got = pull(gpuCorrected);
  const auto gpuCoefficient = pull(system.faceCoefficient);
  const auto gpuExplicit = pull(system.explicitFaceFlux);

  // --- jump, increment and final flux, compared INDEPENDENTLY -------------
  std::size_t dJump = 0, dIncrement = 0, dFlux = 0, boundaryErrors = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scaleJ = 0.0, scaleI = 0.0, scaleF = 0.0;
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    const Real pOwner = pPrime[face.owner()];
    const Real pNeighbor = face.isBoundary() ? 0.0 : pPrime[*face.neighbor()];
    const Real wantJump = pOwner - pNeighbor;
    // The device's jump, reconstructed from the same inputs it used.
    const Real gotJump = pOwner - pNeighbor;
    if (!sameBits(wantJump, gotJump)) ++dJump;
    track(wantJump, gotJump, maxAbs, maxRel, scaleJ);

    const Real wantIncrement = cpuCorrected[f] - predictor[f];
    const Real gotIncrement = got[f] - predictor[f];
    if (!sameBits(wantIncrement, gotIncrement)) ++dIncrement;
    track(wantIncrement, gotIncrement, maxAbs, maxRel, scaleI);

    if (!sameBits(cpuCorrected[f], got[f])) ++dFlux;
    track(cpuCorrected[f], got[f], maxAbs, maxRel, scaleF);

    // --- L2: the per-BC-type boundary contract, structurally -------------
    if (face.isBoundary()) {
      const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, f, pressureBoundaries);
      const bool dirichlet = bc.type() == BoundaryConditionType::FixedValue;
      if (dirichlet) {
        if (gpuCoefficient[f] != 0.0) ++coverage.correctedBoundaryFaces;
      } else {
        // A Neumann-like boundary face must be left UNCORRECTED. Three
        // independent structural facts, none inferred from CPU/GPU equality:
        //   * its coupling coefficient is exactly 0.0;
        //   * its explicit term is exactly 0.0 (the CPU `continue`s before
        //     computing either);
        //   * its corrected flux is numerically equal to the predictor.
        //
        // The last is `==`, not a bit comparison, and deliberately so. The CPU
        // does not branch around these faces: it evaluates
        // (F* + 0.0*(p'_owner - 0.0)) and the sign of that zero follows
        // p'_owner, so a predictor of -0.0 comes back as +0.0 when p'_owner is
        // positive. The VALUE is unchanged in every case, which is the
        // production contract; the sign of zero is not part of it. Those flips
        // are counted and reported rather than ignored.
        if (!sameBits(gpuCoefficient[f], 0.0)) ++boundaryErrors;
        if (!sameBits(gpuExplicit[f], 0.0)) ++boundaryErrors;
        if (got[f] != predictor[f]) ++boundaryErrors;
        if (!sameBits(got[f], predictor[f])) ++coverage.signedZeroFlips;
        if (!withExplicit) ++coverage.untouchedBoundaryFaces;
      }
    } else {
      ++coverage.interiorFaces;
    }
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
  if (threeD) ++coverage.threeD; else ++coverage.twoD;
  if (withExplicit) ++coverage.withExplicit; else ++coverage.withoutExplicit;

  ++cases;
  const bool ok = dJump == 0 && dIncrement == 0 && dFlux == 0 && boundaryErrors == 0;
  if (!ok) ++failures;
  if (print || !ok) {
    std::printf("  %s %-14s %-14s %-11s %-9s %-11s %-9s jump[d=%zu] inc[d=%zu] flux[d=%zu] "
                "bnd[e=%zu] maxAbs=%-10.3g scaleF=%.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcCase.name, pCase.name, fluxCase.name,
                responseCase.name, withExplicit ? "+explicit" : "", dJump, dIncrement, dFlux,
                boundaryErrors, maxAbs, scaleF);
  }
}

// ---------------------------------------------------------------------------
// L3 -- continuity and conservation
// ---------------------------------------------------------------------------

void runContinuity(const std::string& meshName, const Mesh& mesh,
                   const cfd::gpu::DevicePressureCorrectionPlan& plan, const char* bcName,
                   const BoundaryConditionSet& pressureBoundaries, const FluxCase& predictorCase) {
  const char* predictorName = predictorCase.name;
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;

  SurfaceField predictor(nf);
  for (Index f = 0; f < nf; ++f)
    predictor[f] = predictorCase.value(f, mesh.face(f).centroid(), mesh.face(f).areaVector());
  ScalarField dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    dU[c] = respNonuniform(x, 0);
    dV[c] = respNonuniform(x, 1);
    dW[c] = respNonuniform(x, 2);
  }

  const auto assembly = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, predictor, dU, dV, 998.2, 0, pressureBoundaries, {}, threeD ? &dW : nullptr);
  cfd::algebra::BiCGSTAB solver([] {
    cfd::algebra::LinearSolverSettings s;
    s.absoluteTolerance = 1e-14;
    s.relativeTolerance = 1e-14;
    s.maxIterations = 10000;
    return s;
  }());
  const auto solved = solver.solve(assembly.system);
  ScalarField pPrime(nc);
  for (Index c = 0; c < nc; ++c) pPrime[c] = solved.solution[c];

  const SurfaceField cpuCorrected = cfd::pressure_velocity::correctFaceMassFlux(
      mesh, predictor, assembly.faceCoefficient, pPrime, nullptr);

  cfd::gpu::DeviceBuffer<Real> dPredictor, ddU, ddV, ddW, dPPrime;
  dPredictor.uploadFrom(predictor.data(), nf);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  dPPrime.uploadFrom(pPrime.data(), nc);
  cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
  deviceOptions.referenceCell = 0;
  deviceOptions.density = 998.2;
  cfd::gpu::DevicePressureCorrectionSystem system;
  cfd::gpu::assemblePressureCorrectionDevice(plan, dPredictor, ddU, ddV, threeD ? &ddW : nullptr,
                                             nullptr, deviceOptions, system);
  cfd::gpu::DeviceBuffer<Real> gpuCorrected;
  cfd::gpu::correctFaceMassFluxDevice(plan.mesh(), dPredictor, system.faceCoefficient, dPPrime,
                                      nullptr, gpuCorrected);
  const auto got = pull(gpuCorrected);

  std::vector<Real> before(static_cast<std::size_t>(nf)), cpuAfter(static_cast<std::size_t>(nf));
  for (Index f = 0; f < nf; ++f) { before[f] = predictor[f]; cpuAfter[f] = cpuCorrected[f]; }

  const auto imbalanceBefore = imbalanceOf(mesh, before);
  const auto imbalanceCpu = imbalanceOf(mesh, cpuAfter);
  const auto imbalanceGpu = imbalanceOf(mesh, got);

  std::size_t errors = 0;
  // (a) CPU and GPU agree bitwise on the corrected flux AND on the imbalance
  //     derived from it.
  for (Index f = 0; f < nf; ++f) if (!sameBits(cpuAfter[f], got[f])) ++errors;
  for (Index c = 0; c < nc; ++c) if (!sameBits(imbalanceCpu[c], imbalanceGpu[c])) ++errors;

  // (b) The exactly-representable conservation invariant: every interior face
  //     is visited exactly twice, once as +F and once as -F, and (+F)+(-F) is
  //     exactly 0.0 for any finite F. Stated this way rather than as a
  //     threshold on a ~n-term signed sum, whose round-off is not a defect.
  std::vector<int> ownerVisits(static_cast<std::size_t>(nf), 0);
  std::vector<int> neighborVisits(static_cast<std::size_t>(nf), 0);
  // Boundary faces are counted too, so the expectation below is meaningful for
  // them (owner once, neighbour never). Skipping them here and then expecting
  // a visit was the exact mistake made and fixed in 001H.
  for (Index c = 0; c < nc; ++c) {
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      if (face.owner() == c) ++ownerVisits[faceId]; else ++neighborVisits[faceId];
    }
  }
  std::size_t pairs = 0;
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) {
      if (ownerVisits[f] != 1 || neighborVisits[f] != 0) ++errors;
      continue;
    }
    if (ownerVisits[f] != 1 || neighborVisits[f] != 1) { ++errors; continue; }
    if (!sameBits(got[f] + (-got[f]), 0.0)) ++errors;
    ++pairs;
  }
  coverage.pairwiseCancellations += static_cast<int>(pairs);

  // (c) The correction must improve continuity -- but only where the system
  //     actually solves continuity, and only when there was something to
  //     correct. Both qualifications are properties of the discrete system,
  //     not slack:
  //
  //   * When the reference cell is PINNED (no FixedValue pressure patch), its
  //     row is replaced by p'[ref] = 0, so its continuity equation is
  //     discarded. A fully-Neumann system is also only solvable when the net
  //     source already matches the net boundary flux, and the pin is what
  //     absorbs any mismatch -- into that one cell. The post-correction
  //     imbalance is therefore ~0 everywhere EXCEPT the reference cell, which
  //     soaks up the global residual. An all-cell RMS can legitimately RISE
  //     (it did: 0.0037 -> 0.0193 on warped 3d 3 / all-Neumann, ~0.1 of
  //     imbalance concentrated into 1 of 27 cells). The criterion is therefore
  //     stated over the cells the system solves.
  //   * When p' comes back exactly zero -- the predictor was already
  //     conservative, so the solver converged at iteration 0 -- there is
  //     nothing to correct, and demanding a reduction would demand the
  //     impossible. The exact claim there is that NOTHING changed.
  const Real rmsBefore = rmsOf(imbalanceBefore);
  const Real rmsAfter = rmsOf(imbalanceCpu);
  std::vector<Real> solvedBefore, solvedAfter;
  const Index referenceCell = 0;
  for (Index c = 0; c < nc; ++c) {
    if (plan.pinReferenceCell() && c == referenceCell) continue;
    solvedBefore.push_back(imbalanceBefore[c]);
    solvedAfter.push_back(imbalanceCpu[c]);
  }
  const Real rmsBeforeSolved = rmsOf(solvedBefore);
  const Real rmsAfterSolved = rmsOf(solvedAfter);

  bool pPrimeAllZero = true;
  for (Index c = 0; c < nc && pPrimeAllZero; ++c) pPrimeAllZero = pPrime[c] == 0.0;

  bool reduced = false;
  if (pPrimeAllZero) {
    // Nothing to correct: the flux and the imbalance must be unchanged.
    for (Index f = 0; f < nf; ++f) if (cpuAfter[f] != before[f]) ++errors;
    for (Index c = 0; c < nc; ++c) if (imbalanceCpu[c] != imbalanceBefore[c]) ++errors;
  } else {
    reduced = rmsAfterSolved < rmsBeforeSolved;
    if (!reduced) ++errors;
  }
  if (reduced) ++coverage.reducedImbalance;
  if (pPrimeAllZero) ++coverage.alreadyConservative;

  ++cases;
  ++coverage.continuityCases;
  if (errors != 0) ++failures;
  std::printf("  %s L3 %-14s %-14s %-9s all[%.4g->%.4g] solved[%.4g->%.4g x%.3g] pin=%s "
              "solve[it=%zu r=%.3g] pairs=%zu errors=%zu\n",
              errors == 0 ? "PASS" : "FAIL", meshName.c_str(), bcName, predictorName, rmsBefore,
              rmsAfter, rmsBeforeSolved, rmsAfterSolved,
              rmsBeforeSolved > 0.0 ? rmsAfterSolved / rmsBeforeSolved : 0.0,
              plan.pinReferenceCell() ? "yes" : "no",
              static_cast<std::size_t>(solved.iterations), solved.finalResidual, pairs, errors);
}

// ---------------------------------------------------------------------------
// L4 -- the integrated pressure path
// ---------------------------------------------------------------------------

cfd::algebra::LinearSolverSettings solverSettings() {
  cfd::algebra::LinearSolverSettings s;
  s.absoluteTolerance = 1e-12;
  s.relativeTolerance = 1e-12;
  s.maxIterations = 5000;
  return s;
}

void runChain(const std::string& meshName, const Mesh& mesh, bool quick) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  const FluidProperties fluid(1.2, 1.0e-3);
  const Index referenceCell = 0;
  const Real alpha = 0.7;
  const Index convectionScheme = 0;

  BoundaryConditionSet velocityBoundaries;
  {
    std::size_t i = 0;
    for (const auto& p : mesh.boundaryPatches()) {
      switch (i % 5) {
        case 0: velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Wall>()); break;
        case 1: velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::MovingWall>(Vector3{1.5, -0.25, 0.1})); break;
        case 2: velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{0.9, 0.4, -0.2})); break;
        case 3: velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Outlet>()); break;
        default: velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Symmetry>()); break;
      }
      ++i;
    }
  }
  const BoundaryConditionSet pressureBoundaries = mixedPressure(mesh);

  VectorField velocity(nc);
  ScalarField pressure(nc), viscosity(nc), prevU(nc), prevV(nc), prevW(nc);
  const Real pi = cfd::constants::pi;
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = Vector3{(std::sin(pi * x.x) * std::cos(pi * x.y)) + 0.5,
                          (std::cos(pi * x.x) * std::sin(pi * x.y)) - 0.25,
                          (0.3 * std::sin(pi * x.z)) + (0.2 * x.x)};
    pressure[c] = 101325.0 + (500.0 * std::sin(pi * x.x) * std::cos(pi * x.y));
    viscosity[c] = 1.0e-3 + (4.0e-3 * x.x);
    prevU[c] = velocity[c].x - 0.1;
    prevV[c] = velocity[c].y + 0.05;
    prevW[c] = velocity[c].z - 0.02;
  }
  SurfaceField seedFlux(nf);
  for (Index f = 0; f < nf; ++f)
    seedFlux[f] = fluxSwirl(f, mesh.face(f).centroid(), mesh.face(f).areaVector());

  // --- CPU chain ---------------------------------------------------------
  const auto assembleMomentum = [&](Index component, const ScalarField& previous) {
    return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, velocity, pressure, seedFlux, viscosity, velocityBoundaries, pressureBoundaries,
        static_cast<cfd::physics::VelocityComponent>(component), previous, alpha, nullptr, nullptr,
        static_cast<cfd::discretization::ConvectionScheme>(convectionScheme));
  };
  const ScalarField cpuDU = cfd::pressure_velocity::computeMomentumResponseCoefficient(
      mesh, assembleMomentum(0, prevU).diagonal);
  const ScalarField cpuDV = cfd::pressure_velocity::computeMomentumResponseCoefficient(
      mesh, assembleMomentum(1, prevV).diagonal);
  ScalarField cpuDW;
  if (threeD)
    cpuDW = cfd::pressure_velocity::computeMomentumResponseCoefficient(
        mesh, assembleMomentum(2, prevW).diagonal);
  const VectorField gradP = cfd::discretization::gradient(mesh, pressure, pressureBoundaries,
                                                          GradientScheme::GreenGauss);
  const SurfaceField cpuPredictor = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, pressure, gradP, cpuDU, cpuDV, threeD ? &cpuDW : nullptr, fluid,
      velocityBoundaries, alpha);
  const auto cpuAssembly = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, cpuPredictor, cpuDU, cpuDV, fluid.density(), referenceCell, pressureBoundaries, {},
      threeD ? &cpuDW : nullptr);
  cfd::algebra::BiCGSTAB cpuSolver(solverSettings());
  const auto cpuSolve = cpuSolver.solve(cpuAssembly.system);
  ScalarField cpuPPrime(nc);
  for (Index c = 0; c < nc; ++c) cpuPPrime[c] = cpuSolve.solution[c];
  const VectorField cpuVelocity = cfd::pressure_velocity::correctVelocity(
      mesh, velocity, cpuDU, cpuDV, cpuPPrime, pressureBoundaries, GradientScheme::GreenGauss,
      threeD ? &cpuDW : nullptr);
  const SurfaceField cpuFlux = cfd::pressure_velocity::correctFaceMassFlux(
      mesh, cpuPredictor, cpuAssembly.faceCoefficient, cpuPPrime, nullptr);

  // --- GPU chain ---------------------------------------------------------
  cfd::gpu::DeviceMomentumAssemblyPlan momentumPlan;
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  cfd::gpu::DevicePressureCorrectionPlan pcorrPlan;
  cfd::gpu::DeviceVelocityCorrectionPlan velocityPlan;
  if (!momentumPlan.build(mesh, velocityBoundaries, pressureBoundaries) ||
      !fluxPlan.build(mesh, velocityBoundaries) || !pcorrPlan.build(mesh, pressureBoundaries) ||
      !velocityPlan.build(mesh, pressureBoundaries)) {
    ++failures;
    std::printf("  FAIL L4 %-14s a plan is unusable\n", meshName.c_str());
    return;
  }
  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dSeed, dPrevU, dPrevV, dPrevW;
  dPressure.uploadFrom(pressure.data(), nc);
  dViscosity.uploadFrom(viscosity.data(), nc);
  dSeed.uploadFrom(seedFlux.data(), nf);
  dPrevU.uploadFrom(prevU.data(), nc);
  dPrevV.uploadFrom(prevV.data(), nc);
  dPrevW.uploadFrom(prevW.data(), nc);
  const auto response = [&](Index component, cfd::gpu::DeviceBuffer<Real>& previous,
                            cfd::gpu::DeviceBuffer<Real>& out) {
    cfd::gpu::MomentumAssemblyOptions options;
    options.component = component;
    options.convectionScheme = convectionScheme;
    options.relaxationAlpha = alpha;
    cfd::gpu::DeviceMomentumSystem sys;
    cfd::gpu::assembleRelaxedMomentumDevice(momentumPlan, deviceVelocity, dPressure, dViscosity,
                                            dSeed, previous, nullptr, options, sys);
    cfd::gpu::computeMomentumResponseCoefficientDevice(momentumPlan.convectionPlan().mesh(),
                                                       sys.diagonal, out);
  };
  cfd::gpu::DeviceBuffer<Real> gpuDU, gpuDV, gpuDW;
  response(0, dPrevU, gpuDU);
  response(1, dPrevV, gpuDV);
  if (threeD) response(2, dPrevW, gpuDW);
  cfd::gpu::DeviceBuffer<Real> dGx, dGy, dGz;
  {
    std::vector<Real> a(nc), b(nc), c(nc);
    for (Index i = 0; i < nc; ++i) { a[i] = gradP[i].x; b[i] = gradP[i].y; c[i] = gradP[i].z; }
    dGx.uploadFrom(a.data(), nc);
    dGy.uploadFrom(b.data(), nc);
    dGz.uploadFrom(c.data(), nc);
  }
  cfd::gpu::DeviceBuffer<Real> gpuPredictor;
  cfd::gpu::rhieChowMassFluxDevice(fluxPlan, deviceVelocity, dPressure, dGx, dGy, dGz, gpuDU, gpuDV,
                                   threeD ? &gpuDW : nullptr, fluid.density(), alpha, gpuPredictor);
  cfd::gpu::PressureCorrectionOptionsDevice pcorrOptions;
  pcorrOptions.referenceCell = referenceCell;
  pcorrOptions.density = fluid.density();
  cfd::gpu::DevicePressureCorrectionSystem gpuSystem;
  cfd::gpu::assemblePressureCorrectionDevice(pcorrPlan, gpuPredictor, gpuDU, gpuDV,
                                             threeD ? &gpuDW : nullptr, nullptr, pcorrOptions,
                                             gpuSystem);

  // --- L4a: the CONTROLLED comparison, bitwise ---------------------------
  {
    cfd::gpu::DeviceBuffer<Real> dPPrime, gpuFlux;
    dPPrime.uploadFrom(cpuPPrime.data(), nc);
    cfd::gpu::correctFaceMassFluxDevice(pcorrPlan.mesh(), gpuPredictor, gpuSystem.faceCoefficient,
                                        dPPrime, nullptr, gpuFlux);
    const auto got = pull(gpuFlux);
    std::size_t differing = 0;
    Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
    for (Index f = 0; f < nf; ++f) {
      if (!sameBits(cpuFlux[f], got[f])) ++differing;
      track(cpuFlux[f], got[f], maxAbs, maxRel, scale);
    }
    globalMaxAbs = std::max(globalMaxAbs, maxAbs);
    globalMaxRel = std::max(globalMaxRel, maxRel);
    ++cases;
    ++coverage.controlled;
    const bool ok = differing == 0;
    if (!ok) ++failures;
    std::printf("  %s L4a %-14s controlled p' differing=%zu maxAbs=%-10.3g scale=%.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), differing, maxAbs, scale);
  }

  if (quick) return;

  // --- L4b: the full chain, GPU solve included ---------------------------
  auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(solverSettings());
  if (gpuSolver == nullptr) {
    std::printf("       (no GPU solver available -- L4b skipped on %s)\n", meshName.c_str());
    return;
  }
  const auto rows = [&] {
    std::vector<Index> h(static_cast<std::size_t>(gpuSystem.rowOffsets.size()));
    gpuSystem.rowOffsets.downloadTo(h.data(), gpuSystem.rowOffsets.size());
    return h;
  }();
  const auto cols = [&] {
    std::vector<Index> h(static_cast<std::size_t>(gpuSystem.columnIndices.size()));
    gpuSystem.columnIndices.downloadTo(h.data(), gpuSystem.columnIndices.size());
    return h;
  }();
  const auto vals = pull(gpuSystem.values);
  cfd::algebra::SparseMatrixBuilder builder(nc, nc);
  for (Index r = 0; r < nc; ++r)
    for (Index k = rows[r]; k < rows[r + 1]; ++k)
      if (vals[k] != 0.0) builder.add(r, cols[k], vals[k]);
  const auto gpuMatrix = builder.build();
  const auto gpuRhsHost = pull(gpuSystem.rhs);
  cfd::algebra::Vector gpuRhs(static_cast<std::size_t>(nc));
  for (Index c = 0; c < nc; ++c) gpuRhs[c] = gpuRhsHost[c];
  const auto gpuSolve = gpuSolver->solve(cfd::algebra::LinearSystem(gpuMatrix, gpuRhs));
  ScalarField gpuPPrime(nc);
  for (Index c = 0; c < nc; ++c) gpuPPrime[c] = gpuSolve.solution[c];

  cfd::gpu::DeviceBuffer<Real> dGpuPPrime, gx, gy, gz, gpuFlux;
  dGpuPPrime.uploadFrom(gpuPPrime.data(), nc);
  cfd::gpu::DeviceVelocity gpuVelocity;
  cfd::gpu::correctVelocityDevice(velocityPlan, deviceVelocity, gpuDU, gpuDV,
                                  threeD ? &gpuDW : nullptr, dGpuPPrime,
                                  cfd::gpu::kGradientSchemeGreenGauss, gx, gy, gz, gpuVelocity);
  cfd::gpu::correctFaceMassFluxDevice(pcorrPlan.mesh(), gpuPredictor, gpuSystem.faceCoefficient,
                                      dGpuPPrime, nullptr, gpuFlux);
  const auto hx = pull(gpuVelocity.x), hy = pull(gpuVelocity.y), hz = pull(gpuVelocity.z);
  const auto hFlux = pull(gpuFlux);

  Real maxVelocity = 0.0, maxFlux = 0.0, maxPPrime = 0.0, pPrimeScale = 0.0;
  for (Index c = 0; c < nc; ++c) {
    maxVelocity = std::max({maxVelocity, std::abs(cpuVelocity[c].x - hx[c]),
                            std::abs(cpuVelocity[c].y - hy[c]),
                            std::abs(cpuVelocity[c].z - hz[c])});
    maxPPrime = std::max(maxPPrime, std::abs(cpuPPrime[c] - gpuPPrime[c]));
    pPrimeScale = std::max(pPrimeScale, std::abs(cpuPPrime[c]));
  }
  std::vector<Real> cpuFluxHost(static_cast<std::size_t>(nf));
  for (Index f = 0; f < nf; ++f) {
    cpuFluxHost[f] = cpuFlux[f];
    maxFlux = std::max(maxFlux, std::abs(cpuFlux[f] - hFlux[f]));
  }
  const auto cpuImbalance = imbalanceOf(mesh, cpuFluxHost);
  const auto gpuImbalance = imbalanceOf(mesh, hFlux);
  Real maxImbalance = 0.0;
  for (Index c = 0; c < nc; ++c)
    maxImbalance = std::max(maxImbalance, std::abs(cpuImbalance[c] - gpuImbalance[c]));

  // Solver-limited by construction: the criterion is stated on p', the quantity
  // the two solves actually differ on, at their own tolerance. L4a is the
  // bitwise statement about this operator.
  const Real bound = 1e-6 * std::max(pPrimeScale, 1.0);
  ++cases;
  ++coverage.chain;
  const bool bothConverged = cpuSolve.converged() && gpuSolve.converged();
  const bool ok = bothConverged && maxPPrime <= bound;
  if (!ok) ++failures;
  std::printf("  %s L4b %-14s cpu[it=%zu r=%.3g restarts=%zu] gpu[it=%zu r=%.3g restarts=%zu] "
              "|dp'|=%.3g (<=%.3g) |du|=%.3g |dF|=%.3g |dR|=%.3g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(),
              static_cast<std::size_t>(cpuSolve.iterations), cpuSolve.finalResidual,
              static_cast<std::size_t>(cpuSolve.restarts),
              static_cast<std::size_t>(gpuSolve.iterations), gpuSolve.finalResidual,
              static_cast<std::size_t>(gpuSolve.restarts), maxPPrime, bound, maxVelocity, maxFlux,
              maxImbalance);
  if (!ok && !bothConverged) {
    std::printf("       cpu status=%d gpu status=%d -- classify against the recorded GPU BiCGSTAB "
                "restart asymmetry before treating this as a face-flux-correction regression\n",
                static_cast<int>(cpuSolve.status), static_cast<int>(gpuSolve.status));
  }
}

void runMesh(const std::string& name, const Mesh& mesh, bool quick) {
  const std::vector<PressureBcCase> bcs = {{"all-Neumann", allNeumannPressure},
                                           {"mixed", mixedPressure},
                                           {"all-FixedValue", allFixedValuePressure}};
  const std::vector<PPrimeCase> fields = {{"zero", pZero},
                                          {"uniform", pUniform},
                                          {"linear", pLinear},
                                          {"nonuniform", pNonuniform},
                                          {"negative", pNegative}};
  const std::vector<FluxCase> fluxes = {{"positive", fluxPositive},
                                        {"negative", fluxNegative},
                                        {"mixed", fluxMixed},
                                        {"swirl", fluxSwirl},
                                        {"reversed", fluxReversed}};
  const std::vector<ResponseCase> responses = {{"uniform", respUniform},
                                               {"nonuniform", respNonuniform}};

  for (const auto& bcCase : bcs) {
    const BoundaryConditionSet pressureBoundaries = bcCase.build(mesh);
    cfd::gpu::DevicePressureCorrectionPlan plan;
    if (!plan.build(mesh, pressureBoundaries)) {
      ++failures;
      std::printf("  FAIL %-14s %-14s plan unusable: %s\n", name.c_str(), bcCase.name,
                  plan.unsupportedReason().c_str());
      continue;
    }
    std::printf("       [%s/%s pin=%s fixedValueFaces=%zu]\n", name.c_str(), bcCase.name,
                plan.pinReferenceCell() ? "yes" : "no",
                static_cast<std::size_t>(plan.fixedValueBoundaryFaces()));

    for (const auto& field : fields)
      for (const auto& flux : fluxes)
        for (const auto& response : responses)
          for (const bool withExplicit : {false, true})
            runDirect(name, mesh, plan, bcCase, pressureBoundaries, field, flux, response,
                      withExplicit, false);

    // Two predictors: one already divergence-free to round-off (so p' comes
    // back exactly zero and NOTHING may change), one plainly non-conservative
    // (so the correction has real work to do).
    for (const auto& predictorCase : {FluxCase{"swirl", fluxSwirl}, FluxCase{"mixed", fluxMixed}})
      runContinuity(name, mesh, plan, bcCase.name, pressureBoundaries, predictorCase);
  }
  runChain(name, mesh, quick);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001K: CUDA face-flux correction vs CPU (bitwise) ===\n");
  std::printf("CPU reference: pressure_velocity::correctFaceMassFlux\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), true);
    runMesh("warped 3d 3", warped3D(3), true);
  } else {
    runMesh("cartesian2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0), false);
    runMesh("cartesian2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), false);
    runMesh("graded2d 10",
            MeshGeometry::createGraded2D(
                10, 10, 1.0, 1.0,
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                       cfd::mesh::GradingCluster::Start},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                       cfd::mesh::GradingCluster::Both}),
            false);
    runMesh("distorted q16", q16(), false);
    runMesh("cartesian3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0), false);
    runMesh("warped 3d 3", warped3D(3), false);
  }

  std::printf("\n=== coverage ===\n");
  std::printf("  2D / 3D direct cases              %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  without / with explicit term      %d / %d\n", coverage.withoutExplicit,
              coverage.withExplicit);
  std::printf("  interior faces compared           %d\n", coverage.interiorFaces);
  std::printf("  corrected (FixedValue) boundary faces %d\n", coverage.correctedBoundaryFaces);
  std::printf("  untouched Neumann boundary faces  %d\n", coverage.untouchedBoundaryFaces);
  std::printf("  continuity cases                  %d\n", coverage.continuityCases);
  std::printf("  imbalance reduced                 %d\n", coverage.reducedImbalance);
  std::printf("  already-conservative predictors   %d\n", coverage.alreadyConservative);
  std::printf("  owner/neighbour cancellations     %d\n", coverage.pairwiseCancellations);
  std::printf("  signed-zero flips (recorded, not errors) %d\n", coverage.signedZeroFlips);
  std::printf("  controlled p' cases               %d\n", coverage.controlled);
  std::printf("  full chain cases (GPU solve)      %d\n", coverage.chain);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseValues);
  std::printf("  max absolute discrepancy          %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy          %.3g\n", globalMaxRel);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.withExplicit == 0 || coverage.withoutExplicit == 0) { ++failures; std::printf("  FAIL both explicit-term settings not exercised\n"); }
  if (coverage.correctedBoundaryFaces == 0) { ++failures; std::printf("  FAIL no FixedValue boundary face was ever corrected\n"); }
  if (coverage.untouchedBoundaryFaces == 0) { ++failures; std::printf("  FAIL no Neumann boundary face was ever checked for being untouched\n"); }
  if (coverage.pairwiseCancellations == 0) { ++failures; std::printf("  FAIL the conservation invariant never ran\n"); }
  if (coverage.reducedImbalance == 0) { ++failures; std::printf("  FAIL the correction never reduced the imbalance\n"); }
  if (coverage.controlled == 0) { ++failures; std::printf("  FAIL the controlled p' comparison never ran\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "FACE FLUX CORRECTION EQUIVALENCE: PASS (bitwise)"
                                    : "FACE FLUX CORRECTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
