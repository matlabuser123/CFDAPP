// P12-MESH-007: topology-preserving mesh motion -- geometry update, mesh
// velocity, swept volumes and the discrete geometric conservation law
// (results/p12-mesh-007/acceptance_gate.md G1.1, G1.3, G2, G3, G4).
//
// Kind of check: VERIFICATION against analytical answers (affine motions map
// the reference geometry exactly: volumes by det A, points by the map, area
// vectors by cof A; vertex velocities are known in closed form) and against
// INDEPENDENT formulas implemented here (the swept volume of a face as the
// signed area of the swept quadrilateral in 2D, or the 3 x 3 x 3 Gauss volume
// of the swept trilinear region in 3D -- not the production formula). The
// non-affine (sinusoidal) geometry is checked against an independent Python
// implementation (G2.3) from the data MeshMotionEvidence dumps.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "MeshMotionCases.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"

namespace {

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector3;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshGeometryState;
using cfd::mesh::MeshMotion;
using cfd::mesh::MeshMotionStep;
using m7::kDt;
using m7::kEps;
using m7::kSteps;

// ---- helpers ------------------------------------------------------------------------------

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }
bool sameBits(const Vector3& a, const Vector3& b) {
  return sameBits(a.x, b.x) && sameBits(a.y, b.y) && sameBits(a.z, b.z);
}

// Index of the first differing entry, or -1: bitwise comparison of two geometries.
long firstBitwiseDifference(const MeshGeometryState& a, const MeshGeometryState& b) {
  for (std::size_t i = 0; i < a.cellVolumes.size(); ++i) {
    if (!sameBits(a.cellVolumes[i], b.cellVolumes[i]) ||
        !sameBits(a.cellCentroids[i], b.cellCentroids[i])) {
      return static_cast<long>(i);
    }
  }
  for (std::size_t i = 0; i < a.faceCentroids.size(); ++i) {
    if (!sameBits(a.faceCentroids[i], b.faceCentroids[i]) ||
        !sameBits(a.faceAreaVectors[i], b.faceAreaVectors[i])) {
      return 1000000 + static_cast<long>(i);
    }
  }
  if (a.blockVertices.size() != b.blockVertices.size()) return 2000000;
  for (std::size_t bl = 0; bl < a.blockVertices.size(); ++bl) {
    if (a.blockVertices[bl].size() != b.blockVertices[bl].size()) return 2000001;
    for (std::size_t v = 0; v < a.blockVertices[bl].size(); ++v) {
      if (!sameBits(a.blockVertices[bl][v], b.blockVertices[bl][v])) return 3000000 + static_cast<long>(v);
    }
  }
  return -1;
}

// Everything that must never change under motion (G2.7).
struct TopologySnapshot {
  std::vector<std::vector<Index>> cellFaces;
  std::vector<Index> cellIds;
  std::vector<std::tuple<Index, Index, long>> faces;  // id, owner, neighbor (-1: boundary)
  std::vector<std::pair<std::string, std::vector<Index>>> patches;
  std::vector<std::tuple<Index, Index, Index, std::string, std::size_t>> blocks;
  bool operator==(const TopologySnapshot&) const = default;
};

TopologySnapshot topologyOf(const Mesh& mesh) {
  TopologySnapshot t;
  for (const auto& cell : mesh.cells()) {
    t.cellIds.push_back(cell.id());
    t.cellFaces.push_back(cell.faceIds());
  }
  for (const auto& face : mesh.faces()) {
    t.faces.emplace_back(face.id(), face.owner(),
                         face.neighbor().has_value() ? static_cast<long>(*face.neighbor()) : -1L);
  }
  for (const auto& patch : mesh.boundaryPatches()) t.patches.emplace_back(patch.name(), patch.faceIds());
  for (const auto& g : mesh.structuredBlocks()) {
    t.blocks.emplace_back(g.nx, g.ny, g.nz, g.name, g.vertices.size());
  }
  return t;
}

struct NamedMesh {
  std::string name;
  Mesh (*make)();
};

std::vector<NamedMesh> allMeshes() {
  return {{"C16", &m7::c16}, {"G16", &m7::g16}, {"Q16", &m7::q16}, {"MB2", &m7::mb2}, {"H8", &m7::h8}};
}

// Worst ratio measured/bound over everything a test checks, printed as evidence.
struct Worst {
  Real ratio{0.0};
  void add(Real measured, Real bound) {
    ratio = std::max(ratio, bound > 0.0 ? measured / bound : (measured > 0.0 ? 1e300 : 0.0));
  }
};

// Signed area of polygon p (shoelace).
Real shoelace(const std::vector<Vector3>& p) {
  Real twice = 0.0;
  for (std::size_t k = 0; k < p.size(); ++k) {
    const Vector3& a = p[k];
    const Vector3& b = p[(k + 1) % p.size()];
    twice += (a.x * b.y) - (b.x * a.y);
  }
  return 0.5 * twice;
}

