// P12-MESH-006 -- the 3D incompressible solver through the production path (case directory ->
// CaseReader -> CaseBuilder -> ProjectRunner -> export), against gates G5, G6, G7 and G9.3 of
// results/p12-mesh-006/acceptance_gate.md (fixed before these tests ran; Amendment A2 fixes the
// measurement definitions used below).
//
//   G5  analytical square duct, Re = 10 (cases/duct_3d; n = 8, 16, 24)
//   G6  the n = 16 duct along x, y and z: the same solution under the axis permutation
//   G7  lid-driven cube, Re = 1000 (cases/lid_driven_cavity_3d_re1000; 32^3, 48^3, 64^3) against
//       Albensoeder & Kuhlmann (2005), Tables 5 and 6
//
// Regular suite: the duct series against the literature constants, and the committed n = 8 duct
// through the production path with every export parsed back. The gate studies are DISABLED_
// (minutes to many hours in Release) and are run explicitly from the repository root, one level
// per process: each level writes results/p12-mesh-006/data/<study>_<level>.json, and the *Gate
// test assembles the decision from those files.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::testutil::CaseFixtureCopy;
using nlohmann::json;

namespace {

constexpr const char* kDuctCase = "cases/duct_3d";
constexpr const char* kCubeCase = "cases/lid_driven_cavity_3d_re1000";
const std::filesystem::path kDataDir = "results/p12-mesh-006/data";
const Real kPi = std::acos(-1.0);
constexpr Index kInvalid = std::numeric_limits<Index>::max();

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string utcNow() {
  const std::time_t t = std::time(nullptr);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buffer;
}

std::string statusName(ProjectRunStatus status) {
  switch (status) {
    case ProjectRunStatus::Converged:
      return "Converged";
    case ProjectRunStatus::DidNotConverge:
      return "DidNotConverge";
    case ProjectRunStatus::NumericalFailure:
      return "NumericalFailure";
    case ProjectRunStatus::Cancelled:
      return "Cancelled";
    case ProjectRunStatus::InvalidCase:
      return "InvalidCase";
    case ProjectRunStatus::ApplicationError:
      return "ApplicationError";
  }
  return "unknown";
}

Real component(const Vector3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

Vector3 unit(int axis) {
  return axis == 0 ? Vector3{1.0, 0.0, 0.0}
                   : (axis == 1 ? Vector3{0.0, 1.0, 0.0} : Vector3{0.0, 0.0, 1.0});
}

// (i, j, k) -> cell id of a uniform Cartesian box, found from the cell centroids (independent of
// the mesh's own cell numbering).
struct BoxIndex {
  Index n[3]{};
  Real h[3]{};
  std::vector<Index> ids;
  [[nodiscard]] Index at(Index i, Index j, Index k) const {
    return ids[i + (n[0] * (j + (n[1] * k)))];
  }
};

BoxIndex indexBox(const cfd::mesh::Mesh& mesh, Index nx, Index ny, Index nz, Real lx, Real ly,
                  Real lz) {
  BoxIndex box;
  box.n[0] = nx;
  box.n[1] = ny;
  box.n[2] = nz;
  box.h[0] = lx / static_cast<Real>(nx);
  box.h[1] = ly / static_cast<Real>(ny);
  box.h[2] = lz / static_cast<Real>(nz);
  box.ids.assign(nx * ny * nz, kInvalid);
  for (const auto& cell : mesh.cells()) {
    Index ijk[3];
    for (int a = 0; a < 3; ++a) {
      ijk[a] = static_cast<Index>(std::floor(component(cell.centroid(), a) / box.h[a]));
    }
    box.ids[ijk[0] + (nx * (ijk[1] + (ny * ijk[2])))] = cell.id();
  }
  for (const Index id : box.ids) EXPECT_NE(id, kInvalid);
  return box;
}

bool allFinite(const cfd::pressure_velocity::SIMPLEResult& r) {
  bool finite = true;
  for (Index i = 0; i < r.velocity.size(); ++i) {
    finite = finite && cfd::isFinite(r.velocity[i]) && std::isfinite(r.pressure[i]);
  }
  for (Index f = 0; f < r.massFlux.size(); ++f) finite = finite && std::isfinite(r.massFlux[f]);
  return finite;
}

bool exportsWritten(const ProjectRunResult& run) {
  return run.exportSummary.has_value() && run.exportSummary->fieldsCsvPath.has_value() &&
         run.exportSummary->vtkPath.has_value() &&
         std::filesystem::is_regular_file(run.exportSummary->metadataPath) &&
         std::filesystem::is_regular_file(run.exportSummary->residualsCsvPath) &&
         std::filesystem::is_regular_file(*run.exportSummary->fieldsCsvPath) &&
         std::filesystem::is_regular_file(*run.exportSummary->vtkPath);
}

// A variant written by CaseWriter must carry the committed case's solver configuration unchanged.
bool sameSolverConfiguration(const cfd::io::SolverConfig& a, const cfd::io::SolverConfig& b) {
  const auto sameLinear = [](const cfd::io::LinearSolverSpec& x,
                             const cfd::io::LinearSolverSpec& y) {
    return x.type == y.type && x.backend == y.backend &&
           x.absoluteTolerance == y.absoluteTolerance &&
           x.relativeTolerance == y.relativeTolerance && x.maxIterations == y.maxIterations;
  };
  return a.type == b.type && a.maxIterations == b.maxIterations &&
         a.velocityRelaxation == b.velocityRelaxation &&
         a.pressureRelaxation == b.pressureRelaxation &&
         a.velocityTolerance == b.velocityTolerance && a.pressureTolerance == b.pressureTolerance &&
         a.continuityTolerance == b.continuityTolerance &&
         sameLinear(a.momentumSolver, b.momentumSolver) &&
         sameLinear(a.pressureSolver, b.pressureSolver) &&
         a.convectionScheme == b.convectionScheme && a.gradientScheme == b.gradientScheme &&
         a.nonOrthogonalCorrections == b.nonOrthogonalCorrections && a.faceFlux == b.faceFlux;
}

void writeJson(const std::string& name, const json& document) {
  std::filesystem::create_directories(kDataDir);
  std::ofstream(kDataDir / (name + ".json")) << document.dump(2) << '\n';
}

json readJson(const std::string& name) {
  const auto path = kDataDir / (name + ".json");
  if (!std::filesystem::is_regular_file(path)) {
    ADD_FAILURE() << path << " is missing: run its level test first";
    return json::object();
  }
  return json::parse(readFile(path));
}

// ================================================================================================
// G5 / G6 -- the square duct (side a = 1, length 6, U = 1, rho = 1, mu = 0.1: Re = 10)
// ================================================================================================
constexpr Real kDuctViscosity = 0.1;
constexpr Real kDuctVelocity = 1.0;
constexpr int kSeriesMax = 399;  // m, n odd <= 399 (acceptance_gate.md G5)

// K = Q mu / (G a^4) = (64 / pi^6) sum_{m, n odd} 1 / (m^2 n^2 (m^2 + n^2)).
Real seriesK() {
  Real sum = 0.0;
  for (int m = 1; m <= kSeriesMax; m += 2) {
    for (int n = 1; n <= kSeriesMax; n += 2) {
      const auto mm = static_cast<Real>(m * m);
      const auto nn = static_cast<Real>(n * n);
      sum += 1.0 / (mm * nn * (mm + nn));
    }
  }
  return 64.0 * sum / std::pow(kPi, 6);
}

// The fully developed Navier-Stokes solution; G = -dp/dx from Q = U a^2 = K G a^4 / mu.
struct DuctExact {
  Real k = seriesK();
  Real g = kDuctVelocity * kDuctViscosity / k;
  // u on the cross-section [0, 1]^2 (c1, c2 the two cross-flow coordinates).
  [[nodiscard]] Real u(Real c1, Real c2) const {
    std::vector<Real> s2;
    for (int n = 1; n <= kSeriesMax; n += 2)
      s2.push_back(std::sin(static_cast<Real>(n) * kPi * c2));
    Real sum = 0.0;
    for (int m = 1; m <= kSeriesMax; m += 2) {
      const Real s1 = std::sin(static_cast<Real>(m) * kPi * c1);
      for (int n = 1; n <= kSeriesMax; n += 2) {
        const auto mm = static_cast<Real>(m * m);
        const auto nn = static_cast<Real>(n * n);
        sum += s1 * s2[static_cast<std::size_t>(n / 2)] / (static_cast<Real>(m * n) * (mm + nn));
      }
    }
    return 16.0 * g * sum / (kDuctViscosity * std::pow(kPi, 4));
  }
};

// The duct along `axis` (0 = x, 1 = y, 2 = z) with an n x n cross-section: the committed
// cases/duct_3d (n = 8, axis x) with the flow axis, grid and patches permuted cyclically, so the
// cross-flow axes are (axis + 1) % 3 and (axis + 2) % 3.
CaseDefinition ductDefinition(Index n, int axis) {
  CaseDefinition d = CaseReader{}.read(kDuctCase);
  Real lengths[3] = {1.0, 1.0, 1.0};
  Index cells[3] = {n, n, n};
  lengths[axis] = 6.0;
  cells[axis] = 6 * n;
  d.geometry.length = lengths[0];
  d.geometry.height = lengths[1];
  d.geometry.depth = lengths[2];
  d.mesh.nx = cells[0];
  d.mesh.ny = cells[1];
  d.mesh.nz = cells[2];
  const auto inlet = d.boundaries.patches.at("xmin");
  const auto outlet = d.boundaries.patches.at("xmax");
  const auto wall = d.boundaries.patches.at("ymin");
  const char* names[3][2] = {{"xmin", "xmax"}, {"ymin", "ymax"}, {"zmin", "zmax"}};
  d.boundaries.patches.clear();
  for (int b = 0; b < 3; ++b) {
    auto low = b == axis ? inlet : wall;
    if (b == axis) low.velocity.value = unit(axis) * kDuctVelocity;
    d.boundaries.patches[names[b][0]] = low;
    d.boundaries.patches[names[b][1]] = b == axis ? outlet : wall;
  }
  return d;
}

// A converged duct in canonical coordinates: index (is, i1, i2) along the flow axis and the two
// cross-flow axes; velocity components (streamwise, cross 1, cross 2).
struct DuctRun {
  Index n{0};
  int axis{0};
  std::string status;
  bool converged{false};
  bool finite{false};
  bool exported{false};
  std::string faceFlux;
  Index iterations{0};
  double seconds{0.0};
  json metadata = json::object();
  std::vector<Real> us, u1, u2, p;
  std::vector<Real> sectionFlow;  // per face plane along the flow axis, oriented downstream
  [[nodiscard]] Index ns() const { return 6 * n; }
  [[nodiscard]] Index at(Index is, Index i1, Index i2) const {
    return is + (ns() * (i1 + (n * i2)));
  }
  [[nodiscard]] Real h() const { return 1.0 / static_cast<Real>(n); }
};

DuctRun runDuct(Index n, int axis) {
  CaseFixtureCopy fixture(kDuctCase);
  CaseWriter::write(fixture.path(), ductDefinition(n, axis));
  DuctRun d;
  d.n = n;
  d.axis = axis;
  const CaseDefinition used = CaseReader{}.read(fixture.path());
  if (!sameSolverConfiguration(used.solver, CaseReader{}.read(kDuctCase).solver) ||
      used.physics.dynamicViscosity != kDuctViscosity) {
    ADD_FAILURE() << "the written duct variant does not carry the committed configuration";
    return d;
  }
  const auto start = std::chrono::steady_clock::now();
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  d.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!run.simpleResult.has_value() || !run.mesh.has_value()) {
    ADD_FAILURE() << "no solver result: " << run.errorMessage;
    return d;
  }
  const auto& r = *run.simpleResult;
  const auto& mesh = *run.mesh;
  d.status = statusName(run.status);
  d.converged = run.status == ProjectRunStatus::Converged;
  d.finite = allFinite(r);
  d.exported = exportsWritten(run);
  d.faceFlux = std::string(cfd::pressure_velocity::faceFluxSchemeName(r.faceFlux));
  d.iterations = r.iterations;
  if (d.exported) d.metadata = json::parse(readFile(run.exportSummary->metadataPath));

  const int a1 = (axis + 1) % 3;
  const int a2 = (axis + 2) % 3;
  Index cells[3] = {n, n, n};
  Real lengths[3] = {1.0, 1.0, 1.0};
  cells[axis] = 6 * n;
  lengths[axis] = 6.0;
  const BoxIndex box =
      indexBox(mesh, cells[0], cells[1], cells[2], lengths[0], lengths[1], lengths[2]);
  const Index total = d.ns() * n * n;
  d.us.assign(total, 0.0);
  d.u1.assign(total, 0.0);
  d.u2.assign(total, 0.0);
  d.p.assign(total, 0.0);
  for (Index is = 0; is < d.ns(); ++is) {
    for (Index i1 = 0; i1 < n; ++i1) {
      for (Index i2 = 0; i2 < n; ++i2) {
        Index ijk[3];
        ijk[axis] = is;
        ijk[a1] = i1;
        ijk[a2] = i2;
        const Index id = box.at(ijk[0], ijk[1], ijk[2]);
        const Index c = d.at(is, i1, i2);
        d.us[c] = component(r.velocity[id], axis);
        d.u1[c] = component(r.velocity[id], a1);
        d.u2[c] = component(r.velocity[id], a2);
        d.p[c] = r.pressure[id];
      }
    }
  }
  // Mass flow through every face plane normal to the flow axis (inlet plane 0 .. outlet ns).
  d.sectionFlow.assign(d.ns() + 1, 0.0);
  for (const auto& face : mesh.faces()) {
    const Real normal = component(face.areaVector(), axis);
    if (std::abs(normal) < 0.5 * face.area()) continue;
    const auto plane = static_cast<Index>(std::llround(component(face.centroid(), axis) / d.h()));
    d.sectionFlow[plane] += r.massFlux[face.id()] * (normal > 0.0 ? 1.0 : -1.0);
  }
  return d;
}

struct DuctMetrics {
  Real linf{0.0};  // cross-section max |u - u_exact| / U on the measurement plane
  Real rms{0.0};   // area-weighted RMS (uniform cells: plain RMS)
  Real downstreamLinf{0.0};
  Real downstreamRms{0.0};
  Real crossFlowMax{0.0};  // max |cross-flow velocity| / U on the measurement plane
  Real gradient{0.0};      // -dp/ds, least squares over the cell planes in s in [2.5, 4.5]
  Real gradientError{0.0};
  Real umax{0.0};  // bilinear interpolant at the axis on the measurement plane
  Real umaxError{0.0};
  Real centrelineVariation{0.0};  // (max - min) / u_max of the axis velocity over s in [3, 4.5]
  Real maxSectionError{0.0};      // max over sections |m - m_in| / m_in
  Real globalImbalance{0.0};      // |m_in - m_out| / m_in
  Real inflow{0.0};
};

DuctMetrics measureDuct(const DuctRun& d, const DuctExact& exact) {
  DuctMetrics m;
  const Index n = d.n;
  const Real h = d.h();
  const auto planeErrors = [&](Index is, Real& linf, Real& rms) {
    Real sum = 0.0;
    for (Index i1 = 0; i1 < n; ++i1) {
      for (Index i2 = 0; i2 < n; ++i2) {
        const Real e =
            std::abs(d.us[d.at(is, i1, i2)] - exact.u((static_cast<Real>(i1) + 0.5) * h,
                                                      (static_cast<Real>(i2) + 0.5) * h)) /
            kDuctVelocity;
        linf = std::max(linf, e);
        sum += e * e;
      }
    }
    rms = std::sqrt(sum / static_cast<Real>(n * n));
  };
  const Index plane = (4 * n) - 1;  // x = 4a - h/2 (Amendment A2.2)
  planeErrors(plane, m.linf, m.rms);
  planeErrors(plane + 1, m.downstreamLinf, m.downstreamRms);
  for (Index i1 = 0; i1 < n; ++i1) {
    for (Index i2 = 0; i2 < n; ++i2) {
      m.crossFlowMax = std::max({m.crossFlowMax, std::abs(d.u1[d.at(plane, i1, i2)]),
                                 std::abs(d.u2[d.at(plane, i1, i2)])});
    }
  }
  const auto axisVelocity = [&](Index is) {
    const Index c = n / 2;
    return 0.25 * (d.us[d.at(is, c - 1, c - 1)] + d.us[d.at(is, c, c - 1)] +
                   d.us[d.at(is, c - 1, c)] + d.us[d.at(is, c, c)]);
  };
  const Real exactMax = exact.u(0.5, 0.5);
  m.umax = axisVelocity(plane);
  m.umaxError = std::abs(m.umax - exactMax) / exactMax;
  Real lo = std::numeric_limits<Real>::max();
  Real hi = -lo;
  Real sw = 0.0, sx = 0.0, sxx = 0.0, sp = 0.0, sxp = 0.0;
  for (Index is = 0; is < d.ns(); ++is) {
    const Real s = (static_cast<Real>(is) + 0.5) * h;
    if (s >= 3.0 && s <= 4.5) {
      lo = std::min(lo, axisVelocity(is));
      hi = std::max(hi, axisVelocity(is));
    }
    if (s < 2.5 || s > 4.5) continue;
    Real mean = 0.0;
    for (Index i1 = 0; i1 < n; ++i1) {
      for (Index i2 = 0; i2 < n; ++i2) mean += d.p[d.at(is, i1, i2)];
    }
    mean /= static_cast<Real>(n * n);
    sw += 1.0;
    sx += s;
    sxx += s * s;
    sp += mean;
    sxp += s * mean;
  }
  m.centrelineVariation = (hi - lo) / exactMax;
  m.gradient = -((sw * sxp) - (sx * sp)) / ((sw * sxx) - (sx * sx));
  m.gradientError = std::abs(m.gradient - exact.g) / exact.g;
  m.inflow = d.sectionFlow.front();  // = rho U a^2 through the inlet
  for (const Real flow : d.sectionFlow) {
    m.maxSectionError = std::max(m.maxSectionError, std::abs(flow - m.inflow) / m.inflow);
  }
  m.globalImbalance =
      std::abs(d.sectionFlow.front() - d.sectionFlow.back()) / d.sectionFlow.front();
  return m;
}

json ductJson(const DuctRun& d, const DuctMetrics& m) {
  return json{{"n", d.n},
              {"axis", d.axis},
              {"cells", d.ns() * d.n * d.n},
              {"status", d.status},
              {"converged", d.converged},
              {"finite", d.finite},
              {"exported", d.exported},
              {"face_flux", d.faceFlux},
              {"iterations", d.iterations},
              {"seconds", d.seconds},
              {"linf", m.linf},
              {"rms", m.rms},
              {"downstream_plane_linf", m.downstreamLinf},
              {"downstream_plane_rms", m.downstreamRms},
              {"cross_flow_max", m.crossFlowMax},
              {"pressure_gradient", m.gradient},
              {"pressure_gradient_error", m.gradientError},
              {"u_max", m.umax},
              {"u_max_error", m.umaxError},
              {"centreline_variation_3_to_4.5", m.centrelineVariation},
              {"max_section_flow_error", m.maxSectionError},
              {"global_imbalance", m.globalImbalance},
              {"inflow", m.inflow},
              {"metadata_conservation", d.metadata.value("conservation", json::object())},
              {"metadata_mesh", d.metadata.value("mesh", json::object())},
              {"finished_utc", utcNow()}};
}

std::string ductLine(const json& j) {
  char buffer[512];
  std::snprintf(
      buffer, sizeof(buffer),
      "n=%2d cells %6d | %s, %s, %d it, %.1f s | Linf %.4e RMS %.4e | dp/dx %.6f err %.4e | "
      "u_max %.5f err %.4e | cross-flow %.2e | sections %.2e global %.2e",
      j.at("n").get<int>(), j.at("cells").get<int>(), j.at("status").get<std::string>().c_str(),
      j.at("face_flux").get<std::string>().c_str(), j.at("iterations").get<int>(),
      j.at("seconds").get<double>(), j.at("linf").get<double>(), j.at("rms").get<double>(),
      j.at("pressure_gradient").get<double>(), j.at("pressure_gradient_error").get<double>(),
      j.at("u_max").get<double>(), j.at("u_max_error").get<double>(),
      j.at("cross_flow_max").get<double>(), j.at("max_section_flow_error").get<double>(),
      j.at("global_imbalance").get<double>());
  return buffer;
}

void runDuctLevel(Index n) {
  const DuctExact exact;
  const DuctRun d = runDuct(n, 0);
  const DuctMetrics m = measureDuct(d, exact);
  const json j = ductJson(d, m);
  writeJson("duct3d_n" + std::to_string(n), j);
  std::printf("%s\n", ductLine(j).c_str());
  EXPECT_TRUE(d.converged && d.finite && d.exported) << d.status;
}

// ================================================================================================
// G7 -- lid-driven cube, Re = 1000 (lid ymax moving +x)
// ================================================================================================
// Albensoeder & Kuhlmann (2005), J. Comput. Phys. 206, 536-558: Table 5 (lid-parallel velocity
// along the lid-normal centreline) and Table 6 (lid-normal velocity along the lid-parallel
// centreline), cube, rigid end walls, Re = 1000. Transcribed from tum-pbs/PICT tests/validations.py
// @ a95d7f9d0713262a1bff2bd9e2be5a203ee69208, lid_driven_cavity_3D, keys (1000, 1, 1, False).
// Their cavity is [-1/2, 1/2]^3 with the lid at x = -1/2 moving +y. Mapping to CFDApp:
// x_AK = 1/2 - y, y_AK = x - 1/2, z_AK = z - 1/2; v_AK = u, u_AK = -v.
const std::vector<Real> kTable5X = {-0.5,    -0.4766, -0.4688, -0.4609, -0.4531, -0.3516,
                                    -0.2344, -0.1172, 0.0,     0.0469,  0.2187,  0.3281,
                                    0.3984,  0.4297,  0.4375,  0.4453,  0.5};
const std::vector<Real> kTable5V = {1.0,      0.58964,  0.48443,  0.39821,  0.33171,  0.12183,
                                    0.07334,  0.03905,  0.00802,  -0.00612, -0.10999, -0.25160,
                                    -0.27293, -0.23696, -0.22283, -0.20623, 0.0};
const std::vector<Real> kTable6Y = {-0.5,    -0.4375, -0.4297, -0.4219, -0.4062, -0.3437,
                                    -0.2734, -0.2656, 0.0,     0.3047,  0.3594,  0.4063,
                                    0.4453,  0.4531,  0.4609,  0.4688,  0.5};
const std::vector<Real> kTable6U = {0.0,      -0.21738, -0.22746, -0.23503, -0.24407, -0.22924,
                                    -0.17580, -0.16987, -0.03674, 0.15223,  0.31117,  0.43423,
                                    0.33511,  0.29032,  0.24095,  0.18864,  0.0};

// Trilinear interpolation of the cell-centred velocity of an n^3 unit cube on the cell-centre
// lattice extended by the walls (node -1 = the wall at 0, node n = the wall at 1, carrying the
// boundary value: the lid (1, 0, 0) on ymax, 0 on every other wall).
Vector3 sampleCube(const BoxIndex& box, const cfd::fields::VectorField& velocity,
                   const Vector3& x) {
  const auto n = static_cast<long long>(box.n[0]);
  const Real h = 1.0 / static_cast<Real>(n);
  long long base[3];
  Real weight[3];
  for (int a = 0; a < 3; ++a) {
    const Real s = component(x, a);
    long long lo = static_cast<long long>(std::floor((s / h) - 0.5));
    lo = std::clamp(lo, -1LL, n - 1);
    const Real t0 = lo < 0 ? 0.0 : (static_cast<Real>(lo) + 0.5) * h;
    const Real t1 = lo + 1 >= n ? 1.0 : (static_cast<Real>(lo) + 1.5) * h;
    base[a] = lo;
    weight[a] = (s - t0) / (t1 - t0);
  }
  const auto node = [&](long long i, long long j, long long k) {
    const bool wallX = i < 0 || i >= n;
    const bool wallZ = k < 0 || k >= n;
    if (j >= n) return (wallX || wallZ) ? Vector3{} : Vector3{1.0, 0.0, 0.0};
    if (wallX || wallZ || j < 0) return Vector3{};
    return velocity[box.at(static_cast<Index>(i), static_cast<Index>(j), static_cast<Index>(k))];
  };
  Vector3 value{};
  for (int di = 0; di < 2; ++di) {
    for (int dj = 0; dj < 2; ++dj) {
      for (int dk = 0; dk < 2; ++dk) {
        const Real w = (di == 1 ? weight[0] : 1.0 - weight[0]) *
                       (dj == 1 ? weight[1] : 1.0 - weight[1]) *
                       (dk == 1 ? weight[2] : 1.0 - weight[2]);
        value = value + (node(base[0] + di, base[1] + dj, base[2] + dk) * w);
      }
    }
  }
  return value;
}

json measureCube(const ProjectRunResult& run, Index n, double seconds);

CaseDefinition cubeDefinition(Index n) {
  CaseDefinition d = CaseReader{}.read(kCubeCase);
  d.mesh.nx = n;
  d.mesh.ny = n;
  d.mesh.nz = n;
  return d;
}

void runCubeLevel(Index n) {
  CaseFixtureCopy fixture(kCubeCase);
  const CaseDefinition base = CaseReader{}.read(kCubeCase);
  if (n != base.mesh.nx) CaseWriter::write(fixture.path(), cubeDefinition(n));
  const CaseDefinition used = CaseReader{}.read(fixture.path());
  ASSERT_EQ(used.mesh.nx, n);
  ASSERT_EQ(used.mesh.ny, n);
  ASSERT_EQ(used.mesh.nz, n);
  // Everything but the grid is the committed benchmark configuration.
  ASSERT_TRUE(sameSolverConfiguration(used.solver, base.solver));
  ASSERT_EQ(used.physics.dynamicViscosity, base.physics.dynamicViscosity);
  ASSERT_EQ(used.physics.density, base.physics.density);
  ASSERT_EQ(used.boundaries.patches.at("ymax").velocity.value, (Vector3{1.0, 0.0, 0.0}));

  const auto start = std::chrono::steady_clock::now();
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  ASSERT_TRUE(run.simpleResult.has_value() && run.mesh.has_value()) << run.errorMessage;
  const json j = measureCube(run, n, seconds);
  writeJson("cavity3d_re1000_n" + std::to_string(n), j);
  std::printf("%dx%dx%d: %s, %d iterations, %.1f s, symmetry %.3e, net flux %.3e\n", int(n), int(n),
              int(n), j.at("status").get<std::string>().c_str(), j.at("iterations").get<int>(),
              seconds, j.at("spanwise_symmetry").get<double>(),
              j.at("net_boundary_flux").get<double>());
  EXPECT_EQ(run.status, ProjectRunStatus::Converged);
}

// Samples the centrelines, the spanwise symmetry, the net boundary flux and the midplane vortex
// centre of a converged n^3 lid-driven cube (lid ymax moving +x).
json measureCube(const ProjectRunResult& run, Index n, double seconds) {
  const auto& r = *run.simpleResult;
  const auto& mesh = *run.mesh;
  const BoxIndex box = indexBox(mesh, n, n, n, 1.0, 1.0, 1.0);

  json table5 = json::array();
  json table6 = json::array();
  for (std::size_t s = 1; s + 1 < kTable5X.size(); ++s) {
    const Vector3 at5{0.5, 0.5 - kTable5X[s], 0.5};
    const Real u = sampleCube(box, r.velocity, at5).x;
    table5.push_back({{"x_ak", kTable5X[s]},
                      {"reference_v_ak", kTable5V[s]},
                      {"cfdapp", u},
                      {"difference", u - kTable5V[s]}});
    const Vector3 at6{kTable6Y[s] + 0.5, 0.5, 0.5};
    const Real v = -sampleCube(box, r.velocity, at6).y;
    table6.push_back({{"y_ak", kTable6Y[s]},
                      {"reference_u_ak", kTable6U[s]},
                      {"cfdapp", v},
                      {"difference", v - kTable6U[s]}});
  }
  // Spanwise mirror symmetry about z = 1/2: u, v even, w odd.
  Real symmetry = 0.0;
  for (Index k = 0; k < n; ++k) {
    for (Index j = 0; j < n; ++j) {
      for (Index i = 0; i < n; ++i) {
        const Vector3& a = r.velocity[box.at(i, j, k)];
        const Vector3& b = r.velocity[box.at(i, j, n - 1 - k)];
        symmetry =
            std::max({symmetry, std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z + b.z)});
      }
    }
  }
  Real net = 0.0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) net += r.massFlux[face.id()];
  }
  // Reported: primary-vortex centre on the midplane z = 1/2 -- the minimum of the stream function
  // psi = integral of u dy from the bottom wall (u interpolated to the midplane).
  const Real h = 1.0 / static_cast<Real>(n);
  Real psiMin = 0.0;
  Real vortexX = 0.0;
  Real vortexY = 0.0;
  for (Index i = 0; i < n; ++i) {
    Real psi = 0.0;
    for (Index j = 0; j < n; ++j) {
      psi +=
          0.5 * (r.velocity[box.at(i, j, (n / 2) - 1)].x + r.velocity[box.at(i, j, n / 2)].x) * h;
      if (psi < psiMin) {
        psiMin = psi;
        vortexX = (static_cast<Real>(i) + 0.5) * h;
        vortexY = static_cast<Real>(j + 1) * h;
      }
    }
  }
  const json j{{"n", n},
               {"cells", n * n * n},
               {"status", statusName(run.status)},
               {"converged", run.status == ProjectRunStatus::Converged},
               {"finite", allFinite(r)},
               {"exported", exportsWritten(run)},
               {"face_flux", std::string(cfd::pressure_velocity::faceFluxSchemeName(r.faceFlux))},
               {"iterations", r.iterations},
               {"seconds", seconds},
               {"final_residuals",
                {{"u", r.finalUResidual},
                 {"v", r.finalVResidual},
                 {"w", r.finalWResidual},
                 {"p", r.finalPressureResidual},
                 {"continuity", r.finalContinuityResidual}}},
               {"table5", table5},
               {"table6", table6},
               {"spanwise_symmetry", symmetry},
               {"net_boundary_flux", net},
               {"vortex_centre_midplane", {{"x", vortexX}, {"y", vortexY}, {"psi", psiMin}}},
               {"finished_utc", utcNow()}};
  return j;
}

