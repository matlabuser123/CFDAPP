// GPU-DISC-001I gate -- CUDA pressure-correction assembly vs the CPU.
//
// CPU reference: cfd::pressure_velocity::assemblePressureCorrection
// (-> assembleGeometricPressureCorrection). Assembly only; nothing is solved.
//
// Layers:
//   L1 direct      the assembled system against the CPU, over meshes, fluxes,
//                  response fields, densities, reference cells, pressure BC
//                  sets and both settings of the non-orthogonal flag.
//   L2 chain       CPU momentum -> response -> face flux -> pressure correction
//                  against the same chain on CUDA.
//   L3 continuity  the RHS convention verified against an INDEPENDENTLY
//                  computed per-cell mass imbalance -- not inferred from
//                  CPU/GPU equality.
//   L4 reference   the conditional pin: owner==ref, neighbour==ref, neither,
//                  and a FixedValue patch suppressing pinning entirely.
//
// Diagonal, off-diagonals and RHS are compared INDEPENDENTLY so a sign reversal
// localises to one link of the chain.
//
// usage: pressure_correction_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PressureCorrectionOptions;

namespace {

int failures = 0;
int cases = 0;
std::size_t coefficientsCompared = 0;
std::size_t bitwiseCoefficients = 0;
Real globalMaxAbs = 0.0;
Real globalMaxRel = 0.0;

struct Coverage {
  int twoD = 0, threeD = 0;
  int pinned = 0, unpinned = 0;
  int ownerIsRef = 0, neighbourIsRef = 0, neitherIsRef = 0;
  int nonOrthOn = 0, nonOrthOff = 0;
  int axisAlignedPlans = 0, generalPlans = 0;
  int wellPosedFallbacks = 0;
  int continuityChecks = 0;
  int coupledBoundaryFaces = 0;
  int explicitTermFaces = 0;
  int bandHits = 0;
  int integrated = 0;
  int patternMismatch = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

std::vector<Real> pullR(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}
std::vector<Index> pullI(const cfd::gpu::DeviceBuffer<Index>& b) {
  std::vector<Index> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}

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

// --- fluxes --------------------------------------------------------------
struct FluxCase { const char* name; Real (*value)(Index, const Vector3&, const Vector3&); };
Real fluxZero(Index, const Vector3&, const Vector3&) { return 0.0; }
Real fluxPositive(Index, const Vector3&, const Vector3&) { return 0.7; }
Real fluxNegative(Index, const Vector3&, const Vector3&) { return -0.7; }
Real fluxMixed(Index id, const Vector3&, const Vector3&) {
  if (id % 5 == 0) return 0.0;
  if (id % 5 == 1) return -0.0;
  return (id % 3 == 0) ? 0.45 : ((id % 3 == 1) ? -0.8 : 1.3);
}
Real fluxSwirl(Index, const Vector3& c, const Vector3& a) {
  const Vector3 u{-(c.y - 0.5), c.x - 0.5, 0.1 * c.z};
  return dot(u, a);
}

// --- response coefficients ------------------------------------------------
struct ResponseCase { const char* name; Real (*du)(const Vector3&); Real (*dv)(const Vector3&); };
Real respUniform(const Vector3&) { return 0.02; }
Real respVarying(const Vector3& x) { return 0.005 + (0.03 * x.x) + (0.01 * x.y * x.y); }
// Extreme anisotropy: the response VECTOR (du*Sx, dv*Sy, dw*Sz) is then rotated
// far from Sf, which is how dot(d, responseVector) can approach and cross the
// 1e-6*|d|*|sf| well-posedness threshold inside decomposeAreaVector.
Real respAnisoU(const Vector3&) { return 1.0e2; }
Real respAnisoV(const Vector3&) { return 1.0e-10; }

BoundaryConditionSet allNeumannPressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches())
    s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return s;
}
BoundaryConditionSet oneFixedValuePressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    if (i == 0) s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    else s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
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
struct PressureBcCase { const char* name; BoundaryConditionSet (*build)(const Mesh&); bool expectPin; };

// The CPU's two exact geometric predicates, restated verbatim
// (PressureCorrectionEquation.cpp:60, :65). A face satisfying BOTH takes the
// axis-aligned short-circuit and NEVER reaches decomposeAreaVector, so its
// well-posedness guard cannot be exercised there -- a property of the mesh,
// not a coverage shortfall.
bool isAxisAligned(const Vector3& sf) noexcept {
  const int nonZero = (sf.x != 0.0 ? 1 : 0) + (sf.y != 0.0 ? 1 : 0) + (sf.z != 0.0 ? 1 : 0);
  return nonZero == 1;
}
bool exactlyParallel(const Vector3& a, const Vector3& b) noexcept {
  return cross(a, b) == Vector3{};
}
// Internal faces that actually reach the non-orthogonal decomposition.
Index decomposingFaceCount(const Mesh& mesh) {
  Index n = 0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Vector3 sf = face.areaVector();
    const Vector3 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
    if (!(isAxisAligned(sf) && exactlyParallel(d, sf))) ++n;
  }
  return n;
}

// ---------------------------------------------------------------------------
// L1 / L3 / L4 -- direct
// ---------------------------------------------------------------------------

