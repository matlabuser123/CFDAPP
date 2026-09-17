// P12-DIFF-002 ThermalInterface fix, criterion T1: the SINGLE-MATERIAL thermal diffusion assembly
// must be BITWISE unchanged by the shared-helper refactor.
//
// This probe dumps every stored matrix entry and every RHS value of
// `assembleThermalDiffusionContribution` in %a hex float -- an exact, round-trip-free
// representation, so a byte-for-byte diff of two runs is a bitwise comparison of the assembled
// system. It is built twice: once against the authoritative (post-fix) library and once against an
// isolated baseline tree holding the PRE-FIX EnergyEquation.{hpp,cpp}, whose sha256 are verified
// equal to the hashes frozen in logs/00_freeze.log before any production edit.
//
// It deliberately uses ONLY the API that exists in both versions (no reference to the new helpers),
// so the same source compiles against either library.
//
// Coverage: both overloads (constant k, and the per-cell k field -- both had their boundary block
// replaced by the shared call), both non-orthogonal settings (the refactor also rewrote the
// gradTPtr/internalGradT lines, so the N>0 path must be checked too), all-Dirichlet and mixed
// Dirichlet/HeatFlux/Adiabatic patches (gradient-type faces must keep their exact prescribed flux),
// on 2D Cartesian, 3D Cartesian, graded, distorted and one-cell-thick meshes.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/HeatFlux.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kConductivity = 2.5;

boundary::BoundaryConditionSet allDirichlet(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  Real v = 300.0;
  for (const auto& p : mesh.boundaryPatches()) {
    b.set(mesh, p.name(), std::make_unique<boundary::FixedTemperature>(v));
    v += 20.0;
  }
  return b;
}

// Gradient-type patches mixed in, so a change to their (unchanged) treatment would show up too.
boundary::BoundaryConditionSet mixed(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    switch (i % 3) {
      case 0:
        b.set(mesh, p.name(),
              std::make_unique<boundary::FixedTemperature>(300.0 + 15.0 * static_cast<Real>(i)));
        break;
      case 1:
        b.set(mesh, p.name(), std::make_unique<boundary::HeatFlux>(1250.0, kConductivity));
        break;
      default: b.set(mesh, p.name(), std::make_unique<boundary::Adiabatic>()); break;
    }
    ++i;
  }
  return b;
}

// A non-uniform field, so the assembled RHS depends on the actual values (a uniform field could
// hide a coefficient error through cancellation).
fields::ScalarField field(const Mesh& mesh) {
  fields::ScalarField t(mesh.numberOfCells(), 0.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const auto c = mesh.cell(i).centroid();
    t[i] = 305.0 + 3.0 * c.x - 1.5 * c.y + 0.75 * c.z + 0.5 * c.x * c.y;
  }
  return t;
}

void dumpSystem(const algebra::SparseMatrix& m, const algebra::Vector& rhs) {
  const Index* off = m.rowOffsetsData();
  for (Index r = 0; r < m.rows(); ++r) {
    for (Index k = off[r]; k < off[r + 1]; ++k) {
      std::printf("  A %6lld %6lld %a\n", static_cast<long long>(r),
                  static_cast<long long>(m.columnIndicesData()[k]), m.valuesData()[k]);
    }
    std::printf("  b %6lld %a\n", static_cast<long long>(r), rhs[r]);
  }
}

void run(const std::string& label, const Mesh& mesh) {
  const auto t = field(mesh);
  for (const bool useMixed : {false, true}) {
    const auto bcs = useMixed ? mixed(mesh) : allDirichlet(mesh);
    for (const bool enabled : {false, true}) {
      discretization::NonOrthogonalCorrectionOptions opts;
      opts.enabled = enabled;

      // constant-conductivity overload
      {
        algebra::SparseMatrixBuilder bld(mesh.numberOfCells(), mesh.numberOfCells());
        algebra::Vector rhs(mesh.numberOfCells(), 0.0);
        thermal::assembleThermalDiffusionContribution(mesh, kConductivity, t, bcs, bld, rhs, opts);
        const auto m = bld.build();
        std::printf("== %s | bcs=%s | N=%d | overload=const-k | cells=%lld nnz=%lld\n",
                    label.c_str(), useMixed ? "mixed" : "dirichlet", enabled ? 2 : 0,
                    static_cast<long long>(mesh.numberOfCells()),
                    static_cast<long long>(m.nonZeros()));
        dumpSystem(m, rhs);
      }

      // per-cell conductivity field overload, deliberately NON-uniform
      {
        fields::ScalarField kf(mesh.numberOfCells(), kConductivity);
        for (Index i = 0; i < mesh.numberOfCells(); ++i) {
          kf[i] = kConductivity * (1.0 + 0.3 * static_cast<Real>(i % 7));
        }
        algebra::SparseMatrixBuilder bld(mesh.numberOfCells(), mesh.numberOfCells());
        algebra::Vector rhs(mesh.numberOfCells(), 0.0);
        thermal::assembleThermalDiffusionContribution(mesh, kf, t, bcs, bld, rhs, opts);
        const auto m = bld.build();
        std::printf("== %s | bcs=%s | N=%d | overload=k-field | cells=%lld nnz=%lld\n",
                    label.c_str(), useMixed ? "mixed" : "dirichlet", enabled ? 2 : 0,
                    static_cast<long long>(mesh.numberOfCells()),
                    static_cast<long long>(m.nonZeros()));
        dumpSystem(m, rhs);
      }
    }
  }
}

Mesh gradedMesh() {
  return MeshGeometry::createGraded2D(
      12, 12, 1.0, 1.0,
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both},
      mesh::AxisGrading{mesh::GradingType::Geometric, 1.2, mesh::GradingCluster::Both});
}

Mesh distortedMesh() {
  constexpr Index n = 12;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      const bool interior = (i > 0 && i < n && j > 0 && j < n);
      vertices.push_back(Vector3{xi + (interior ? 0.45 * eta / static_cast<Real>(n) : 0.0),
                                 eta + (interior ? 0.45 * xi / static_cast<Real>(n) : 0.0), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

}  // namespace

int main() {
  std::printf("# T1: exact (%%a hex) dump of the SINGLE-MATERIAL thermal diffusion assembly.\n");
  std::printf("# Identical output from the pre-fix and post-fix libraries == bitwise unchanged.\n");
  run("Cartesian 2x1", MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0));
  run("Cartesian 10x10", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0));
  run("Cartesian 20x20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0));
  run("Cartesian 3D 8x8x8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  run("graded 12x12 r=1.2", gradedMesh());
  run("distorted 12x12 s=0.45", distortedMesh());
  run("Cartesian 8x1", MeshGeometry::createCartesian2D(8, 1, 8.0, 1.0));
  run("Cartesian 3D 8x8x1", MeshGeometry::createCartesian3D(8, 8, 1, 8.0, 8.0, 1.0));
  return 0;
}