struct CubeComparison {
  Real maxDifference{0.0};
  Real rms{0.0};
  Real table5Min{0.0};  // |difference| at the Table 5 minimum (x_AK = 0.3984)
  Real table6Min{0.0};  // at the Table 6 minimum (y_AK = -0.4062)
  Real table6Max{0.0};  // at the Table 6 maximum (y_AK = 0.4063)
};

CubeComparison compareCube(const json& level) {
  CubeComparison c;
  Real sum = 0.0;
  int count = 0;
  for (const char* table : {"table5", "table6"}) {
    for (const auto& station : level.at(table)) {
      const Real d = std::abs(station.at("difference").get<Real>());
      c.maxDifference = std::max(c.maxDifference, d);
      sum += d * d;
      ++count;
      if (std::string(table) == "table5" && station.at("x_ak").get<Real>() == 0.3984)
        c.table5Min = d;
      if (std::string(table) == "table6" && station.at("y_ak").get<Real>() == -0.4062)
        c.table6Min = d;
      if (std::string(table) == "table6" && station.at("y_ak").get<Real>() == 0.4063)
        c.table6Max = d;
    }
  }
  c.rms = std::sqrt(sum / count);
  return c;
}

struct GateLog {
  std::string text;
  bool allPassed{true};
  void operator()(const std::string& label, bool passed, const std::string& detail) {
    text += std::string(passed ? "PASS " : "FAIL ") + label + ": " + detail + "\n";
    allPassed = allPassed && passed;
    EXPECT_TRUE(passed) << label << ": " << detail;
  }
};