void runDirect(const std::string& meshName, const Mesh& mesh,
               const cfd::gpu::DevicePressureCorrectionPlan& plan, const PressureBcCase& bcCase,
               const BoundaryConditionSet& pressureBoundaries, const FluxCase& fluxCase,
               const ResponseCase& responseCase, Real density, Index referenceCell,
               bool nonOrthogonal, bool withPrevious, bool print) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;

  SurfaceField flux(nf);
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    flux[f] = fluxCase.value(f, face.centroid(), face.areaVector());
  }
  ScalarField dU(nc), dV(nc), dW(nc), previous(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    dU[c] = responseCase.du(x);
    dV[c] = responseCase.dv(x);
    dW[c] = responseCase.du(x) * 0.87;
    previous[c] = 3.5 * std::sin(2.0 * x.x) - (1.25 * x.y) + (0.4 * x.z);
  }

  PressureCorrectionOptions options;
  options.nonOrthogonal = nonOrthogonal;
  options.previousPressureCorrection = withPrevious ? &previous : nullptr;

  const auto cpu = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, flux, dU, dV, density, referenceCell, pressureBoundaries, options,
      threeD ? &dW : nullptr);
  const auto& cpuMatrix = cpu.system.matrix();
  const auto& cpuRhs = cpu.system.rhs();

  cfd::gpu::DeviceBuffer<Real> dFlux, ddU, ddV, ddW, dPrev;
  dFlux.uploadFrom(flux.data(), nf);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  dPrev.uploadFrom(previous.data(), nc);
  cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
  deviceOptions.nonOrthogonal = nonOrthogonal;
  deviceOptions.referenceCell = referenceCell;
  deviceOptions.density = density;
  cfd::gpu::DevicePressureCorrectionSystem system;
  cfd::gpu::assemblePressureCorrectionDevice(plan, dFlux, ddU, ddV, threeD ? &ddW : nullptr,
                                             withPrevious ? &dPrev : nullptr, deviceOptions,
                                             system);

  const auto gRow = pullI(system.rowOffsets);
  const auto gCol = pullI(system.columnIndices);
  const auto gVal = pullR(system.values);
  const auto gRhs = pullR(system.rhs);
  const auto gCoefficient = pullR(system.faceCoefficient);
  const auto gExplicit = pullR(system.explicitFaceFlux);

  // --- diagonal, off-diagonal and RHS compared INDEPENDENTLY --------------
  std::size_t dDiag = 0, dOff = 0, dRhs = 0, missing = 0, extraNonZero = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scaleD = 0.0, scaleO = 0.0, scaleR = 0.0;
  const auto track = [&](Real want, Real got, Real& scale) {
    ++coefficientsCompared;
    if (sameBits(want, got)) ++bitwiseCoefficients;
    const Real diff = std::abs(want - got);
    maxAbs = std::max(maxAbs, diff);
    scale = std::max(scale, std::abs(want));
    if (std::abs(want) > 0.0) maxRel = std::max(maxRel, diff / std::abs(want));
  };
  for (Index r = 0; r < nc; ++r) {
    Index g = gRow[r];
    for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
      const Index column = cpuMatrix.columnIndicesData()[k];
      while (g < gRow[r + 1] && gCol[g] < column) { if (gVal[g] != 0.0) ++extraNonZero; ++g; }
      if (g >= gRow[r + 1] || gCol[g] != column) { ++missing; continue; }
      const Real want = cpuMatrix.valuesData()[k];
      const Real got = gVal[g];
      if (column == r) { if (!sameBits(want, got)) ++dDiag; track(want, got, scaleD); }
      else { if (!sameBits(want, got)) ++dOff; track(want, got, scaleO); }
      ++g;
    }
    for (; g < gRow[r + 1]; ++g) if (gVal[g] != 0.0) ++extraNonZero;
  }
  for (Index r = 0; r < nc; ++r) {
    if (!sameBits(cpuRhs[r], gRhs[r])) ++dRhs;
    track(cpuRhs[r], gRhs[r], scaleR);
  }
  // The two per-face SurfaceFields the assembly also produces. They are not a
  // by-product: correctFaceMassFlux must reuse the EXACT coefficients the
  // matrix was built with, so they are part of what this gate qualifies -- and
  // they are the only place a wrongly-coupled Neumann boundary face or a
  // wrong explicit term would show up before the face-flux correction exists.
  std::size_t dCoefficient = 0, dExplicit = 0;
  Real scaleF = 0.0;
  for (Index f = 0; f < nf; ++f) {
    if (!sameBits(cpu.faceCoefficient[f], gCoefficient[f])) ++dCoefficient;
    track(cpu.faceCoefficient[f], gCoefficient[f], scaleF);
    if (!sameBits(cpu.explicitFaceFlux[f], gExplicit[f])) ++dExplicit;
    track(cpu.explicitFaceFlux[f], gExplicit[f], scaleF);
    if (mesh.face(f).isBoundary() && cpu.faceCoefficient[f] != 0.0) ++coverage.coupledBoundaryFaces;
    if (cpu.explicitFaceFlux[f] != 0.0) ++coverage.explicitTermFaces;
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
  if (missing != 0 || extraNonZero != 0) ++coverage.patternMismatch;

  // --- L3: the RHS against an INDEPENDENT continuity imbalance ------------
  // Computed here from the flux directly, not from evaluateContinuity, so the
  // sign convention is verified rather than assumed.
  std::size_t rhsConventionErrors = 0;
  for (Index c = 0; c < nc; ++c) {
    if (plan.pinReferenceCell() && c == referenceCell) continue;  // pinned row: rhs is 0 by fiat
    if (withPrevious && nonOrthogonal) continue;  // explicit term also contributes
    Real imbalance = 0.0;
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      imbalance += (face.owner() == c) ? flux[faceId] : -flux[faceId];
    }
    if (!sameBits(-imbalance, gRhs[c])) ++rhsConventionErrors;
  }
  ++coverage.continuityChecks;

  // --- L4: reference-pin semantics ---------------------------------------
  std::size_t pinErrors = 0;
  if (plan.pinReferenceCell() != bcCase.expectPin) ++pinErrors;
  if (plan.pinReferenceCell()) {
    ++coverage.pinned;
    // The reference row must be exactly the identity equation.
    if (!sameBits(gRhs[referenceCell], 0.0)) ++pinErrors;
    bool sawDiagonal = false;
    for (Index k = gRow[referenceCell]; k < gRow[referenceCell + 1]; ++k) {
      if (gCol[k] == referenceCell) {
        sawDiagonal = true;
        if (!sameBits(gVal[k], 1.0)) ++pinErrors;  // EXACTLY 1.0, not 1.0 + accumulated
      } else if (gVal[k] != 0.0) {
        ++pinErrors;
      }
    }
    if (!sawDiagonal) ++pinErrors;
  } else {
    ++coverage.unpinned;
  }

  if (nonOrthogonal) ++coverage.nonOrthOn; else ++coverage.nonOrthOff;
  if (threeD) ++coverage.threeD; else ++coverage.twoD;

  ++cases;
  const bool ok = dDiag == 0 && dOff == 0 && dRhs == 0 && missing == 0 && extraNonZero == 0 &&
                  rhsConventionErrors == 0 && pinErrors == 0 && dCoefficient == 0 &&
                  dExplicit == 0;
  if (!ok) ++failures;
  if (print || !ok) {
    std::printf("  %s %-14s %-12s %-9s %-10s rho=%-7.4g ref=%-5zu %-7s%s diag[d=%zu] off[d=%zu] "
                "rhs[d=%zu] coef[d=%zu] expl[d=%zu] conv[e=%zu] pin[e=%zu] maxAbs=%-10.3g "
                "scaleD=%-10.4g scaleR=%-10.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcCase.name, fluxCase.name,
                responseCase.name, density, static_cast<std::size_t>(referenceCell),
                nonOrthogonal ? "nonorth" : "orth", withPrevious ? "+prev" : "     ", dDiag, dOff,
                dRhs, dCoefficient, dExplicit, rhsConventionErrors, pinErrors, maxAbs, scaleD,
                scaleR);
  }
}

