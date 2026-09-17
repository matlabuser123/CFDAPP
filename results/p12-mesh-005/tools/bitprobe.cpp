// P12-MESH-005 2D bit-identity probe: for every committed case mesh (and two programmatic
// meshes), hash the exact bits of every 2D quantity the MESH-005 generalisation touches.
// Built against the pre-MESH-005 library and the new one; outputs must be identical.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/validation/ErrorNorms.hpp"

using namespace cfd;

namespace {

struct Hash {
  std::uint64_t h = 14695981039346656037ULL;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ULL;
    }
  }
  void real(Real v) { bytes(&v, sizeof v); }
  void vec(const Vector2& v) {
    real(v.x);
    real(v.y);
  }
};

Real phi(const Vector2& p) { return std::sin(1.3 * p.x) * std::cos(0.7 * p.y) + 0.4 * p.x * p.y; }

void probe(const std::string& name, const mesh::Mesh& m) {
  const Index n = m.numberOfCells();
  fields::ScalarField f(n);
  for (const auto& c : m.cells()) f[c.id()] = phi(c.centroid());

  boundary::BoundaryConditionSet dirichlet;
  boundary::BoundaryConditionSet neumann;
  for (const auto& p : m.boundaryPatches()) {
    dirichlet.set(m, p.name(), std::make_unique<boundary::FixedValue>(0.3 + 0.1 * p.name().size()));
    neumann.set(m, p.name(), std::make_unique<boundary::FixedGradient>(0.05 * p.name().size()));
  }
  fields::SurfaceField flux(m.numberOfFaces());
  for (const auto& face : m.faces()) flux[face.id()] = dot(Vector2{1.0, 0.45}, face.areaVector());

  Hash geo;
  for (const auto& face : m.faces()) {
    geo.vec(mesh::MeshGeometry::unitNormal(face));
    if (face.isBoundary()) {
      const auto d = mesh::MeshGeometry::decomposeBoundaryFaceArea(m, face);
      geo.vec(d.orthogonal);
      geo.vec(d.nonOrthogonal);
      continue;
    }
    const auto d = mesh::MeshGeometry::decomposeFaceArea(m, face);
    geo.vec(d.orthogonal);
    geo.vec(d.nonOrthogonal);
    geo.real(mesh::MeshGeometry::nonOrthogonalityAngleDegrees(m, face));
    if (const auto c = mesh::MeshGeometry::ownerNeighborCrossing(m, face)) {
      geo.real(c->t);
      geo.vec(c->crossingPoint);
      geo.vec(c->skewVector);
    }
    if (const auto s = mesh::MeshGeometry::skewness(m, face)) geo.real(*s);
  }

  Hash ops;
  for (const auto* bcs : {&dirichlet, &neumann}) {
    for (const auto scheme :
         {discretization::GradientScheme::GreenGauss, discretization::GradientScheme::LeastSquares}) {
      const auto g = discretization::gradient(m, f, *bcs, scheme);
      for (Index i = 0; i < n; ++i) ops.vec(g[i]);
      const auto dc = discretization::diffusion(m, f, 1.7, *bcs, true, scheme);
      for (Index i = 0; i < n; ++i) ops.real(dc[i]);
    }
    const auto d0 = discretization::diffusion(m, f, 1.7, *bcs);
    for (Index i = 0; i < n; ++i) ops.real(d0[i]);
    const auto s = discretization::interpolate(m, f, *bcs);
    for (Index i = 0; i < s.size(); ++i) ops.real(s[i]);
    for (const auto sc : {discretization::ConvectionScheme::Upwind, discretization::ConvectionScheme::Central,
                          discretization::ConvectionScheme::LinearUpwind, discretization::ConvectionScheme::QUICK}) {
      const auto c = discretization::convection(m, f, flux, *bcs, sc);
      for (Index i = 0; i < n; ++i) ops.real(c[i]);
    }
    discretization::NonOrthogonalCorrectionOptions corr;
    corr.enabled = true;
    const auto a = thermal::assembleEnergyEquation(m, f, flux, thermal::ThermalProperties(0.8, 1.2), *bcs, 2.5, corr);
    const auto& A = a.system.matrix();
    for (Index k = 0; k < A.nonZeros(); ++k) ops.real(A.valuesData()[k]);
    for (Index i = 0; i < n; ++i) ops.real(a.system.rhs()[i]);
  }
  fields::VectorField vf(n);
  fields::VectorField vx(n);
  for (Index i = 0; i < n; ++i) {
    vf[i] = Vector2{f[i], -0.5 * f[i]};
    vx[i] = Vector2{f[i] * 0.9, 0.1};
  }
  for (const auto& face : m.faces()) {
    if (face.isBoundary()) continue;
    ops.vec(discretization::interpolateInternalFace(m, face, vf));
    ops.real(discretization::interpolateInternalFaceSkewCorrected(m, face, f, vx));
    ops.vec(discretization::interpolateInternalFaceSkewCorrected(m, face, vf, vx, vx));
  }
  const auto norms = validation::computeVectorErrorNorms(m, vf, vx);
  for (const auto* e : {&norms.x, &norms.y, &norms.magnitude}) {
    ops.real(e->l1);
    ops.real(e->l2);
    ops.real(e->linf);
  }
  const auto q = mesh::MeshQuality::evaluate(m);
  std::printf("%-40s cells %6llu fingerprint %s geometry %016llx operators %016llx\n    quality: %s\n",
              name.c_str(), static_cast<unsigned long long>(n), mesh::computeMeshFingerprint(m).c_str(),
              static_cast<unsigned long long>(geo.h), static_cast<unsigned long long>(ops.h),
              q.summaryLine().c_str());
  for (const auto& issue : q.issues) std::printf("    %s\n", mesh::formatMeshQualityIssue(issue).c_str());
}

}  // namespace

int main(int argc, char** argv) {
  probe("cartesian 4x3 (1 x 1)", mesh::MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0));
  probe("cartesian 13x7 (2.1 x 0.9)", mesh::MeshGeometry::createCartesian2D(13, 7, 2.1, 0.9));
  for (int a = 1; a < argc; ++a) {
    const std::string dir = argv[a];
    const auto def = io::CaseReader().read(dir);
    const auto setup = io::CaseBuilder().build(def);
    probe(dir.substr(dir.find_last_of('/') + 1), setup.mesh);
  }
  return 0;
}