std::string fmt(const char* format, double value) {
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

}  // namespace

// --- Regular suite --------------------------------------------------------------------------

// G5's independent check of the series: the literature constants of the square duct, K =
// Q mu / (G a^4) = 0.035144 (f Re = 2 / K = 56.91) and u_max / U = 2.0962, to 4 digits.
TEST(Duct3DProductionCase, SeriesReproducesTheSquareDuctConstants) {
  const DuctExact exact;
  std::printf("K = %.7f, u_max/U = %.6f, f Re = %.4f\n", exact.k, exact.u(0.5, 0.5), 2.0 / exact.k);
  EXPECT_NEAR(exact.k / 0.035144, 1.0, 1e-4);
  EXPECT_NEAR(exact.u(0.5, 0.5) / 2.0962, 1.0, 1e-4);
  // Symmetric and zero on the walls.
  EXPECT_NEAR(exact.u(0.2, 0.7), exact.u(0.7, 0.2), 1e-12);
  EXPECT_NEAR(exact.u(0.0, 0.4), 0.0, 1e-12);
}

// The committed duct (n = 8) through CaseReader -> CaseBuilder -> ProjectRunner -> export: 3D
// case, Converged from rest, Rhie-Chow, mass conserved through every section, and every export
// parsed back (G9.3): fields.csv, residuals.csv (with w), solution.vtk (hexahedra, the solution's
// values exactly), metadata.json (dimension 3, grid, lengths, face flux, residuals, mass balance).
TEST(Duct3DProductionCase, CommittedDuctRunsConservesAndExports) {
  const CaseDefinition committed = CaseReader{}.read(kDuctCase);
  EXPECT_EQ(cfd::io::geometryDimension(committed.geometry), 3);
  EXPECT_EQ(committed.mesh.nz, 8u);
  EXPECT_EQ(committed.solver.faceFlux, "rhie_chow");

  const CaseFixtureCopy fixture(kDuctCase);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.simpleResult.has_value() && run.mesh.has_value());
  const auto& r = *run.simpleResult;
  const auto& mesh = *run.mesh;
  EXPECT_EQ(mesh.dimension(), 3);
  EXPECT_EQ(r.faceFlux, cfd::pressure_velocity::FaceFluxScheme::RhieChow);
  EXPECT_TRUE(allFinite(r));
  ASSERT_EQ(r.wResidualHistory.size(), r.uResidualHistory.size());
  ASSERT_TRUE(exportsWritten(run));

  // Section mass flow (the measurement of G5.3, here on the committed grid).
  std::vector<Real> sectionFlow(49, 0.0);
  for (const auto& face : mesh.faces()) {
    const Real normal = face.areaVector().x;
    if (std::abs(normal) < 0.5 * face.area()) continue;
    sectionFlow[static_cast<std::size_t>(std::llround(face.centroid().x * 8.0))] +=
        r.massFlux[face.id()] * (normal > 0.0 ? 1.0 : -1.0);
  }
  for (const Real flow : sectionFlow) EXPECT_NEAR(flow, 1.0, 1e-6);

  const Index cells = mesh.numberOfCells();
  ASSERT_EQ(cells, 48u * 8u * 8u);
  // fields.csv: header and one row per cell, values equal to the solution.
  {
    std::istringstream in(readFile(*run.exportSummary->fieldsCsvPath));
    std::string line;
    std::getline(in, line);
    EXPECT_EQ(line, "cell_id,x,y,z,velocity_x,velocity_y,velocity_z,velocity_magnitude,pressure");
    Index rows = 0;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      std::replace(line.begin(), line.end(), ',', ' ');
      std::istringstream row(line);
      Index id = 0;
      Real x = 0, y = 0, z = 0, u = 0, v = 0, w = 0, magnitude = 0, p = 0;
      row >> id >> x >> y >> z >> u >> v >> w >> magnitude >> p;
      ASSERT_LT(id, cells);
      EXPECT_EQ(x, mesh.cell(id).centroid().x);
      EXPECT_EQ(z, mesh.cell(id).centroid().z);
      EXPECT_EQ(w, r.velocity[id].z);
      EXPECT_EQ(p, r.pressure[id]);
      ++rows;
    }
    EXPECT_EQ(rows, cells);
  }
  // residuals.csv: the w column, one row per outer iteration.
  {
    std::istringstream in(readFile(run.exportSummary->residualsCsvPath));
    std::string line;
    std::getline(in, line);
    EXPECT_EQ(line,
              "iteration,u_residual,v_residual,w_residual,p_residual,continuity_residual,"
              "global_mass_imbalance");
    Index rows = 0;
    while (std::getline(in, line)) rows += line.empty() ? 0 : 1;
    EXPECT_EQ(rows, r.iterations);
  }
  // solution.vtk: (nx+1)(ny+1)(nz+1) points, n VTK_HEXAHEDRON (12) cells, pressure and the
  // 3-component velocity as cell data, equal to the solution.
  {
    std::istringstream in(readFile(*run.exportSummary->vtkPath));
    std::string line;
    std::size_t points = 0, cellCount = 0;
    std::vector<Real> pressure;
    std::vector<Vector3> velocity;
    while (std::getline(in, line)) {
      std::istringstream header(line);
      std::string keyword;
      header >> keyword;
      if (keyword == "POINTS") {
        header >> points;
        for (std::size_t i = 0; i < points; ++i) std::getline(in, line);
      } else if (keyword == "CELLS") {
        header >> cellCount;
        for (std::size_t i = 0; i < cellCount; ++i) {
          std::getline(in, line);
          EXPECT_EQ(line.rfind("8 ", 0), 0u);
        }
      } else if (keyword == "CELL_TYPES") {
        for (std::size_t i = 0; i < cellCount; ++i) {
          std::getline(in, line);
          EXPECT_EQ(line, "12");
        }
      } else if (keyword == "SCALARS") {
        std::string name;
        header >> name;
        std::getline(in, line);  // LOOKUP_TABLE
        std::vector<Real> values(cells);
        for (auto& value : values) in >> value;
        if (name == "pressure") pressure = values;
      } else if (keyword == "VECTORS") {
        std::string name;
        header >> name;
        EXPECT_EQ(name, "velocity");
        velocity.resize(cells);
        for (auto& value : velocity) in >> value.x >> value.y >> value.z;
      }
    }
    EXPECT_EQ(points, 49u * 9u * 9u);
    EXPECT_EQ(cellCount, cells);
    ASSERT_EQ(pressure.size(), cells);
    ASSERT_EQ(velocity.size(), cells);
    for (Index i = 0; i < cells; ++i) {
      EXPECT_EQ(pressure[i], r.pressure[i]);
      EXPECT_EQ(velocity[i].x, r.velocity[i].x);
      EXPECT_EQ(velocity[i].y, r.velocity[i].y);
      EXPECT_EQ(velocity[i].z, r.velocity[i].z);
    }
  }
  // metadata.json
  const json metadata = json::parse(readFile(run.exportSummary->metadataPath));
  EXPECT_EQ(metadata.at("mesh").at("dimension").get<int>(), 3);
  EXPECT_EQ(metadata.at("mesh").at("nx").get<Index>(), 48u);
  EXPECT_EQ(metadata.at("mesh").at("ny").get<Index>(), 8u);
  EXPECT_EQ(metadata.at("mesh").at("nz").get<Index>(), 8u);
  EXPECT_EQ(metadata.at("mesh").at("lx").get<Real>(), 6.0);
  EXPECT_EQ(metadata.at("mesh").at("lz").get<Real>(), 1.0);
  EXPECT_EQ(metadata.at("solver").at("face_flux").get<std::string>(), "rhie_chow");
  EXPECT_EQ(metadata.at("residuals").at("w").get<Real>(), r.finalWResidual);
  const auto& conservation = metadata.at("conservation");
  EXPECT_NEAR(conservation.at("inflow").get<Real>(), 1.0, 1e-12);
  EXPECT_NEAR(conservation.at("outflow").get<Real>(), 1.0, 1e-6);
  EXPECT_LE(conservation.at("relative_imbalance").get<Real>(), 1e-6);
  for (const char* key : {"net_boundary_flux", "max_cell_imbalance", "rms_cell_imbalance",
                          "flux_scale", "normalized_continuity"}) {
    EXPECT_TRUE(conservation.contains(key)) << key;
  }
  std::printf("committed duct 48x8x8: %d iterations, W residual %.3e, relative imbalance %.3e\n",
              int(r.iterations), r.finalWResidual,
              conservation.at("relative_imbalance").get<Real>());

  // The G5 variant writer reproduces the committed case exactly (same solve), and the G5
  // measurement runs on it (values reported, not gated at n = 8 beyond G5.2/G5.3/G5.4).
  const DuctRun variant = runDuct(8, 0);
  EXPECT_EQ(variant.iterations, r.iterations);
  EXPECT_TRUE(variant.converged && variant.finite && variant.exported);
  std::printf("%s\n", ductLine(ductJson(variant, measureDuct(variant, DuctExact{}))).c_str());
}