// Independent 3D: volume of the trilinear hexahedron c[0..7] (corners (0,0,0),
// (1,0,0), (1,1,0), (0,1,0), then at z = 1) by 3 x 3 x 3 Gauss of det J.
Real trilinearVolume333(const std::array<Vector3, 8>& x) {
  const Real g[3] = {0.5 - (0.5 * std::sqrt(0.6)), 0.5, 0.5 + (0.5 * std::sqrt(0.6))};
  const Real w[3] = {5.0 / 18.0, 8.0 / 18.0, 5.0 / 18.0};
  Real volume = 0.0;
  for (int a = 0; a < 3; ++a) {
    for (int b = 0; b < 3; ++b) {
      for (int c = 0; c < 3; ++c) {
        const Real xi = g[a];
        const Real eta = g[b];
        const Real zeta = g[c];
        const Vector3 dxi = ((x[1] - x[0]) * ((1 - eta) * (1 - zeta))) + ((x[2] - x[3]) * (eta * (1 - zeta))) +
                            ((x[5] - x[4]) * ((1 - eta) * zeta)) + ((x[6] - x[7]) * (eta * zeta));
        const Vector3 deta = ((x[3] - x[0]) * ((1 - xi) * (1 - zeta))) + ((x[2] - x[1]) * (xi * (1 - zeta))) +
                             ((x[7] - x[4]) * ((1 - xi) * zeta)) + ((x[6] - x[5]) * (xi * zeta));
        const Vector3 dzeta = ((x[4] - x[0]) * ((1 - xi) * (1 - eta))) + ((x[5] - x[1]) * (xi * (1 - eta))) +
                              ((x[6] - x[2]) * (xi * eta)) + ((x[7] - x[3]) * ((1 - xi) * eta));
        volume += w[a] * w[b] * w[c] * dot(dxi, cross(deta, dzeta));
      }
    }
  }
  return volume;
}

// The independent swept volume of face f between vertex sets `from` and `to`
// (G4.3): 2D -- signed shoelace area of (p^n, p^{n+1}, q^{n+1}, q^n) with the
// edge p -> q oriented so its right-hand normal is the face's area vector;
// 3D -- the trilinear volume of the region with the face at t^n as bottom and
// at t^{n+1} as top (face vertices ordered along the area vector).
Real independentSwept(const Mesh& mesh, const MeshGeometry::StructuredTopology& topology,
                      Index f, const std::vector<Vector3>& from, const std::vector<Vector3>& to) {
  const auto& fv = topology.faces[f];
  const Vector3 area = mesh.face(f).areaVector();
  if (topology.dimension == 2) {
    Index p = fv.vertex[0];
    Index q = fv.vertex[1];
    const Vector3 e = to[q] - to[p];
    if (dot(Vector3{e.y, -e.x, 0.0}, area) < 0.0) std::swap(p, q);
    return shoelace({from[p], to[p], to[q], from[q]});
  }
  std::array<Index, 4> v = fv.vertex;
  if (dot(cross(to[v[2]] - to[v[0]], to[v[3]] - to[v[1]]), area) < 0.0) std::swap(v[1], v[3]);
  return trilinearVolume333({from[v[0]], from[v[1]], from[v[2]], from[v[3]], to[v[0]], to[v[1]],
                             to[v[2]], to[v[3]]});
}

// --- G1.1 / G3.1 ---------------------------------------------------------------------------

TEST(MeshMotionStatic, StationaryMotionLeavesEveryMeshBitwiseUnchanged) {
  for (const auto& [name, make] : allMeshes()) {
    Mesh mesh = make();
    const MeshGeometryState original = mesh.geometry();
    const TopologySnapshot topology = topologyOf(mesh);
    MeshMotion motion(mesh, m7::stationary());
    for (int n = 1; n <= 5; ++n) {
      const MeshMotionStep& step = motion.advance(n * kDt);
      EXPECT_FALSE(step.moved) << name;
      EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), original), -1) << name << " step " << n;
      for (const Real v : step.sweptVolumes) EXPECT_TRUE(sameBits(v, 0.0)) << name;
      for (const Real v : step.meshVolumeFlux) EXPECT_TRUE(sameBits(v, 0.0)) << name;
      for (const Vector3& v : step.vertexVelocities) EXPECT_TRUE(sameBits(v, Vector3{})) << name;
      for (const Real r : step.gclResiduals) EXPECT_TRUE(sameBits(r, 0.0)) << name;
      ASSERT_EQ(step.previousVolumes.size(), mesh.numberOfCells());
      for (Index c = 0; c < mesh.numberOfCells(); ++c) {
        EXPECT_TRUE(sameBits(step.previousVolumes[c], mesh.cell(c).volume())) << name;
      }
    }
    EXPECT_TRUE(topologyOf(mesh) == topology) << name;
    std::printf("G1.1 %s: 5 stationary advances, geometry bitwise unchanged, dV = v = 0.0\n",
                name.c_str());
  }
}