// ---------------------------------------------------------------------------
// L5 -- structural checks that do NOT go through CPU/GPU equality
//
// Three things the brief asks for explicitly and that equality alone cannot
// establish:
//   (a) the per-face reference-pin topology -- owner == ref, neighbour == ref
//       and neither == ref all occur, counted per FACE;
//   (b) when one endpoint of a face is pinned, the OPPOSITE row still receives
//       its pair (+D on its diagonal, -D in its column for the reference) --
//       "suppress the reference row", not "skip the face";
//   (c) the M-matrix sign pattern D >= 0, diagonal >= 0, off-diagonal <= 0,
//       which is the assembled consequence of the sign chain in the audit.
// ---------------------------------------------------------------------------

void runStructural(const std::string& meshName, const Mesh& mesh,
                   const cfd::gpu::DevicePressureCorrectionPlan& plan, const char* bcName,
                   Index referenceCell) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;

  SurfaceField flux(nf);
  for (Index f = 0; f < nf; ++f)
    flux[f] = fluxSwirl(f, mesh.face(f).centroid(), mesh.face(f).areaVector());
  ScalarField dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    dU[c] = respVarying(x);
    dV[c] = respVarying(x) * 0.6;
    dW[c] = respVarying(x) * 1.3;
  }

  cfd::gpu::DeviceBuffer<Real> dFlux, ddU, ddV, ddW;
  dFlux.uploadFrom(flux.data(), nf);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  cfd::gpu::PressureCorrectionOptionsDevice options;
  options.nonOrthogonal = true;
  options.referenceCell = referenceCell;
  options.density = 998.2;
  cfd::gpu::DevicePressureCorrectionSystem system;
  cfd::gpu::assemblePressureCorrectionDevice(plan, dFlux, ddU, ddV, threeD ? &ddW : nullptr,
                                             nullptr, options, system);
  const auto gRow = pullI(system.rowOffsets);
  const auto gCol = pullI(system.columnIndices);
  const auto gVal = pullR(system.values);
  const auto gD = pullR(system.faceCoefficient);

  const auto entry = [&](Index row, Index column, Real& out) {
    for (Index k = gRow[row]; k < gRow[row + 1]; ++k)
      if (gCol[k] == column) { out = gVal[k]; return true; }
    return false;
  };

  // (a) + (b)
  std::size_t oppositeRowErrors = 0, sharedFaceAmbiguous = 0;
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Index owner = face.owner();
    const Index neighbour = *face.neighbor();
    const bool ownerPinned = plan.pinReferenceCell() && owner == referenceCell;
    const bool neighbourPinned = plan.pinReferenceCell() && neighbour == referenceCell;
    if (ownerPinned) ++coverage.ownerIsRef;
    else if (neighbourPinned) ++coverage.neighbourIsRef;
    else ++coverage.neitherIsRef;
    if (!ownerPinned && !neighbourPinned) continue;

    // The opposite row must still carry -D in the reference's column. That
    // column accumulates one -D per face shared by the two cells, so this is
    // an exact check only when they share exactly one face.
    const Index other = ownerPinned ? neighbour : owner;
    Index shared = 0;
    for (const Index g : mesh.cell(other).faceIds()) {
      const auto& gf = mesh.face(g);
      if (gf.isBoundary()) continue;
      if (gf.owner() == referenceCell || *gf.neighbor() == referenceCell) ++shared;
    }
    if (shared != 1) { ++sharedFaceAmbiguous; continue; }
    Real offDiagonal = 0.0;
    if (!entry(other, referenceCell, offDiagonal)) { ++oppositeRowErrors; continue; }
    if (!sameBits(offDiagonal, -gD[f])) ++oppositeRowErrors;
  }

  // (c) M-matrix sign pattern, on the non-reference rows.
  std::size_t signErrors = 0;
  for (Index f = 0; f < nf; ++f)
    if (!(gD[f] >= 0.0)) ++signErrors;
  for (Index r = 0; r < nc; ++r) {
    if (plan.pinReferenceCell() && r == referenceCell) continue;
    for (Index k = gRow[r]; k < gRow[r + 1]; ++k) {
      if (gCol[k] == r) { if (!(gVal[k] >= 0.0)) ++signErrors; }
      else if (!(gVal[k] <= 0.0)) ++signErrors;
    }
  }

  // (d) Independent census of the well-posedness branch inside
  // decomposeAreaVector -- computed here from the public CPU helpers, so it
  // reports which side of the 1e-6*|d|*|sf| guard each face actually fell on.
  std::size_t wellPosed = 0, fallback = 0, parallel = 0, shortCircuit = 0;
  for (Index f = 0; f < nf; ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Vector3 sf = face.areaVector();
    const Vector3 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
    if (isAxisAligned(sf) && exactlyParallel(d, sf)) { ++shortCircuit; continue; }
    const Real iu = cfd::discretization::interpolateInternalFace(mesh, face, dU);
    const Real iv = cfd::discretization::interpolateInternalFace(mesh, face, dV);
    const Real iw = threeD ? cfd::discretization::interpolateInternalFace(mesh, face, dW) : 0.0;
    const Vector3 rv = (sf.z == 0.0) ? Vector3{iu * sf.x, iv * sf.y, 0.0}
                                     : Vector3{iu * sf.x, iv * sf.y, iw * sf.z};
    const auto decomposition = MeshGeometry::decomposeAreaVector(d, rv);
    if (!decomposition.valid) ++fallback;
    else if (cross(d, rv) == Vector3{}) ++parallel;
    else ++wellPosed;
  }
  coverage.wellPosedFallbacks += static_cast<int>(fallback);

  ++cases;
  const bool ok = oppositeRowErrors == 0 && signErrors == 0;
  if (!ok) ++failures;
  std::printf("  %s L5 %-14s %-14s ref=%-5zu opposite[e=%zu] sign[e=%zu] ambiguous=%zu "
              "faces[axisaligned=%zu wellposed=%zu parallel=%zu fallback=%zu]\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName,
              static_cast<std::size_t>(referenceCell), oppositeRowErrors, signErrors,
              sharedFaceAmbiguous, shortCircuit, wellPosed, parallel, fallback);
}