// The centreline sampler of G7 is exact for fields that are linear along the sampled line and
// consistent with the wall values: u = y on the lid-normal line (0 at ymin, the lid's 1 at ymax),
// v = x on the lid-parallel line between the xmin wall (0) and the last cell centre.
TEST(LidDrivenCube3DProductionCase, CentrelineSamplerIsExactForLinearFields) {
  const Index n = 8;
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  const BoxIndex box = indexBox(mesh, n, n, n, 1.0, 1.0, 1.0);
  cfd::fields::VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = Vector3{cell.centroid().y, cell.centroid().x, 0.25};
  }
  const Real h = 1.0 / static_cast<Real>(n);
  for (const Real s : {0.0, 0.01, h / 2.0, 0.3, 0.5, 0.77, 1.0 - (h / 2.0), 0.99, 1.0}) {
    EXPECT_NEAR(sampleCube(box, velocity, Vector3{0.5, s, 0.5}).x, s, 1e-15) << s;
  }
  for (const Real s : {0.0, h / 4.0, h / 2.0, 0.3, 0.5, 1.0 - (h / 2.0)}) {
    EXPECT_NEAR(sampleCube(box, velocity, Vector3{s, 0.5, 0.5}).y, s, 1e-15) << s;
  }
  // Between the last cell centre and the xmax wall: linear to the wall value 0.
  EXPECT_NEAR(sampleCube(box, velocity, Vector3{1.0 - (h / 4.0), 0.5, 0.5}).y,
              0.5 * (1.0 - (h / 2.0)), 1e-15);
  // Station bookkeeping: 15 interior stations per table, the three gated extrema among them.
  EXPECT_EQ(kTable5X.size(), 17u);
  EXPECT_EQ(kTable6Y.size(), 17u);
  EXPECT_EQ(*std::min_element(kTable5V.begin(), kTable5V.end()), -0.27293);
  EXPECT_EQ(*std::min_element(kTable6U.begin(), kTable6U.end()), -0.24407);
  EXPECT_EQ(*std::max_element(kTable6U.begin(), kTable6U.end()), 0.43423);
}

