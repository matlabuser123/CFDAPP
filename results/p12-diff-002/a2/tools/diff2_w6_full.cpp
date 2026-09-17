// P12-DIFF-002 A2-6: the COMPLETE W6 dataset the original W6 asked for and the previous run
// recorded only partially -- dp/dx error, velocity L1 / L2 / Linf, wall flux, observed orders and
// iterations, at 64x8 / 96x12 / 144x18 on the production distorted-Poiseuille family.
//
// The case, the mesh mapping, the developed-region window and the exact solution are transcribed
// from tests/integration/case/test_structured_quad_production_case.cpp (kLength 8, kHeight 1,
// kMeanVelocity 1, kViscosity 0.1, developed region 0.50 L .. 0.85 L, mappedVertices(.., 0.1, 0.05,
// 1.0)), so the numbers are directly comparable with that test's own output. The case is run
// through ProjectRunner exactly as the test does, with the committed case's own solver settings.
//
// Wall flux: the viscous wall shear mu * du/dn at every wall face, reconstructed from the CONVERGED
// velocity through the production boundaryFaceDiffusionTerms (so it measures the shipped wall
// discretization), compared with the exact 6 mu U / H. Reported as L1 and Linf over the developed
// region's wall faces.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using namespace cfd;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kLength = 8.0;
constexpr Real kHeight = 1.0;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kDevelopedStart = 0.50 * kLength;
constexpr Real kDevelopedEnd = 0.85 * kLength;
const Real kPi = std::acos(-1.0);
const Real kExactGradient = -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight);

std::vector<Vector3> mappedVertices(Index nx, Index ny) {
  std::vector<Vector3> vertices;
  vertices.reserve((nx + 1) * (ny + 1));
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real xi = kLength * static_cast<Real>(i) / static_cast<Real>(nx);
      const Real eta = kHeight * static_cast<Real>(j) / static_cast<Real>(ny);
      Real x = xi + (0.1 * std::sin(kPi * xi / kLength) * std::sin(2.0 * kPi * eta / kHeight));
      Real y = eta + (0.05 * std::sin(2.0 * kPi * xi / 1.0) * std::sin(kPi * eta / kHeight));
      if (i == 0) x = 0.0;
      if (i == nx) x = kLength;
      if (j == 0) y = 0.0;
      if (j == ny) y = kHeight;
      vertices.push_back(Vector3{x, y, 0.0});
    }
  }
  return vertices;
}

Real exactU(Real y) { return 6.0 * kMeanVelocity * (y / kHeight) * (1.0 - (y / kHeight)); }

struct Row {
  Index nx{};
  Index ny{};
  std::size_t iterations{};
  Real dpdx{};
  Real dpdxError{};
  Real l1{};
  Real l2{};
  Real linf{};
  Real wallFluxL1{};
  Real wallFluxLinf{};
  Real massImbalance{};
  bool ok{false};
};