// A response field engineered to drive dot(d, responseVector) across the
// 1e-6*|d|*|sf| guard: with dU fixed and dV swept over many decades, the
// response vector rotates, and on a skewed mesh (where d.x*sf.x and d.y*sf.y
// can carry opposite signs) the dot product changes sign somewhere in the
// sweep. Cases on BOTH sides of the threshold are therefore generated by
// construction, and the census above reports which.
void runThresholdSweep(const std::string& meshName, const Mesh& mesh,
                       const cfd::gpu::DevicePressureCorrectionPlan& plan, const char* bcName,
                       const BoundaryConditionSet& pressureBoundaries, Index referenceCell) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  SurfaceField flux(nf);
  for (Index f = 0; f < nf; ++f)
    flux[f] = fluxSwirl(f, mesh.face(f).centroid(), mesh.face(f).areaVector());
  cfd::gpu::DeviceBuffer<Real> dFlux;
  dFlux.uploadFrom(flux.data(), nf);

  std::size_t sawFallback = 0, sawWellPosed = 0, differing = 0;
  for (int decade = -12; decade <= 12; ++decade) {
    const Real ratio = std::pow(10.0, static_cast<Real>(decade));
    ScalarField dU(nc), dV(nc), dW(nc);
    for (Index c = 0; c < nc; ++c) {
      const Vector3 x = mesh.cell(c).centroid();
      dU[c] = 1.0e-2 * (1.0 + (0.3 * x.x));
      dV[c] = dU[c] * ratio;
      dW[c] = dU[c] * 0.5;
    }
    PressureCorrectionOptions options;
    options.nonOrthogonal = true;
    const auto cpu = cfd::pressure_velocity::assemblePressureCorrection(
        mesh, flux, dU, dV, 998.2, referenceCell, pressureBoundaries, options,
        threeD ? &dW : nullptr);

    cfd::gpu::DeviceBuffer<Real> ddU, ddV, ddW;
    ddU.uploadFrom(dU.data(), nc);
    ddV.uploadFrom(dV.data(), nc);
    ddW.uploadFrom(dW.data(), nc);
    cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
    deviceOptions.nonOrthogonal = true;
    deviceOptions.referenceCell = referenceCell;
    deviceOptions.density = 998.2;
    cfd::gpu::DevicePressureCorrectionSystem system;
    cfd::gpu::assemblePressureCorrectionDevice(plan, dFlux, ddU, ddV, threeD ? &ddW : nullptr,
                                               nullptr, deviceOptions, system);
    const auto gRow = pullI(system.rowOffsets);
    const auto gCol = pullI(system.columnIndices);
    const auto gVal = pullR(system.values);
    const auto gRhs = pullR(system.rhs);
    const auto& cpuMatrix = cpu.system.matrix();
    for (Index r = 0; r < nc; ++r) {
      Index g = gRow[r];
      for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
        const Index column = cpuMatrix.columnIndicesData()[k];
        while (g < gRow[r + 1] && gCol[g] < column) ++g;
        if (g >= gRow[r + 1] || gCol[g] != column) { ++differing; continue; }
        ++coefficientsCompared;
        if (sameBits(cpuMatrix.valuesData()[k], gVal[g])) ++bitwiseCoefficients; else ++differing;
        ++g;
      }
    }
    for (Index r = 0; r < nc; ++r) {
      ++coefficientsCompared;
      if (sameBits(cpu.system.rhs()[r], gRhs[r])) ++bitwiseCoefficients; else ++differing;
    }

    // Which side of the guard did this decade land on?
    for (Index f = 0; f < nf; ++f) {
      const auto& face = mesh.face(f);
      if (face.isBoundary()) continue;
      const Vector3 sf = face.areaVector();
      const Vector3 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
      const Real iu = cfd::discretization::interpolateInternalFace(mesh, face, dU);
      const Real iv = cfd::discretization::interpolateInternalFace(mesh, face, dV);
      const Real iw = threeD ? cfd::discretization::interpolateInternalFace(mesh, face, dW) : 0.0;
      const Vector3 rv = (sf.z == 0.0) ? Vector3{iu * sf.x, iv * sf.y, 0.0}
                                       : Vector3{iu * sf.x, iv * sf.y, iw * sf.z};
      if (isAxisAligned(sf) && exactlyParallel(d, sf)) continue;
      if (MeshGeometry::decomposeAreaVector(d, rv).valid) ++sawWellPosed; else ++sawFallback;
    }
  }
  coverage.wellPosedFallbacks += static_cast<int>(sawFallback);
  ++cases;
  // Straddling the guard needs faces that reach the decomposition at all. On an
  // orthogonal Cartesian mesh every internal face takes the axis-aligned
  // short-circuit first, so NO response field whatsoever can cross the guard
  // there: the requirement is inapplicable by a proven property of the mesh,
  // not relaxed. It is still enforced on every mesh that does decompose, and
  // suite-wide in the coverage gate.
  const Index decomposing = decomposingFaceCount(mesh);
  const bool applicable = decomposing > 0;
  const bool ok = differing == 0 && (!applicable || (sawFallback > 0 && sawWellPosed > 0));
  if (!ok) ++failures;
  std::printf("  %s L6 %-14s %-14s threshold sweep 25 decades differing=%zu decomposingFaces=%zu "
              "faces[wellposed=%zu fallback=%zu]%s\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName, differing,
              static_cast<std::size_t>(decomposing), sawWellPosed, sawFallback,
              applicable ? "" : "  (guard unreachable: every internal face is axis-aligned)");
}