// The light committed cube (cases/lid_driven_cavity_3d, Re = 100, 16^3, face flux automatic)
// through the production path, measured with the G7 machinery.
TEST(LidDrivenCube3DProductionCase, CommittedLightCubeRunsAndIsMeasured) {
  const CaseFixtureCopy fixture("cases/lid_driven_cavity_3d");
  EXPECT_EQ(CaseReader{}.read(fixture.path()).solver.faceFlux, "automatic");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_EQ(run.status, ProjectRunStatus::Converged) << run.errorMessage;
  ASSERT_TRUE(run.simpleResult.has_value() && run.mesh.has_value());
  EXPECT_EQ(run.simpleResult->faceFlux, cfd::pressure_velocity::FaceFluxScheme::RhieChow);
  const json j = measureCube(run, 16, 0.0);
  EXPECT_TRUE(j.at("finite").get<bool>());
  EXPECT_TRUE(j.at("exported").get<bool>());
  EXPECT_EQ(j.at("table5").size(), 15u);
  EXPECT_EQ(j.at("table6").size(), 15u);
  EXPECT_LE(std::abs(j.at("net_boundary_flux").get<Real>()), 1e-12);
  // The discretization is mirror-symmetric; what remains is iterative and scales linearly with
  // the outer tolerance (results/p12-mesh-006/logs/03_diag_spanwise_asymmetry_vs_tolerance.log:
  // 6e-8 / 7e-10 / 7e-12 at 1e-6 / 1e-8 / 1e-10), so it is bounded by this case's tolerance.
  EXPECT_LE(j.at("spanwise_symmetry").get<Real>(),
            CaseReader{}.read(fixture.path()).solver.velocityTolerance);
  std::printf(
      "light cube 16^3 Re=100: %d iterations, symmetry %.3e, net flux %.3e, vortex (%.4f, %.4f)\n",
      j.at("iterations").get<int>(), j.at("spanwise_symmetry").get<double>(),
      j.at("net_boundary_flux").get<double>(), j.at("vortex_centre_midplane").at("x").get<double>(),
      j.at("vortex_centre_midplane").at("y").get<double>());
}