// --- G1.3 ----------------------------------------------------------------------------------

TEST(MeshMotionKernel, RecomputedGeometryReproducesTheBuilders) {
  for (const auto& [name, make] : allMeshes()) {
    const Mesh mesh = make();
    const auto topology = MeshGeometry::structuredTopology(mesh);
    const MeshGeometryState builder = mesh.geometry();
    const MeshGeometryState kernel = MeshGeometry::computeGeometry(topology, topology.vertices);
    if (name == "Q16" || name == "MB2") {
      EXPECT_EQ(firstBitwiseDifference(kernel, builder), -1) << name;
      std::printf("G1.3 %s: kernel geometry BITWISE identical to the builder's\n", name.c_str());
      continue;
    }
    const Real x = m7::maxAbsCoordinate(topology.vertices);
    Worst worst;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      const Real dv = std::abs(kernel.cellVolumes[c] - builder.cellVolumes[c]);
      const Real dc = magnitude(kernel.cellCentroids[c] - builder.cellCentroids[c]);
      if (topology.dimension == 2) {
        worst.add(dv, 16 * kEps * x * x);
        worst.add(dc * builder.cellVolumes[c], 32 * kEps * x * x * x);
      } else {
        const Real hc = m7::hexLongestEdge(topology.vertices, topology.cells[c].corner);
        worst.add(dv, 64 * kEps * x * hc * hc);
        worst.add(dc, 64 * kEps * x);
      }
    }
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const Real dx = magnitude(kernel.faceCentroids[f] - builder.faceCentroids[f]);
      const Real ds = magnitude(kernel.faceAreaVectors[f] - builder.faceAreaVectors[f]);
      if (topology.dimension == 2) {
        worst.add(dx, 8 * kEps * x);
        worst.add(ds, 8 * kEps * x);
      } else {
        const Real hf = m7::faceLongestDiagonal(topology.vertices, topology.faces[f].vertex);
        worst.add(dx, 16 * kEps * x);
        worst.add(ds, 16 * kEps * x * hf);
      }
    }
    EXPECT_LE(worst.ratio, 1.0) << name;
    std::printf("G1.3 %s: kernel vs builder, worst measured/bound = %.3e\n", name.c_str(),
                worst.ratio);
  }
}

TEST(MeshMotionKernel, TopologyMapWeldsMultiBlockInterfaceVertices) {
  const Mesh mesh = m7::mb2();
  const auto topology = MeshGeometry::structuredTopology(mesh);
  // 9 x 17 + 9 x 17 block vertices, 17 of them shared on the interface x = 0.5.
  EXPECT_EQ(topology.vertices.size(), (2 * 9 * 17) - 17);
  for (Index j = 0; j <= 16; ++j) {
    EXPECT_EQ(topology.blockVertexIds[0][(j * 9) + 8], topology.blockVertexIds[1][j * 9]);
  }
}

TEST(MeshMotionKernel, AMeshWithoutAStructuredGridCannotMove) {
  const Mesh built = m7::c16();
  const Mesh bare(built.cells(), built.faces(), built.boundaryPatches());
  EXPECT_THROW((void)MeshGeometry::structuredTopology(bare), InvalidArgumentError);
}

// --- G2 (affine: analytical) + G3.2 / G3.3 + G4 ----------------------------------------------

struct AffineCase {
  std::string name;
  Mesh (*make)();
  m7::AffineSpec (*spec)();
};

