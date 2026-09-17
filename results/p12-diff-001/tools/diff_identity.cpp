// P12-DIFF-001 step 2, decisive check: is the proposed correction a no-op in this codebase?
//
// Claim 1 (algebra): the production coefficient already IS the normal-distance coefficient.
//   S_orth = (S.S)/(d.S) d   =>   |S_orth| / |d| = |S|^2 / (d.S)
//   and d.S = d.(|S| n) = |S| d_n   =>   |S|^2 / (d.S) = |S| / d_n.
// So passing |d| as `distance` already yields Gamma |S| / d_n. Verified numerically below.
//
// Claim 2 (boundary-condition model): CFDApp's value-prescribing conditions (FixedValue, Wall,
// MovingWall, Inlet, FixedTemperature, WallOmega) are constant per patch. If phi_b is the same
// everywhere on a flat patch, then its value at the normal foot equals its value at the face
// centroid, so the tangential transfer phi_b +/- grad(phi)_P . d_t is exactly zero-effect for the
// prescribed data -- there is no tangential variation of the PRESCRIBED value to transfer.
// Verified below by comparing the production terms against the explicit normal-distance form.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;

namespace {

const Real kPi = std::acos(-1.0);
constexpr Real kGamma = 0.1;

std::vector<Vector2> vertices(Index nx, Index ny, Real distort) {
  const Real L = 8.0, H = 1.0, lambda = 1.0;
  const Real ax = 0.1 * distort, ay = 0.05 * distort;
  std::vector<Vector2> v;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = L * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = H * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (ax * std::sin(kPi * xi / L) * std::sin(2.0 * kPi * eta / H));
      Real y = eta + (ay * std::sin(2.0 * kPi * xi / lambda) * std::sin(kPi * eta / H));
      if (i == 0) x = 0.0;
      if (i == nx) x = L;
      if (j == 0) y = 0.0;
      if (j == ny) y = H;
      v.push_back(Vector2{x, y});
    }
  }
  return v;
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-001: is the proposed Dirichlet tangential correction a no-op?\n");
  for (const Real distort : {0.0, 0.5, 1.0}) {
    const Index nx = 144, ny = 18;
    const mesh::Mesh m =
        mesh::MeshGeometry::createStructuredQuad2D(nx, ny, vertices(nx, ny, distort));
    fields::ScalarField phi(m.numberOfCells());
    for (const auto& c : m.cells()) {
      phi[c.id()] = 6.0 * c.centroid().y * (1.0 - c.centroid().y);
    }
    boundary::BoundaryConditionSet bc;
    bc.set(m, "bottom", std::make_unique<boundary::FixedValue>(0.0));
    bc.set(m, "top", std::make_unique<boundary::FixedValue>(0.0));
    bc.set(m, "left", std::make_unique<boundary::FixedGradient>(0.0));
    bc.set(m, "right", std::make_unique<boundary::FixedGradient>(0.0));
    const auto grad =
        discretization::gradient(m, phi, bc, discretization::GradientScheme::GreenGauss);

    Real worstCoefficient = 0.0;   // |production coefficient - Gamma |S| / d_n| (claim 1)
    Real worstTangential = 0.0;    // |grad(phi)_P . d_t| at the wall, absolute
    Real worstTangentialRel = 0.0; // the same relative to |phi_b - phi_P|
    Real worstExplicit = 0.0;      // |explicitFlux| relative to the implicit part
    Index faces = 0;
    for (const char* wall : {"bottom", "top"}) {
      for (const Index faceId : m.boundaryPatch(wall).faceIds()) {
        const auto& face = m.face(faceId);
        const auto& owner = m.cell(face.owner());
        const Vector3 d = face.centroid() - owner.centroid();
        const Vector3 n = face.areaVector() * (1.0 / face.area());
        const Real dn = dot(d, n);
        const Vector3 dt = d - (n * dn);
        const auto terms = discretization::boundaryFaceDiffusionTerms(m, face, kGamma,
                                                                      magnitude(d), &grad, true);
        const Real normalForm = kGamma * face.area() / dn;
        worstCoefficient = std::max(worstCoefficient,
                                    std::abs(terms.coefficient - normalForm) / normalForm);
        const Real transfer = dot(grad[owner.id()], dt);
        const Real jump = std::abs(0.0 - phi[owner.id()]);
        worstTangential = std::max(worstTangential, std::abs(transfer));
        if (jump > 0.0) {
          worstTangentialRel = std::max(worstTangentialRel, std::abs(transfer) / jump);
        }
        const Real implicit = std::abs(terms.coefficient * (0.0 - phi[owner.id()]));
        if (implicit > 0.0) {
          worstExplicit = std::max(worstExplicit, std::abs(terms.explicitFlux) / implicit);
        }
        ++faces;
      }
    }
    std::printf("I   distortion %.2f | wall faces %zu | claim 1: |production coeff - Gamma|S|/d_n| "
                "/ (Gamma|S|/d_n) = %.3e | claim 2: max |grad.d_t| = %.3e (%.3e of the wall jump) | "
                "explicit/implicit = %.3e\n",
                distort, static_cast<std::size_t>(faces), worstCoefficient, worstTangential,
                worstTangentialRel, worstExplicit);
  }
  return 0;
}