// --- G5: one grid per process, then the gate -------------------------------------------------
TEST(Duct3DProductionCase, DISABLED_DuctLevel8) { runDuctLevel(8); }
TEST(Duct3DProductionCase, DISABLED_DuctLevel16) { runDuctLevel(16); }
TEST(Duct3DProductionCase, DISABLED_DuctLevel24) { runDuctLevel(24); }

TEST(Duct3DProductionCase, DISABLED_DuctGate) {
  const json l8 = readJson("duct3d_n8");
  const json l16 = readJson("duct3d_n16");
  const json l24 = readJson("duct3d_n24");
  ASSERT_FALSE(l8.empty() || l16.empty() || l24.empty());
  GateLog gate;
  gate.text = "P12-MESH-006 G5 -- analytical square duct, Re = 10 (acceptance_gate.md G5, A2)\n";
  gate.text += "exact: K = " + fmt("%.7f", DuctExact{}.k) +
               ", G = -dp/dx = " + fmt("%.7f", DuctExact{}.g) +
               ", u_max = " + fmt("%.6f", DuctExact{}.u(0.5, 0.5)) + "\n\n";
  for (const json* l : {&l8, &l16, &l24}) gate.text += ductLine(*l) + "\n";
  gate.text += "\n";
  const auto value = [](const json& l, const char* key) { return l.at(key).get<Real>(); };
  // G5.1
  gate("G5.1 n=24 Linf <= 0.010", value(l24, "linf") <= 0.010, fmt("%.4e", value(l24, "linf")));
  gate("G5.1 n=24 RMS <= 0.005", value(l24, "rms") <= 0.005, fmt("%.4e", value(l24, "rms")));
  gate("G5.1 n=24 |dp/dx error|/G <= 0.015", value(l24, "pressure_gradient_error") <= 0.015,
       fmt("%.4e", value(l24, "pressure_gradient_error")));
  gate("G5.1 n=24 |u_max error|/u_max <= 0.010", value(l24, "u_max_error") <= 0.010,
       fmt("%.4e", value(l24, "u_max_error")));
  gate("G5.1 n=16 Linf <= 0.020", value(l16, "linf") <= 0.020, fmt("%.4e", value(l16, "linf")));
  gate("G5.1 n=16 |dp/dx error|/G <= 0.030", value(l16, "pressure_gradient_error") <= 0.030,
       fmt("%.4e", value(l16, "pressure_gradient_error")));
  // G5.2
  for (const char* key : {"rms", "pressure_gradient_error"}) {
    gate(std::string("G5.2 ") + key + " decreasing 8 -> 16 -> 24",
         value(l16, key) < value(l8, key) && value(l24, key) < value(l16, key),
         fmt("%.4e", value(l8, key)) + " -> " + fmt("%.4e", value(l16, key)) + " -> " +
             fmt("%.4e", value(l24, key)));
  }
  // G5.3, G5.4
  for (const json* l : {&l8, &l16, &l24}) {
    const std::string n = "n=" + std::to_string(l->at("n").get<int>());
    gate("G5.3 " + n + " every section within 1e-6 of the inflow",
         value(*l, "max_section_flow_error") <= 1e-6,
         fmt("%.3e", value(*l, "max_section_flow_error")));
    gate("G5.3 " + n + " |m_in - m_out|/m_in <= 1e-6", value(*l, "global_imbalance") <= 1e-6,
         fmt("%.3e", value(*l, "global_imbalance")) + " (metadata relative_imbalance " +
             fmt("%.3e", l->at("metadata_conservation").at("relative_imbalance").get<Real>()) +
             ")");
    gate("G5.4 " + n + " Converged from rest, finite, exported",
         l->at("converged").get<bool>() && l->at("finite").get<bool>() &&
             l->at("exported").get<bool>() && l->at("face_flux").get<std::string>() == "rhie_chow",
         l->at("status").get<std::string>() + ", " + l->at("face_flux").get<std::string>() + ", " +
             std::to_string(l->at("iterations").get<int>()) + " iterations");
  }
  gate.text +=
      "\nREPORTED: downstream plane (x = 4a + h/2) Linf / RMS; axis-velocity variation over "
      "x in [3a, 4.5a] relative to u_max; max cross-flow velocity on the plane\n";
  for (const json* l : {&l8, &l16, &l24}) {
    gate.text += "  n=" + std::to_string(l->at("n").get<int>()) + ": " +
                 fmt("%.4e", value(*l, "downstream_plane_linf")) + " / " +
                 fmt("%.4e", value(*l, "downstream_plane_rms")) + "; " +
                 fmt("%.3e", value(*l, "centreline_variation_3_to_4.5")) + "; " +
                 fmt("%.3e", value(*l, "cross_flow_max")) + "\n";
  }
  gate.text += gate.allPassed ? "\nDECISION: every G5 item PASSES\n" : "\nDECISION: G5 FAILED\n";
  std::printf("\n%s", gate.text.c_str());
  std::ofstream(kDataDir / "duct3d_gate.txt") << gate.text;
}