// One advance at a time, checking every G2/G3/G4 item that has an analytical
// answer for an affine motion; prints the worst measured/bound per group.
void runAffineCase(const AffineCase& run) {
  Mesh mesh = run.make();
  const m7::AffineSpec spec = run.spec();
  const MeshGeometryState reference = mesh.geometry();
  const TopologySnapshot topology0 = topologyOf(mesh);
  MeshMotion motion(mesh, m7::affine(spec));
  const auto& topology = motion.topology();
  const bool is3d = topology.dimension == 3;
  Real totalReference = 0.0;
  for (const Real v : reference.cellVolumes) totalReference += v;
  Worst geometry;
  Worst closure;
  Worst velocity;
  Worst gcl;
  Worst sweptIndependent;
  Worst globalGcl;
  Worst total;
  Real minVolume = 1e300;
  for (int n = 1; n <= kSteps; ++n) {
    const std::vector<Vector3> before = motion.currentVertices();
    const Real tau = n * kDt;
    const MeshMotionStep& step = motion.advance(tau);
    ASSERT_TRUE(step.moved);
    const std::vector<Vector3>& after = motion.currentVertices();
    const Real x = step.maxCoordinate;
    const m7::Matrix a = m7::deformationGradient(spec, tau);
    const Real det = m7::determinant(a);
    const auto image = [&](const Vector3& p) { return motion.motion().position(p, tau); };
    // G2.1 / G2.2 cells
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      const Real volume = mesh.cell(c).volume();
      minVolume = std::min(minVolume, volume);
      EXPECT_GT(volume, 0.0);
      const Real dv = std::abs(volume - (det * reference.cellVolumes[c]));
      const Real dc = magnitude(mesh.cell(c).centroid() - image(reference.cellCentroids[c]));
      if (is3d) {
        const Real hc = m7::hexLongestEdge(after, topology.cells[c].corner);
        geometry.add(dv, 64 * kEps * x * hc * hc);
        geometry.add(dc, 64 * kEps * x);
      } else {
        geometry.add(dv, 16 * kEps * x * x);
        geometry.add(dc * volume, 32 * kEps * x * x * x);
      }
    }
    // G2.2 faces, G2.5 orientation
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const auto& face = mesh.face(f);
      const Real dx = magnitude(face.centroid() - image(reference.faceCentroids[f]));
      const Real ds = magnitude(face.areaVector() - m7::cofactorTimes(a, reference.faceAreaVectors[f]));
      if (is3d) {
        const Real hf = m7::faceLongestDiagonal(after, topology.faces[f].vertex);
        geometry.add(dx, 16 * kEps * x);
        geometry.add(ds, 16 * kEps * x * hf);
      } else {
        geometry.add(dx, 8 * kEps * x);
        geometry.add(ds, 8 * kEps * x);
      }
      const Vector3 d = face.neighbor().has_value()
                            ? mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid()
                            : face.centroid() - mesh.cell(face.owner()).centroid();
      EXPECT_GT(dot(face.areaVector(), d), 0.0) << run.name << " face " << f;
    }
    // G2.4 closure
    std::vector<Vector3> sum(mesh.numberOfCells());
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const auto& face = mesh.face(f);
      sum[face.owner()] += face.areaVector();
      if (face.neighbor().has_value()) sum[*face.neighbor()] -= face.areaVector();
    }
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      closure.add(magnitude(sum[c]), is3d ? 64 * kEps * x * m7::hexLongestEdge(after, topology.cells[c].corner)
                                          : 8 * kEps * x);
    }
    // G2.6 total volume, within the sum of the per-cell volume bounds
    Real totalNow = 0.0;
    Real volumeBoundSum = 0.0;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      totalNow += mesh.cell(c).volume();
      const Real hc = is3d ? m7::hexLongestEdge(after, topology.cells[c].corner) : 0.0;
      volumeBoundSum += is3d ? 64 * kEps * x * hc * hc : 16 * kEps * x * x;
    }
    total.add(std::abs(totalNow - (det * totalReference)), volumeBoundSum);
    // G3.2 / G3.3 vertex velocities: G (X - c) + b exactly
    const auto* affineMotion = dynamic_cast<const cfd::mesh::AffineMotion*>(&motion.motion());
    ASSERT_NE(affineMotion, nullptr);
    for (std::size_t v = 0; v < after.size(); ++v) {
      const Vector3 exact = affineMotion->velocity(motion.referenceVertices()[v]);
      const Vector3 err = step.vertexVelocities[v] - exact;
      const Real bound = 8 * kEps * x / step.dt;
      velocity.add(std::max({std::abs(err.x), std::abs(err.y), std::abs(err.z)}), bound);
    }
    // G4.1 / G4.3 / G4.4
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      const Real bound = is3d ? 1024 * kEps * x * std::pow(m7::hexLongestEdge(after, topology.cells[c].corner), 2)
                              : 256 * kEps * x * x;
      gcl.add(std::abs(step.gclResiduals[c]), bound);
      if (run.name.find("TR") != std::string::npos) {
        gcl.add(std::abs(mesh.cell(c).volume() - step.previousVolumes[c]), bound);  // G4.4
      }
    }
    Real boundSum = 0.0;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      boundSum += is3d ? 1024 * kEps * x * std::pow(m7::hexLongestEdge(after, topology.cells[c].corner), 2)
                       : 256 * kEps * x * x;
    }
    globalGcl.add(std::abs(step.globalGclResidual), boundSum);
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const Real independent = independentSwept(mesh, topology, f, before, after);
      const Real bound = is3d ? 1024 * kEps * x *
                                    std::pow(m7::faceLongestDiagonal(after, topology.faces[f].vertex), 2)
                              : 256 * kEps * x * x;
      sweptIndependent.add(std::abs(step.sweptVolumes[f] - independent), bound);
    }
  }
  EXPECT_TRUE(topologyOf(mesh) == topology0) << run.name;  // G2.7
  EXPECT_LE(geometry.ratio, 1.0) << run.name;
  EXPECT_LE(closure.ratio, 1.0) << run.name;
  EXPECT_LE(total.ratio, 1.0) << run.name;
  EXPECT_LE(velocity.ratio, 1.0) << run.name;
  EXPECT_LE(gcl.ratio, 1.0) << run.name;
  EXPECT_LE(globalGcl.ratio, 1.0) << run.name;
  EXPECT_LE(sweptIndependent.ratio, 1.0) << run.name;
  std::printf("G2/G3/G4 %s: %d advances, min V %.4e; worst measured/bound: geometry %.3e closure "
              "%.3e total %.3e velocity %.3e GCL %.3e globalGCL %.3e swept-vs-independent %.3e\n",
              run.name.c_str(), kSteps, minVolume, geometry.ratio, closure.ratio, total.ratio,
              velocity.ratio, gcl.ratio, globalGcl.ratio, sweptIndependent.ratio);
}