Row run(Index nx, Index ny, const std::string& scratch) {
  Row row;
  row.nx = nx;
  row.ny = ny;
  io::CaseDefinition definition = io::CaseReader{}.read("cases/poiseuille_distorted");
  definition.mesh.type = "structured_quad";
  definition.mesh.nx = nx;
  definition.mesh.ny = ny;
  definition.mesh.vertices = mappedVertices(nx, ny);
  std::filesystem::create_directories(scratch);
  io::CaseWriter::write(scratch, definition);

  const app::ProjectRunResult result = app::ProjectRunner::run(scratch);
  if (!result.mesh.has_value() || !result.simpleResult.has_value()) {
    std::printf("  %zux%zu FAILED TO RUN: %s\n", static_cast<std::size_t>(nx),
                static_cast<std::size_t>(ny), result.errorMessage.c_str());
    return row;
  }
  const Mesh& mesh = *result.mesh;
  const auto& r = *result.simpleResult;
  row.iterations = static_cast<std::size_t>(r.iterations);
  row.massImbalance = r.globalMassImbalance;

  Real absSum = 0.0;
  Real sqSum = 0.0;
  Real volume = 0.0;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector3& c = cell.centroid();
    if (c.x < kDevelopedStart || c.x > kDevelopedEnd) continue;
    const Vector3 error = r.velocity[cell.id()] - Vector3{exactU(c.y), 0.0, 0.0};
    const Real e = magnitude(error);
    const Real v = cell.volume();
    absSum += e * v;
    sqSum += e * e * v;
    volume += v;
    row.linf = std::max(row.linf, e);
    const Real p = r.pressure[cell.id()];
    sw += v;
    sx += v * c.x;
    sxx += v * c.x * c.x;
    sp += v * p;
    sxp += v * c.x * p;
  }
  row.l1 = absSum / volume;
  row.l2 = std::sqrt(sqSum / volume);
  row.dpdx = ((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  row.dpdxError = std::abs(row.dpdx - kExactGradient) / std::abs(kExactGradient);

  // Wall shear from the shipped wall discretization, against the exact 6 mu U / H.
  const io::SimulationSetup setup = io::CaseBuilder{}.build(*result.caseDefinition);
  fields::ScalarField u(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) u[cell.id()] = r.velocity[cell.id()].x;
  // Velocity boundary conditions are vector-valued, so the U-component gradient comes from the
  // production velocity-gradient path (exactly what MomentumEquation's assembler uses), not the
  // scalar gradient().
  const auto velocityGradient = discretization::computeVelocityGradient(
      mesh, r.velocity, setup.velocityBoundaries, discretization::GradientScheme::GreenGauss);
  const fields::VectorField& gradU = velocityGradient.gradU;

  const Real exactShear = 6.0 * kViscosity * kMeanVelocity / kHeight;
  Real wallAbs = 0.0;
  Real wallArea = 0.0;
  for (const char* wall : {"bottom", "top"}) {
    for (const Index faceId : mesh.boundaryPatch(wall).faceIds()) {
      const Face& face = mesh.face(faceId);
      if (face.centroid().x < kDevelopedStart || face.centroid().x > kDevelopedEnd) continue;
      const Real distance =
          MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
      const auto terms = discretization::boundaryFaceDiffusionTerms(mesh, face, kViscosity,
                                                                    distance, &gradU, true);
      // flux into the owner = -(cP u_P - cF u_F) + cB u_b + explicit  (architecture.md section 4)
      const Real flux =
          -((terms.coefficient * u[face.owner()]) - (terms.farCellCoefficient * u[terms.farCell])) +
          (terms.boundaryValueCoefficient * 0.0) + terms.explicitFlux;
      const Real shear = std::abs(flux) / face.area();
      const Real e = std::abs(shear - exactShear) / exactShear;
      wallAbs += e * face.area();
      wallArea += face.area();
      row.wallFluxLinf = std::max(row.wallFluxLinf, e);
    }
  }
  row.wallFluxL1 = (wallArea > 0.0) ? (wallAbs / wallArea) : 0.0;
  row.ok = true;
  return row;
}

Real order(Real coarse, Real fine, Real ratio) {
  return (coarse > 0.0 && fine > 0.0) ? (std::log(coarse / fine) / std::log(ratio)) : 0.0;
}

}  // namespace

int main() {
  std::printf(
      "# P12-DIFF-002 A2-6: complete W6 dataset. Exact dp/dx %.6f; exact wall shear %.6f.\n",
      kExactGradient, 6.0 * kViscosity * kMeanVelocity / kHeight);
  std::printf(
      "# Frozen W6 bound: the 144x18 dp/dx relative error <= 0.747 %% (1.10 x 0.6788 %%).\n\n");

  std::vector<Row> rows;
  const std::vector<std::pair<Index, Index>> grids{{64, 8}, {96, 12}, {144, 18}};
  for (const auto& [nx, ny] : grids) {
    rows.push_back(run(nx, ny, std::string("/tmp/diff2_w6_") + std::to_string(nx)));
  }

  std::printf("%-9s %9s %12s %11s %12s %12s %12s %12s %12s %11s\n", "grid", "iters", "dp/dx",
              "dpdx err %", "vel L1", "vel L2", "vel Linf", "wall L1 %", "wall Linf %", "mass imb");
  for (const Row& r : rows) {
    if (!r.ok) continue;
    std::printf("%3zux%-5zu %9zu %12.6f %11.4f %12.4e %12.4e %12.4e %12.4f %12.4f %11.3e\n",
                static_cast<std::size_t>(r.nx), static_cast<std::size_t>(r.ny), r.iterations,
                r.dpdx, 100.0 * r.dpdxError, r.l1, r.l2, r.linf, 100.0 * r.wallFluxL1,
                100.0 * r.wallFluxLinf, r.massImbalance);
  }

  std::printf("\nobserved orders (refinement ratio 1.5):\n");
  for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
    if (!rows[k].ok || !rows[k + 1].ok) continue;
    std::printf("  pair %zu: dp/dx %.3f  velocity L1 %.3f  L2 %.3f  Linf %.3f  wall flux %.3f\n", k,
                order(rows[k].dpdxError, rows[k + 1].dpdxError, 1.5),
                order(rows[k].l1, rows[k + 1].l1, 1.5), order(rows[k].l2, rows[k + 1].l2, 1.5),
                order(rows[k].linf, rows[k + 1].linf, 1.5),
                order(rows[k].wallFluxL1, rows[k + 1].wallFluxL1, 1.5));
  }

  if (!rows.empty() && rows.back().ok) {
    const Real e = 100.0 * rows.back().dpdxError;
    std::printf("\nW6 gate: 144x18 dp/dx error %.4f %% vs bound 0.7470 %% -> %s\n", e,
                (e <= 0.747) ? "PASS" : "FAIL");
  }
  return 0;
}