// --- G6: the n = 16 duct along x, y and z ----------------------------------------------------
TEST(Duct3DProductionCase, DISABLED_DirectionalSymmetry) {
  const DuctExact exact;
  const DuctRun x = runDuct(16, 0);
  const DuctRun y = runDuct(16, 1);
  const DuctRun z = runDuct(16, 2);
  GateLog gate;
  gate.text = "P12-MESH-006 G6 -- directional symmetry: the n = 16 duct along x, y and z\n\n";
  json document;
  for (const DuctRun* d : {&x, &y, &z}) {
    const DuctMetrics m = measureDuct(*d, exact);
    const json j = ductJson(*d, m);
    document[std::string(1, "xyz"[d->axis])] = j;
    gate.text += std::string(1, "xyz"[d->axis]) + ": " + ductLine(j) + "\n";
    gate("G6 " + std::string(1, "xyz"[d->axis]) + " Converged, finite, Rhie-Chow",
         d->converged && d->finite && d->faceFlux == "rhie_chow", d->status);
  }
  gate.text += "\n";
  const Real pRange =
      *std::max_element(x.p.begin(), x.p.end()) - *std::min_element(x.p.begin(), x.p.end());
  for (const DuctRun* other : {&y, &z}) {
    const std::string pair = std::string("x vs ") + "xyz"[other->axis];
    Real ds = 0.0, d1 = 0.0, d2 = 0.0, dp = 0.0;
    for (std::size_t c = 0; c < x.us.size(); ++c) {
      ds = std::max(ds, std::abs(x.us[c] - other->us[c]) / kDuctVelocity);
      d1 = std::max(d1, std::abs(x.u1[c] - other->u1[c]) / kDuctVelocity);
      d2 = std::max(d2, std::abs(x.u2[c] - other->u2[c]) / kDuctVelocity);
      dp = std::max(dp, std::abs(x.p[c] - other->p[c]));
    }
    const Real dg =
        std::abs(measureDuct(x, exact).gradient - measureDuct(*other, exact).gradient) / exact.g;
    gate("G6.1 " + pair + " streamwise velocity", ds <= 1e-6, fmt("max |du_s|/U %.3e", ds));
    gate("G6.1 " + pair + " cross-flow 1", d1 <= 1e-6, fmt("max |du_1|/U %.3e", d1));
    gate("G6.1 " + pair + " cross-flow 2", d2 <= 1e-6, fmt("max |du_2|/U %.3e", d2));
    gate("G6.1 " + pair + " pressure", dp <= 1e-6 * pRange,
         fmt("max |dp|/(p_max - p_min) %.3e", dp / pRange));
    gate("G6.1 " + pair + " dp/ds", dg <= 1e-6, fmt("|d(dp/ds)|/G %.3e", dg));
    document["differences"][pair] = {{"streamwise", ds},
                                     {"cross1", d1},
                                     {"cross2", d2},
                                     {"pressure_relative", dp / pRange},
                                     {"gradient_relative", dg}};
  }
  const DuctMetrics mz = measureDuct(z, exact);
  gate("G6.2 z-duct Linf <= 0.020", mz.linf <= 0.020, fmt("%.4e", mz.linf));
  gate("G6.2 z-duct |dp/dz error|/G <= 0.030", mz.gradientError <= 0.030,
       fmt("%.4e", mz.gradientError));
  gate.text += gate.allPassed ? "\nDECISION: every G6 item PASSES\n" : "\nDECISION: G6 FAILED\n";
  document["finished_utc"] = utcNow();
  writeJson("duct3d_symmetry", document);
  std::printf("\n%s", gate.text.c_str());
  std::ofstream(kDataDir / "duct3d_symmetry_gate.txt") << gate.text;
}