TEST(MeshMotionGeometry, AffineMotionsGiveTheExactAffineImage2D) {
  runAffineCase({"C16/TR2", &m7::c16, &m7::tr2Spec});
  runAffineCase({"C16/EX2", &m7::c16, &m7::ex2Spec});
  runAffineCase({"C16/SH2", &m7::c16, &m7::sh2Spec});
}

TEST(MeshMotionGeometry, AffineMotionsGiveTheExactAffineImage3D) {
  runAffineCase({"H8/TR3", &m7::h8, &m7::tr3Spec});
  runAffineCase({"H8/EX3", &m7::h8, &m7::ex3Spec});
  runAffineCase({"H8/SH3", &m7::h8, &m7::sh3Spec});
}

// --- G2 (sinusoidal: validity, closure, orientation, volume) + G3.4 + G4 ---------------------

void runSinusoidalCase(const std::string& name, Mesh (*make)(), bool is3d) {
  Mesh mesh = make();
  const TopologySnapshot topology0 = topologyOf(mesh);
  Real totalReference = 0.0;
  for (const auto& cell : mesh.cells()) totalReference += cell.volume();
  const m7::Motion prescribed = is3d ? m7::sn3() : m7::sn2();
  MeshMotion motion(mesh, prescribed);
  const auto& topology = motion.topology();
  const auto* sinus = dynamic_cast<const cfd::mesh::SinusoidalMotion*>(&motion.motion());
  ASSERT_NE(sinus, nullptr);
  const Vector3 amplitude = is3d ? Vector3{m7::kAmplitude, 0.5 * m7::kAmplitude, -0.75 * m7::kAmplitude}
                                 : Vector3{m7::kAmplitude, m7::kAmplitude, 0.0};
  Worst closure;
  Worst total;
  Worst meanVelocity;
  Worst midVelocity;
  Worst gcl;
  Worst globalGcl;
  Worst sweptIndependent;
  Real minVolume = 1e300;
  Real maxRelativeChange = 0.0;
  Real maxDisplacement = 0.0;
  for (int n = 1; n <= kSteps; ++n) {
    const std::vector<Vector3> before = motion.currentVertices();
    const Real t0 = (n - 1) * kDt;
    const Real t1 = n * kDt;
    const MeshMotionStep& step = motion.advance(t1);
    ASSERT_TRUE(step.moved);
    const std::vector<Vector3>& after = motion.currentVertices();
    const Real x = step.maxCoordinate;
    maxDisplacement = std::max(maxDisplacement, step.maxDisplacement);
    // G2.1, G2.5
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      minVolume = std::min(minVolume, mesh.cell(c).volume());
      maxRelativeChange = std::max(
          maxRelativeChange, std::abs(mesh.cell(c).volume() - step.previousVolumes[c]) / step.previousVolumes[c]);
    }
    for (const auto& face : mesh.faces()) {
      const Vector3 d = face.neighbor().has_value()
                            ? mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid()
                            : face.centroid() - mesh.cell(face.owner()).centroid();
      EXPECT_GT(dot(face.areaVector(), d), 0.0) << name << " face " << face.id();
    }
    // G2.4
    std::vector<Vector3> sum(mesh.numberOfCells());
    for (const auto& face : mesh.faces()) {
      sum[face.owner()] += face.areaVector();
      if (face.neighbor().has_value()) sum[*face.neighbor()] -= face.areaVector();
    }
    Real boundSum = 0.0;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      const Real hc = is3d ? m7::hexLongestEdge(after, topology.cells[c].corner) : 0.0;
      closure.add(magnitude(sum[c]), is3d ? 64 * kEps * x * hc : 8 * kEps * x);
      const Real gclBound = is3d ? 1024 * kEps * x * hc * hc : 256 * kEps * x * x;
      gcl.add(std::abs(step.gclResiduals[c]), gclBound);
      boundSum += gclBound;
    }
    globalGcl.add(std::abs(step.globalGclResidual), boundSum);
    // G2.6: the boundary is fixed, so the total volume is constant (within the
    // sum of the per-cell volume bounds)
    Real totalNow = 0.0;
    Real volumeBoundSum = 0.0;
    for (Index c = 0; c < mesh.numberOfCells(); ++c) {
      totalNow += mesh.cell(c).volume();
      const Real hc = is3d ? m7::hexLongestEdge(after, topology.cells[c].corner) : 0.0;
      volumeBoundSum += is3d ? 64 * kEps * x * hc * hc : 16 * kEps * x * x;
    }
    total.add(std::abs(totalNow - totalReference), volumeBoundSum);
    // G3.4: v vs the exact mean velocity over the step and the midpoint velocity
    for (std::size_t v = 0; v < after.size(); ++v) {
      const Vector3& ref = motion.referenceVertices()[v];
      const Real shape = sinus->shape(ref);
      const Vector3 exactMean =
          amplitude * (shape * ((std::sin(m7::kOmega * t1) - std::sin(m7::kOmega * t0)) / step.dt));
      const Vector3 exactMid = amplitude * (shape * m7::kOmega * std::cos(m7::kOmega * (0.5 * (t0 + t1))));
      const Vector3 e1 = step.vertexVelocities[v] - exactMean;
      const Vector3 e2 = step.vertexVelocities[v] - exactMid;
      const Real round = 8 * kEps * x / step.dt;
      const Real taylor = (step.dt * step.dt / 24.0) * m7::kAmplitude * std::pow(m7::kOmega, 3);
      meanVelocity.add(std::max({std::abs(e1.x), std::abs(e1.y), std::abs(e1.z)}), round);
      midVelocity.add(std::max({std::abs(e2.x), std::abs(e2.y), std::abs(e2.z)}), taylor + round);
    }
    // G4.3
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      const Real independent = independentSwept(mesh, topology, f, before, after);
      const Real bound = is3d ? 1024 * kEps * x *
                                    std::pow(m7::faceLongestDiagonal(after, topology.faces[f].vertex), 2)
                              : 256 * kEps * x * x;
      sweptIndependent.add(std::abs(step.sweptVolumes[f] - independent), bound);
    }
  }
  EXPECT_GT(minVolume, 0.0);
  EXPECT_TRUE(topologyOf(mesh) == topology0) << name;
  EXPECT_LE(closure.ratio, 1.0) << name;
  EXPECT_LE(total.ratio, 1.0) << name;
  EXPECT_LE(meanVelocity.ratio, 1.0) << name;
  EXPECT_LE(midVelocity.ratio, 1.0) << name;
  EXPECT_LE(gcl.ratio, 1.0) << name;
  EXPECT_LE(globalGcl.ratio, 1.0) << name;
  EXPECT_LE(sweptIndependent.ratio, 1.0) << name;
  std::printf("G2/G3/G4 %s: %d advances, min V %.4e, max |dV|/V per step %.3e, max displacement "
              "%.4f; worst measured/bound: closure %.3e total %.3e velocity(mean) %.3e "
              "velocity(midpoint) %.3e GCL %.3e globalGCL %.3e swept-vs-independent %.3e\n",
              name.c_str(), kSteps, minVolume, maxRelativeChange, maxDisplacement, closure.ratio,
              total.ratio, meanVelocity.ratio, midVelocity.ratio, gcl.ratio, globalGcl.ratio,
              sweptIndependent.ratio);
}