// ---------------------------------------------------------------------------
// L7 -- INSIDE the well-posedness band
//
// The decade sweep above crosses the guard by flipping the sign of
// dot(d, responseVector); it never lands in the narrow band
//     0 < dot(d, rv) <= 1e-6 * |d| * |rv|
// which is the only region where the 1e-6 factor -- rather than mere
// positivity -- decides. Negative control G8 replaces the guard with a bare
// `> 0.0` and is invisible unless some face sits in that band, so this layer
// puts one there by construction: solve dot(d, rv) = 0 for the v-response of a
// chosen face, then step off the root by offsets small enough to stay inside
// the band.
// ---------------------------------------------------------------------------

void runBandSweep(const std::string& meshName, const Mesh& mesh,
                  const cfd::gpu::DevicePressureCorrectionPlan& plan, const char* bcName,
                  const BoundaryConditionSet& pressureBoundaries, Index referenceCell) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  if (decomposingFaceCount(mesh) == 0) return;  // guard unreachable on this mesh

  // Pick a face whose root response is positive, so the swept field stays in
  // the physical domain the production code is ever handed.
  Real root = 0.0;
  bool haveRoot = false;
  for (Index f = 0; f < nf && !haveRoot; ++f) {
    const auto& face = mesh.face(f);
    if (face.isBoundary()) continue;
    const Vector3 sf = face.areaVector();
    const Vector3 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
    if (isAxisAligned(sf) && exactlyParallel(d, sf)) continue;
    const Real denominator = d.y * sf.y;
    if (denominator == 0.0) continue;
    const Real numerator = (d.x * sf.x) + (threeD ? d.z * sf.z : 0.0);
    const Real candidate = -numerator / denominator;
    if (candidate > 0.0 && std::isfinite(candidate)) { root = candidate; haveRoot = true; }
  }
  if (!haveRoot) return;

  SurfaceField flux(nf);
  for (Index f = 0; f < nf; ++f)
    flux[f] = fluxSwirl(f, mesh.face(f).centroid(), mesh.face(f).areaVector());
  cfd::gpu::DeviceBuffer<Real> dFlux;
  dFlux.uploadFrom(flux.data(), nf);

  const Real offsets[] = {0.0,     1e-9,  -1e-9, 1e-8,  -1e-8, 1e-7, -1e-7,
                          3e-7,    -3e-7, 1e-6,  -1e-6, 3e-6,  -3e-6, 1e-5,
                          -1e-5,   1e-4,  -1e-4};
  std::size_t differing = 0, inBand = 0, evaluated = 0;
  for (const Real offset : offsets) {
    const Real r = root + offset;
    if (!(r > 0.0)) continue;
    ScalarField dU(nc), dV(nc), dW(nc);
    for (Index c = 0; c < nc; ++c) { dU[c] = 1.0; dV[c] = r; dW[c] = 1.0; }

    PressureCorrectionOptions options;
    options.nonOrthogonal = true;
    const auto cpu = cfd::pressure_velocity::assemblePressureCorrection(
        mesh, flux, dU, dV, 998.2, referenceCell, pressureBoundaries, options,
        threeD ? &dW : nullptr);

    cfd::gpu::DeviceBuffer<Real> ddU, ddV, ddW;
    ddU.uploadFrom(dU.data(), nc);
    ddV.uploadFrom(dV.data(), nc);
    ddW.uploadFrom(dW.data(), nc);
    cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
    deviceOptions.nonOrthogonal = true;
    deviceOptions.referenceCell = referenceCell;
    deviceOptions.density = 998.2;
    cfd::gpu::DevicePressureCorrectionSystem system;
    cfd::gpu::assemblePressureCorrectionDevice(plan, dFlux, ddU, ddV, threeD ? &ddW : nullptr,
                                               nullptr, deviceOptions, system);
    const auto gVal = pullR(system.values);
    const auto gRow = pullI(system.rowOffsets);
    const auto gCol = pullI(system.columnIndices);
    const auto gCoefficient = pullR(system.faceCoefficient);
    const auto& cpuMatrix = cpu.system.matrix();
    for (Index rr = 0; rr < nc; ++rr) {
      Index g = gRow[rr];
      for (Index k = cpuMatrix.rowOffsetsData()[rr]; k < cpuMatrix.rowOffsetsData()[rr + 1]; ++k) {
        const Index column = cpuMatrix.columnIndicesData()[k];
        while (g < gRow[rr + 1] && gCol[g] < column) ++g;
        if (g >= gRow[rr + 1] || gCol[g] != column) { ++differing; continue; }
        ++coefficientsCompared;
        if (sameBits(cpuMatrix.valuesData()[k], gVal[g])) ++bitwiseCoefficients; else ++differing;
        ++g;
      }
    }
    for (Index f = 0; f < nf; ++f) {
      ++coefficientsCompared;
      if (sameBits(cpu.faceCoefficient[f], gCoefficient[f])) ++bitwiseCoefficients; else ++differing;
    }

    // Census: how many faces sit strictly inside the band this offset targets?
    for (Index f = 0; f < nf; ++f) {
      const auto& face = mesh.face(f);
      if (face.isBoundary()) continue;
      const Vector3 sf = face.areaVector();
      const Vector3 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
      if (isAxisAligned(sf) && exactlyParallel(d, sf)) continue;
      const Real iu = cfd::discretization::interpolateInternalFace(mesh, face, dU);
      const Real iv = cfd::discretization::interpolateInternalFace(mesh, face, dV);
      const Real iw = threeD ? cfd::discretization::interpolateInternalFace(mesh, face, dW) : 0.0;
      const Vector3 rv = (sf.z == 0.0) ? Vector3{iu * sf.x, iv * sf.y, 0.0}
                                       : Vector3{iu * sf.x, iv * sf.y, iw * sf.z};
      const Real dotValue = dot(d, rv);
      const Real threshold = 1e-6 * magnitude(d) * magnitude(rv);
      ++evaluated;
      if (dotValue > 0.0 && dotValue <= threshold) ++inBand;
    }
  }
  coverage.bandHits += static_cast<int>(inBand);
  ++cases;
  const bool ok = differing == 0 && inBand > 0;
  if (!ok) ++failures;
  std::printf("  %s L7 %-14s %-14s band sweep root=%.6g differing=%zu facesInBand=%zu/%zu\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName, root, differing, inBand, evaluated);
}