// --- G7: one grid per process, then the gate -------------------------------------------------
TEST(LidDrivenCube3DProductionCase, DISABLED_CubeLevel32) { runCubeLevel(32); }
TEST(LidDrivenCube3DProductionCase, DISABLED_CubeLevel48) { runCubeLevel(48); }
TEST(LidDrivenCube3DProductionCase, DISABLED_CubeLevel64) { runCubeLevel(64); }

TEST(LidDrivenCube3DProductionCase, DISABLED_CubeGate) {
  const json l32 = readJson("cavity3d_re1000_n32");
  const json l48 = readJson("cavity3d_re1000_n48");
  const json l64 = readJson("cavity3d_re1000_n64");
  ASSERT_FALSE(l32.empty() || l48.empty() || l64.empty());
  GateLog gate;
  gate.text =
      "P12-MESH-006 G7 -- lid-driven cube, Re = 1000, vs Albensoeder & Kuhlmann (2005) Tables "
      "5/6\n\n";
  std::vector<CubeComparison> comparisons;
  for (const json* l : {&l32, &l48, &l64}) {
    const CubeComparison c = compareCube(*l);
    comparisons.push_back(c);
    gate.text += std::to_string(l->at("n").get<int>()) +
                 "^3: " + l->at("status").get<std::string>() + ", " +
                 std::to_string(l->at("iterations").get<int>()) + " iterations, " +
                 fmt("%.0f s", l->at("seconds").get<double>()) + " | max|d| " +
                 fmt("%.4f", c.maxDifference) + ", RMS " + fmt("%.4f", c.rms) + " | extrema |d| " +
                 fmt("%.4f", c.table5Min) + " / " + fmt("%.4f", c.table6Min) + " / " +
                 fmt("%.4f", c.table6Max) + " | symmetry " +
                 fmt("%.3e", l->at("spanwise_symmetry").get<double>()) + " | net flux " +
                 fmt("%.3e", l->at("net_boundary_flux").get<double>()) + " | vortex (" +
                 fmt("%.4f", l->at("vortex_centre_midplane").at("x").get<double>()) + ", " +
                 fmt("%.4f", l->at("vortex_centre_midplane").at("y").get<double>()) + ")\n";
  }
  gate.text += "\nstation table (reference, 32^3, 48^3, 64^3):\n";
  for (const char* table : {"table5", "table6"}) {
    for (std::size_t s = 0; s < l64.at(table).size(); ++s) {
      const auto& station = l64.at(table)[s];
      const bool five = std::string(table) == "table5";
      gate.text +=
          std::string(five ? "  T5 x_AK " : "  T6 y_AK ") +
          fmt("%8.4f", station.at(five ? "x_ak" : "y_ak").get<double>()) + "  ref " +
          fmt("%9.5f", station.at(five ? "reference_v_ak" : "reference_u_ak").get<double>());
      for (const json* l : {&l32, &l48, &l64}) {
        gate.text += fmt("  %9.5f", l->at(table)[s].at("cfdapp").get<double>());
      }
      gate.text += "\n";
    }
  }
  gate.text += "\n";
  const CubeComparison& c64 = comparisons.back();
  gate("G7.1 64^3 max |d| <= 0.06 U", c64.maxDifference <= 0.06, fmt("%.4f", c64.maxDifference));
  gate("G7.1 64^3 RMS <= 0.03 U", c64.rms <= 0.03, fmt("%.4f", c64.rms));
  gate("G7.1 64^3 Table 5 minimum (-0.27293) within 0.03", c64.table5Min <= 0.03,
       fmt("%.4f", c64.table5Min));
  gate("G7.1 64^3 Table 6 minimum (-0.24407) within 0.03", c64.table6Min <= 0.03,
       fmt("%.4f", c64.table6Min));
  gate("G7.1 64^3 Table 6 maximum (0.43423) within 0.03", c64.table6Max <= 0.03,
       fmt("%.4f", c64.table6Max));
  gate("G7.2 max |d| decreasing 32 -> 48 -> 64",
       comparisons[1].maxDifference < comparisons[0].maxDifference &&
           comparisons[2].maxDifference < comparisons[1].maxDifference,
       fmt("%.4f", comparisons[0].maxDifference) + " -> " +
           fmt("%.4f", comparisons[1].maxDifference) + " -> " +
           fmt("%.4f", comparisons[2].maxDifference));
  gate("G7.2 RMS decreasing 32 -> 48 -> 64",
       comparisons[1].rms < comparisons[0].rms && comparisons[2].rms < comparisons[1].rms,
       fmt("%.4f", comparisons[0].rms) + " -> " + fmt("%.4f", comparisons[1].rms) + " -> " +
           fmt("%.4f", comparisons[2].rms));
  gate("G7.3 64^3 spanwise symmetry <= 1e-5 U", l64.at("spanwise_symmetry").get<Real>() <= 1e-5,
       fmt("%.3e", l64.at("spanwise_symmetry").get<double>()));
  for (const json* l : {&l32, &l48, &l64}) {
    const std::string n = std::to_string(l->at("n").get<int>()) + "^3";
    gate("G7.4 " + n + " Converged from rest, finite, Rhie-Chow",
         l->at("converged").get<bool>() && l->at("finite").get<bool>() &&
             l->at("face_flux").get<std::string>() == "rhie_chow",
         l->at("status").get<std::string>());
    gate("G7.4 " + n + " |net boundary flux| <= 1e-12",
         std::abs(l->at("net_boundary_flux").get<Real>()) <= 1e-12,
         fmt("%.3e", l->at("net_boundary_flux").get<double>()));
  }
  gate.text += gate.allPassed ? "\nDECISION: every G7 item PASSES\n" : "\nDECISION: G7 FAILED\n";
  std::printf("\n%s", gate.text.c_str());
  std::ofstream(kDataDir / "cavity3d_re1000_gate.txt") << gate.text;
}