TEST(MeshMotionGeometry, SinusoidalDeformation2D) {
  runSinusoidalCase("C16/SN2", &m7::c16, false);
  runSinusoidalCase("Q16/SN2", &m7::q16, false);
  runSinusoidalCase("G16/SN2", &m7::g16, false);
  runSinusoidalCase("MB2/SN2", &m7::mb2, false);
}

TEST(MeshMotionGeometry, SinusoidalDeformation3D) { runSinusoidalCase("H8/SN3", &m7::h8, true); }

TEST(MeshMotionGeometry, MultiBlockInterfaceStaysConformal) {
  Mesh mesh = m7::mb2();
  MeshMotion motion(mesh, m7::sn2());
  for (int n = 1; n <= 5; ++n) {
    (void)motion.advance(n * kDt);
    const auto& blocks = mesh.structuredBlocks();
    for (Index j = 0; j <= 16; ++j) {
      EXPECT_TRUE(sameBits(blocks[0].vertices[(j * 9) + 8], blocks[1].vertices[j * 9]));
    }
    // The interface vertices really moved (sin(pi 0.5) = 1).
    EXPECT_GT(std::abs(blocks[0].vertices[(8 * 9) + 8].x - 0.5), 1e-3);
  }
}

// --- G2.8 --------------------------------------------------------------------------------