// ---------------------------------------------------------------------------
// L2 -- the full upstream chain
// ---------------------------------------------------------------------------

void runChain(const std::string& meshName, const Mesh& mesh, Index scheme, Real alpha) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
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
  const FluidProperties fluid(1.2, 1.0e-3);
  const Index referenceCell = 0;

  VectorField velocity(nc);
  ScalarField pressure(nc), viscosity(nc), prevU(nc), prevV(nc), prevW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    const Real pi = cfd::constants::pi;
    velocity[c] = Vector3{std::sin(pi * x.x) * std::cos(pi * x.y) + 0.5,
                          std::cos(pi * x.x) * std::sin(pi * x.y) - 0.25,
                          (0.3 * std::sin(pi * x.z)) + (0.2 * x.x)};
    pressure[c] = 101325.0 + (500.0 * std::sin(pi * x.x) * std::cos(pi * x.y));
    viscosity[c] = 1.0e-3 + (4.0e-3 * x.x);
    prevU[c] = velocity[c].x - 0.1;
    prevV[c] = velocity[c].y + 0.05;
    prevW[c] = velocity[c].z - 0.02;
  }
  SurfaceField seedFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    seedFlux[f] = fluxSwirl(f, face.centroid(), face.areaVector());
  }

  // --- CPU chain ---------------------------------------------------------
  const auto assemble = [&](Index component, const ScalarField& previous) {
    return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, velocity, pressure, seedFlux, viscosity, velocityBoundaries, pressureBoundaries,
        static_cast<cfd::physics::VelocityComponent>(component), previous, alpha, nullptr, nullptr,
        static_cast<cfd::discretization::ConvectionScheme>(scheme));
  };
  const ScalarField cpuDU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, assemble(0, prevU).diagonal);
  const ScalarField cpuDV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, assemble(1, prevV).diagonal);
  ScalarField cpuDW;
  if (threeD) cpuDW = cfd::pressure_velocity::computeMomentumResponseCoefficient(
      mesh, assemble(2, prevW).diagonal);
  const VectorField gradP = cfd::discretization::gradient(
      mesh, pressure, pressureBoundaries, cfd::discretization::GradientScheme::GreenGauss);
  const SurfaceField cpuFlux = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, pressure, gradP, cpuDU, cpuDV, threeD ? &cpuDW : nullptr, fluid,
      velocityBoundaries, alpha);
  const auto cpu = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, cpuFlux, cpuDU, cpuDV, fluid.density(), referenceCell, pressureBoundaries, {},
      threeD ? &cpuDW : nullptr);

  // --- GPU chain ---------------------------------------------------------
  cfd::gpu::DeviceMomentumAssemblyPlan momentumPlan;
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  cfd::gpu::DevicePressureCorrectionPlan pcorrPlan;
  if (!momentumPlan.build(mesh, velocityBoundaries, pressureBoundaries) ||
      !fluxPlan.build(mesh, velocityBoundaries) || !pcorrPlan.build(mesh, pressureBoundaries)) {
    ++failures;
    std::printf("  FAIL L2 %-14s plan unusable\n", meshName.c_str());
    return;
  }
  cfd::gpu::DeviceVelocity deviceVelocity;
  std::vector<Real> vx(nc), vy(nc), vz(nc);
  for (Index c = 0; c < nc; ++c) { vx[c] = velocity[c].x; vy[c] = velocity[c].y; vz[c] = velocity[c].z; }
  deviceVelocity.x.uploadFrom(vx.data(), nc);
  deviceVelocity.y.uploadFrom(vy.data(), nc);
  deviceVelocity.z.uploadFrom(vz.data(), nc);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dSeed, dPrevU, dPrevV, dPrevW;
  dPressure.uploadFrom(pressure.data(), nc);
  dViscosity.uploadFrom(viscosity.data(), nc);
  dSeed.uploadFrom(seedFlux.data(), static_cast<Index>(seedFlux.size()));
  dPrevU.uploadFrom(prevU.data(), nc);
  dPrevV.uploadFrom(prevV.data(), nc);
  dPrevW.uploadFrom(prevW.data(), nc);

  const auto response = [&](Index component, cfd::gpu::DeviceBuffer<Real>& previous,
                            cfd::gpu::DeviceBuffer<Real>& out) {
    cfd::gpu::MomentumAssemblyOptions options;
    options.component = component;
    options.convectionScheme = scheme;
    options.relaxationAlpha = alpha;
    cfd::gpu::DeviceMomentumSystem system;
    cfd::gpu::assembleRelaxedMomentumDevice(momentumPlan, deviceVelocity, dPressure, dViscosity,
                                            dSeed, previous, nullptr, options, system);
    cfd::gpu::computeMomentumResponseCoefficientDevice(momentumPlan.convectionPlan().mesh(),
                                                       system.diagonal, out);
  };
  cfd::gpu::DeviceBuffer<Real> gpuDU, gpuDV, gpuDW;
  response(0, dPrevU, gpuDU);
  response(1, dPrevV, gpuDV);
  if (threeD) response(2, dPrevW, gpuDW);

  cfd::gpu::DeviceBuffer<Real> dGx, dGy, dGz;
  std::vector<Real> gx(nc), gy(nc), gz(nc);
  for (Index c = 0; c < nc; ++c) { gx[c] = gradP[c].x; gy[c] = gradP[c].y; gz[c] = gradP[c].z; }
  dGx.uploadFrom(gx.data(), nc);
  dGy.uploadFrom(gy.data(), nc);
  dGz.uploadFrom(gz.data(), nc);
  cfd::gpu::DeviceBuffer<Real> gpuFlux;
  cfd::gpu::rhieChowMassFluxDevice(fluxPlan, deviceVelocity, dPressure, dGx, dGy, dGz, gpuDU,
                                   gpuDV, threeD ? &gpuDW : nullptr, fluid.density(), alpha,
                                   gpuFlux);
  cfd::gpu::PressureCorrectionOptionsDevice deviceOptions;
  deviceOptions.referenceCell = referenceCell;
  deviceOptions.density = fluid.density();
  cfd::gpu::DevicePressureCorrectionSystem system;
  cfd::gpu::assemblePressureCorrectionDevice(pcorrPlan, gpuFlux, gpuDU, gpuDV,
                                             threeD ? &gpuDW : nullptr, nullptr, deviceOptions,
                                             system);

  const auto& cpuMatrix = cpu.system.matrix();
  const auto& cpuRhs = cpu.system.rhs();
  const auto gRow = pullI(system.rowOffsets);
  const auto gCol = pullI(system.columnIndices);
  const auto gVal = pullR(system.values);
  const auto gRhs = pullR(system.rhs);
  std::size_t dA = 0, dRhs = 0, missing = 0, extraNonZero = 0;
  Real maxAbs = 0.0, scale = 0.0;
  for (Index r = 0; r < nc; ++r) {
    Index g = gRow[r];
    for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
      const Index column = cpuMatrix.columnIndicesData()[k];
      while (g < gRow[r + 1] && gCol[g] < column) { if (gVal[g] != 0.0) ++extraNonZero; ++g; }
      if (g >= gRow[r + 1] || gCol[g] != column) { ++missing; continue; }
      ++coefficientsCompared;
      if (sameBits(cpuMatrix.valuesData()[k], gVal[g])) ++bitwiseCoefficients; else ++dA;
      maxAbs = std::max(maxAbs, std::abs(cpuMatrix.valuesData()[k] - gVal[g]));
      scale = std::max(scale, std::abs(cpuMatrix.valuesData()[k]));
      ++g;
    }
    for (; g < gRow[r + 1]; ++g) if (gVal[g] != 0.0) ++extraNonZero;
  }
  for (Index r = 0; r < nc; ++r) {
    ++coefficientsCompared;
    if (sameBits(cpuRhs[r], gRhs[r])) ++bitwiseCoefficients; else ++dRhs;
    maxAbs = std::max(maxAbs, std::abs(cpuRhs[r] - gRhs[r]));
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  ++cases;
  ++coverage.integrated;
  const bool ok = dA == 0 && dRhs == 0 && missing == 0 && extraNonZero == 0;
  if (!ok) ++failures;
  std::printf("  %s L2 %-14s scheme=%d a=%-5.3g chain A[d=%zu] rhs[d=%zu] maxAbs=%-10.3g "
              "scale=%.4g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), static_cast<int>(scheme), alpha, dA, dRhs,
              maxAbs, scale);
}