// Advances until the motion throws; returns the message (empty if it never
// does) and checks that the failed advance changed nothing.
std::string advanceUntilRejected(Mesh& mesh, const m7::Motion& prescribed, int maxSteps) {
  MeshMotion motion(mesh, prescribed);
  for (int n = 1; n <= maxSteps; ++n) {
    const MeshGeometryState geometry = mesh.geometry();
    const std::vector<Vector3> vertices = motion.currentVertices();
    const Real time = motion.time();
    try {
      (void)motion.advance(n * kDt);
    } catch (const InvalidArgumentError& error) {
      EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), geometry), -1);
      EXPECT_EQ(motion.currentVertices().size(), vertices.size());
      for (std::size_t v = 0; v < vertices.size(); ++v) {
        EXPECT_TRUE(sameBits(motion.currentVertices()[v], vertices[v]));
      }
      EXPECT_TRUE(sameBits(motion.time(), time));
      std::printf("G2.8 rejected at step %d: %s\n", n, error.what());
      return error.what();
    }
  }
  return {};
}

TEST(MeshMotionInvalid, FoldingSinusoidal2DIsRejectedNamingTheCell) {
  Mesh mesh = m7::c16();
  const std::string message = advanceUntilRejected(mesh, m7::sn2(0.4), kSteps);
  EXPECT_NE(message.find("cell ("), std::string::npos) << message;
  EXPECT_NE(message.find("quadrilateral"), std::string::npos) << message;
}

TEST(MeshMotionInvalid, FoldingSinusoidal3DIsRejectedNamingTheCellAndCorner) {
  Mesh mesh = m7::h8();
  const std::string message = advanceUntilRejected(mesh, m7::sn3(0.4), kSteps);
  EXPECT_NE(message.find("cell ("), std::string::npos) << message;
  EXPECT_NE(message.find("corner ("), std::string::npos) << message;
}

TEST(MeshMotionInvalid, CollapsingCellsAreRejectedNamingTheCell) {
  Mesh mesh = m7::c16();
  const std::string message = advanceUntilRejected(mesh, m7::affine(m7::collapseSpec()), kSteps);
  EXPECT_NE(message.find("cell ("), std::string::npos) << message;
  EXPECT_TRUE(message.find("inverted") != std::string::npos ||
              message.find("zero area") != std::string::npos)
      << message;
}

TEST(MeshMotionInvalid, OutOfPlaneMotionOfA2DMeshIsRejected) {
  Mesh mesh = m7::c16();
  m7::AffineSpec up{m7::zeroMatrix(), {}, {0.0, 0.0, 0.1}};
  const std::string message = advanceUntilRejected(mesh, m7::affine(up), 2);
  EXPECT_NE(message.find("xy-plane"), std::string::npos) << message;
}

TEST(MeshMotionInvalid, AdvanceMustMoveForwardInTime) {
  Mesh mesh = m7::c16();
  MeshMotion motion(mesh, m7::affine(m7::tr2Spec()));
  (void)motion.advance(0.1);
  EXPECT_THROW((void)motion.advance(0.1), InvalidArgumentError);
  EXPECT_THROW((void)motion.advance(0.05), InvalidArgumentError);
}

TEST(MeshMotionInvalid, MotionMustStartAtTheReferencePosition) {
  Mesh mesh = m7::c16();
  // A motion whose displacement at the start time is not zero.
  class Offset final : public cfd::mesh::PrescribedMotion {
   public:
    Vector3 position(const Vector3& x, Real) const override { return x + Vector3{1e-3, 0.0, 0.0}; }
    std::string description() const override { return "offset"; }
  };
  EXPECT_NO_THROW(MeshMotion(mesh, m7::sn2()));
  EXPECT_THROW(MeshMotion(mesh, std::make_shared<Offset>()), InvalidArgumentError);
  EXPECT_THROW(MeshMotion(mesh, nullptr), InvalidArgumentError);
}

// --- Mesh::setGeometry / revert ---------------------------------------------------------------

TEST(MeshGeometryUpdate, RejectsInvalidGeometryAndChangesNothing) {
  Mesh mesh = m7::c16();
  const MeshGeometryState original = mesh.geometry();
  MeshGeometryState bad = original;
  bad.cellVolumes[37] = -1.0;
  try {
    mesh.setGeometry(bad);
    FAIL() << "expected InvalidArgumentError";
  } catch (const InvalidArgumentError& error) {
    EXPECT_NE(std::string(error.what()).find("cell 37"), std::string::npos) << error.what();
  }
  EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), original), -1);
  MeshGeometryState outOfPlane = original;
  outOfPlane.faceCentroids[3].z = 1e-3;
  EXPECT_THROW(mesh.setGeometry(outOfPlane), InvalidArgumentError);
  MeshGeometryState shortState = original;
  shortState.faceAreaVectors.pop_back();
  EXPECT_THROW(mesh.setGeometry(shortState), InvalidArgumentError);
  MeshGeometryState nonFinite = original;
  nonFinite.cellCentroids[0].x = std::nan("");
  EXPECT_THROW(mesh.setGeometry(nonFinite), InvalidArgumentError);
  EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), original), -1);
}