void runMesh(const std::string& name, const Mesh& mesh, bool verbose) {
  const std::vector<PressureBcCase> bcs = {
      {"all-Neumann", allNeumannPressure, /*expectPin=*/true},
      {"one-FixedValue", oneFixedValuePressure, /*expectPin=*/false},
      {"mixed", mixedPressure, /*expectPin=*/false}};
  const std::vector<FluxCase> fluxes = {{"zero", fluxZero},   {"positive", fluxPositive},
                                        {"negative", fluxNegative}, {"mixed", fluxMixed},
                                        {"swirl", fluxSwirl}};
  const std::vector<ResponseCase> responses = {{"uniform", respUniform, respUniform},
                                               {"varying", respVarying, respVarying},
                                               {"anisotropic", respAnisoU, respAnisoV}};
  const std::vector<Real> densities = {1.0, 998.2};
  const Index nc = mesh.numberOfCells();
  const std::vector<Index> references = {0, nc / 2, nc - 1};

  for (const auto& bcCase : bcs) {
    const BoundaryConditionSet pressureBoundaries = bcCase.build(mesh);
    cfd::gpu::DevicePressureCorrectionPlan plan;
    if (!plan.build(mesh, pressureBoundaries)) {
      ++failures;
      std::printf("  FAIL %-14s %-12s plan unusable: %s\n", name.c_str(), bcCase.name,
                  plan.unsupportedReason().c_str());
      continue;
    }
    if (plan.axisAlignedFaces() > 0) ++coverage.axisAlignedPlans;
    if (plan.generalFaces() > 0) ++coverage.generalPlans;
    std::printf("       [%s/%s pin=%s fixedValueFaces=%zu axisAligned=%zu general=%zu "
                "resident=%zu B]\n",
                name.c_str(), bcCase.name, plan.pinReferenceCell() ? "yes" : "no",
                static_cast<std::size_t>(plan.fixedValueBoundaryFaces()),
                static_cast<std::size_t>(plan.axisAlignedFaces()),
                static_cast<std::size_t>(plan.generalFaces()), plan.residentBytes());

    for (const Index reference : references) {
      runStructural(name, mesh, plan, bcCase.name, reference);
    }
    runThresholdSweep(name, mesh, plan, bcCase.name, pressureBoundaries, references[1]);
    runBandSweep(name, mesh, plan, bcCase.name, pressureBoundaries, references[1]);

    for (const auto& flux : fluxes) {
      for (const auto& response : responses) {
        for (const Real density : densities) {
          for (const Index reference : references) {
            runDirect(name, mesh, plan, bcCase, pressureBoundaries, flux, response, density,
                      reference, /*nonOrthogonal=*/false, /*withPrevious=*/false, verbose);
            runDirect(name, mesh, plan, bcCase, pressureBoundaries, flux, response, density,
                      reference, /*nonOrthogonal=*/true, /*withPrevious=*/false, verbose);
            runDirect(name, mesh, plan, bcCase, pressureBoundaries, flux, response, density,
                      reference, /*nonOrthogonal=*/true, /*withPrevious=*/true, verbose);
          }
        }
      }
    }
  }
  for (const Index scheme : {0, 3}) runChain(name, mesh, scheme, 0.7);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001I: CUDA pressure-correction assembly vs CPU (bitwise) ===\n");
  std::printf("CPU reference: pressure_velocity::assemblePressureCorrection (assembly only)\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 3", warped3D(3), false);
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
  std::printf("  2D / 3D cases                     %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  pinned / unpinned cases           %d / %d\n", coverage.pinned, coverage.unpinned);
  std::printf("  faces with owner==ref / neighbour==ref / neither  %d / %d / %d\n",
              coverage.ownerIsRef, coverage.neighbourIsRef, coverage.neitherIsRef);
  std::printf("  decomposition fallbacks observed  %d\n", coverage.wellPosedFallbacks);
  std::printf("  faces inside the 1e-6 guard band  %d\n", coverage.bandHits);
  std::printf("  coupled (FixedValue) boundary faces %d\n", coverage.coupledBoundaryFaces);
  std::printf("  faces carrying an explicit term   %d\n", coverage.explicitTermFaces);
  std::printf("  nonOrthogonal off / on            %d / %d\n", coverage.nonOrthOff,
              coverage.nonOrthOn);
  std::printf("  meshes with axis-aligned coupling %d\n", coverage.axisAlignedPlans);
  std::printf("  meshes with general coupling      %d\n", coverage.generalPlans);
  std::printf("  continuity/RHS convention checks  %d\n", coverage.continuityChecks);
  std::printf("  integrated chain cases            %d\n", coverage.integrated);
  std::printf("  sparsity pattern mismatches       %d\n", coverage.patternMismatch);
  std::printf("  coefficients compared             %zu\n", coefficientsCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseCoefficients);
  std::printf("  max absolute discrepancy          %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy          %.3g\n", globalMaxRel);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.pinned == 0 || coverage.unpinned == 0) { ++failures; std::printf("  FAIL both pin decisions not exercised\n"); }
  if (coverage.ownerIsRef == 0 || coverage.neighbourIsRef == 0 || coverage.neitherIsRef == 0) { ++failures; std::printf("  FAIL the three reference-face topologies were not all exercised\n"); }
  if (coverage.wellPosedFallbacks == 0) { ++failures; std::printf("  FAIL the well-posedness guard never rejected a decomposition\n"); }
  if (coverage.bandHits == 0) { ++failures; std::printf("  FAIL no face ever landed inside the 1e-6 guard band\n"); }
  if (coverage.coupledBoundaryFaces == 0) { ++failures; std::printf("  FAIL no FixedValue boundary face ever coupled\n"); }
  if (coverage.explicitTermFaces == 0) { ++failures; std::printf("  FAIL the explicit non-orthogonal term was never non-zero\n"); }
  if (coverage.nonOrthOn == 0 || coverage.nonOrthOff == 0) { ++failures; std::printf("  FAIL both coupling modes not exercised\n"); }
  if (coverage.integrated == 0) { ++failures; std::printf("  FAIL the integrated chain never ran\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "PRESSURE CORRECTION EQUIVALENCE: PASS (bitwise)"
                                    : "PRESSURE CORRECTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