TEST(MeshGeometryUpdate, RevertRestoresTheGeometryBitwiseAndIsIdempotent) {
  Mesh mesh = m7::q16();
  MeshMotion motion(mesh, m7::sn2());
  (void)motion.advance(0.02);
  const MeshGeometryState afterFirst = mesh.geometry();
  const std::vector<Vector3> verticesAfterFirst = motion.currentVertices();
  (void)motion.advance(0.04);
  EXPECT_TRUE(motion.canRevert());
  motion.revert();
  EXPECT_FALSE(motion.canRevert());
  EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), afterFirst), -1);
  EXPECT_TRUE(sameBits(motion.time(), 0.02));
  for (std::size_t v = 0; v < verticesAfterFirst.size(); ++v) {
    EXPECT_TRUE(sameBits(motion.currentVertices()[v], verticesAfterFirst[v]));
  }
  motion.revert();  // idempotent
  EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), afterFirst), -1);
  // Advancing again after the revert reproduces the reverted step exactly.
  (void)motion.advance(0.04);
  Mesh fresh = m7::q16();
  MeshMotion replay(fresh, m7::sn2());
  (void)replay.advance(0.02);
  (void)replay.advance(0.04);
  EXPECT_EQ(firstBitwiseDifference(mesh.geometry(), fresh.geometry()), -1);
}

// --- G2.3 data for the independent Python check (explicit run only) ---------------------------

void dumpVector(std::ofstream& out, const char* key, const std::vector<Vector3>& v) {
  out << "\"" << key << "\": [";
  for (std::size_t i = 0; i < v.size(); ++i) {
    out << (i ? "," : "") << "[" << v[i].x << "," << v[i].y << "," << v[i].z << "]";
  }
  out << "]";
}

void dumpScalar(std::ofstream& out, const char* key, const std::vector<Real>& v) {
  out << "\"" << key << "\": [";
  for (std::size_t i = 0; i < v.size(); ++i) out << (i ? "," : "") << v[i];
  out << "]";
}

TEST(MeshMotionEvidence, DISABLED_DumpSinusoidalGeometryForIndependentCheck) {
  const char* dir = std::getenv("M7_DATA_DIR");
  ASSERT_NE(dir, nullptr) << "set M7_DATA_DIR";
  const std::vector<std::tuple<std::string, Mesh (*)(), bool>> cases = {
      {"C16_SN2", &m7::c16, false}, {"Q16_SN2", &m7::q16, false}, {"G16_SN2", &m7::g16, false},
      {"MB2_SN2", &m7::mb2, false}, {"H8_SN3", &m7::h8, true}};
  for (const auto& [name, make, is3d] : cases) {
    Mesh mesh = make();
    MeshMotion motion(mesh, is3d ? m7::sn3() : m7::sn2());
    const auto& topology = motion.topology();
    std::ofstream out(std::string(dir) + "/geometry_" + name + ".json");
    out.precision(17);
    out << "{\"case\": \"" << name << "\", \"dimension\": " << topology.dimension << ", \"cells\": [";
    for (std::size_t c = 0; c < topology.cells.size(); ++c) {
      out << (c ? "," : "") << "[";
      for (int k = 0; k < (is3d ? 8 : 4); ++k) out << (k ? "," : "") << topology.cells[c].corner[static_cast<std::size_t>(k)];
      out << "]";
    }
    out << "], \"faces\": [";
    for (std::size_t f = 0; f < topology.faces.size(); ++f) {
      out << (f ? "," : "") << "[";
      for (int k = 0; k < (is3d ? 4 : 2); ++k) out << (k ? "," : "") << topology.faces[f].vertex[static_cast<std::size_t>(k)];
      out << "]";
    }
    out << "], \"owner\": [";
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) out << (f ? "," : "") << mesh.face(f).owner();
    out << "], \"neighbor\": [";
    for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
      out << (f ? "," : "") << (mesh.face(f).neighbor().has_value() ? static_cast<long>(*mesh.face(f).neighbor()) : -1L);
    }
    out << "], \"steps\": [";
    std::vector<Vector3> previous = motion.currentVertices();
    for (int n = 1; n <= kSteps; ++n) {
      const MeshMotionStep& step = motion.advance(n * kDt);
      const MeshGeometryState g = mesh.geometry();
      out << (n > 1 ? "," : "") << "{";
      dumpVector(out, "from", previous);
      out << ",";
      dumpVector(out, "vertices", motion.currentVertices());
      out << ",";
      dumpScalar(out, "volume", g.cellVolumes);
      out << ",";
      dumpVector(out, "centroid", g.cellCentroids);
      out << ",";
      dumpVector(out, "faceCentroid", g.faceCentroids);
      out << ",";
      dumpVector(out, "areaVector", g.faceAreaVectors);
      out << ",";
      dumpScalar(out, "swept", step.sweptVolumes);
      out << "}";
      previous = motion.currentVertices();
    }
    out << "]}\n";
    std::printf("dumped %s\n", name.c_str());
  }
}

}  // namespace
