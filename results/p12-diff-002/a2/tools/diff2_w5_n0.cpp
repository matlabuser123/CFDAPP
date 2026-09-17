// P12-DIFF-002 A2-5: the NEWLY ACTIVATED configuration -- non_orthogonal_corrections = 0.
// Before A2 that configuration used the two-point Dirichlet wall flux; after A2 it uses DIFF-002.
// This probe measures the thermal/species iteration cost of exactly that change, by building one
// source twice: once against the real library, once with -DPRECHANGE (which substitutes the
// pre-DIFF-002 boundaryFaceDiffusionTerms, see the note below). Corrections are OFF in every row.
// Meshes are ORTHOGONAL/rectilinear only, because there decomposeBoundaryFaceArea is exactly
// {Sf, 0}, so the PRECHANGE build at N = 0 reproduces the pre-A2 N = 0 behaviour EXACTLY; on a
// distorted mesh it would not, and the honest baseline there is the frozen W5 case values instead.
//
// Why a second probe is needed. diff2_scalar_iters.cpp compares corrections OFF -> ON. On an
// orthogonal mesh that isolates DIFF-002 exactly, because P12-NUM-003's internal-face correction is
// bit-identical there. On a distorted mesh it does NOT: turning corrections on also turns on
// NUM-003's own lagged explicit internal-face term, which needs Picard iterations of its own. The
// 6x thermal ratio that probe reports for the distorted mesh therefore compares two different
// CONFIGURATIONS, not before/after DIFF-002, and cannot be attributed without this probe.
//
// How this isolates it with NO production change. src/discretization/NonOrthogonalDiffusion.cpp
// defines exactly three external symbols. Built with -DPRECHANGE, this translation unit defines all
// three itself, with the PRE-DIFF-002 bodies transcribed verbatim (the boundary one simply lacks
// the stencil block; the internal one is untouched by DIFF-002). The linker then resolves them here
// and never pulls that archive member out of libcfdcore.a, so the whole library above it --
// ThermalSolver, EnergyEquation, SpeciesEquation -- runs against the pre-DIFF-002 boundary
// treatment. Built without the flag, the same source measures the real library. Two binaries, one
// source, identical settings: the difference between their outputs is DIFF-002 and nothing else.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/SpeciesEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

#ifdef PRECHANGE
#include "cfd/discretization/Interpolation.hpp"

// ---------------------------------------------------------------------------
// The pre-DIFF-002 translation unit, transcribed verbatim from the production source's own
// pre-DIFF-002 state. Nothing here is a new formulation: `twoPointTerms` is the production helper,
// internalFaceDiffusionTerms is byte-for-byte what production still has, and
// boundaryFaceDiffusionTerms is production's current body with the P12-DIFF-002 stencil block
// removed -- i.e. exactly its fallback path, which DIFF-001 verified reproduces
// Gamma |S_orth| / |d| to 4.5e-16.
// ---------------------------------------------------------------------------
namespace cfd::discretization {

using cfd::boundary::BoundaryConditionType;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

bool prescribesBoundaryValue(BoundaryConditionType type) noexcept {
  switch (type) {
    case BoundaryConditionType::FixedValue:
    case BoundaryConditionType::FixedTemperature:
    case BoundaryConditionType::WallOmega:
    case BoundaryConditionType::Wall:
    case BoundaryConditionType::MovingWall:
    case BoundaryConditionType::Inlet:
      return true;
    case BoundaryConditionType::FixedGradient:
    case BoundaryConditionType::HeatFlux:
    case BoundaryConditionType::Adiabatic:
    case BoundaryConditionType::Outlet:
    case BoundaryConditionType::Symmetry:
      return false;
  }
  return false;
}

namespace {

FaceDiffusionTerms twoPointTerms(Real coefficient, Real explicitFlux) {
  FaceDiffusionTerms terms;
  terms.coefficient = coefficient;
  terms.explicitFlux = explicitFlux;
  terms.boundaryValueCoefficient = coefficient;
  terms.farCellCoefficient = 0.0;
  terms.farCell = 0;
  terms.higherOrder = false;
  return terms;
}

}  // namespace

FaceDiffusionTerms internalFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi) {
  if (gradPhi != nullptr) {
    const auto decomposition = MeshGeometry::decomposeFaceArea(mesh, face);
    if (decomposition.valid) {
      const Vector2 gradFace = interpolateInternalFace(mesh, face, *gradPhi);
      return twoPointTerms(gammaFace * magnitude(decomposition.orthogonal) / distance,
                           gammaFace * dot(decomposition.nonOrthogonal, gradFace));
    }
  }
  return twoPointTerms(gammaFace * face.area() / distance, 0.0);
}

FaceDiffusionTerms boundaryFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi,
                                              bool prescribedValue) {
  if (gradPhi != nullptr && prescribedValue) {
    const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
    if (decomposition.valid) {
      return twoPointTerms(gammaFace * magnitude(decomposition.orthogonal) / distance,
                           gammaFace * dot(decomposition.nonOrthogonal, (*gradPhi)[face.owner()]));
    }
  }
  return twoPointTerms(gammaFace * face.area() / distance, 0.0);
}

}  // namespace cfd::discretization
#endif  // PRECHANGE

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

const char* kTag =
#ifdef PRECHANGE
    "PRECHANGE";
#else
    "DIFF-002 ";
#endif

// Corrections OFF: the configuration A2 newly activates the wall reconstruction in.
discretization::NonOrthogonalCorrectionOptions correctionsOn() {
  discretization::NonOrthogonalCorrectionOptions o;
  o.enabled = false;
  return o;
}

boundary::BoundaryConditionSet thermalBoundaries(const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  Real value = 300.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedTemperature>(value));
    value += 25.0;
  }
  return bcs;
}

boundary::BoundaryConditionSet speciesBoundaries(const Mesh& mesh) {
  boundary::BoundaryConditionSet bcs;
  Real value = 1.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<boundary::FixedValue>(value));
    value += 0.5;
  }
  return bcs;
}

template <typename Field>
Real volumeNorm(const Mesh& mesh, const Field& field) {
  Real sum = 0.0;
  Real volume = 0.0;
  for (const auto& c : mesh.cells()) {
    sum += field[c.id()] * field[c.id()] * c.volume();
    volume += c.volume();
  }
  return std::sqrt(sum / volume);
}

void row(const std::string& label, const Mesh& mesh) {
  thermal::ThermalSolverSettings settings;
  settings.nonOrthogonal = correctionsOn();
  const fields::SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const auto thermalResult = thermal::ThermalSolver{settings}.solve(
      mesh, fields::ScalarField(mesh.numberOfCells(), 300.0), massFlux,
      thermal::ThermalProperties{0.6, 4180.0}, thermalBoundaries(mesh));

  const auto assembly = species::assembleSpeciesTransportEquation(
      mesh, fields::ScalarField(mesh.numberOfCells(), 1.0), massFlux,
      physics::FluidProperties{998.0, 1.0e-3}, species::SpeciesProperties{"tracer", 1.0e-5},
      speciesBoundaries(mesh), 0.0, correctionsOn());
  const auto speciesResult =
      algebra::BiCGSTAB{algebra::LinearSolverSettings{}}.solve(assembly.system);

  std::printf(
      "%s %-30s THERMAL outer %4zu linear %4zu conv %d |T|2 %.12e | SPECIES linear %4zu "
      "|Y|2 %.12e\n",
      kTag, label.c_str(), static_cast<std::size_t>(thermalResult.iterations),
      static_cast<std::size_t>(thermalResult.linearIterations), thermalResult.converged() ? 1 : 0,
      volumeNorm(mesh, thermalResult.temperature),
      static_cast<std::size_t>(speciesResult.iterations), volumeNorm(mesh, speciesResult.solution));
}

}  // namespace

int main() {
  std::printf("# P12-DIFF-002 A2-5: corrections OFF (N=0) in every row; the ONLY difference\n");
  std::printf("# between the PRECHANGE and DIFF-002 binaries is the boundary treatment.\n\n");
  row("Cartesian 20x20", MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0));
  row("Cartesian 40x40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  row("Cartesian 80x80", MeshGeometry::createCartesian2D(80, 80, 1.0, 1.0));
  row("Cartesian 3D 16x16x16", MeshGeometry::createCartesian3D(16, 16, 16, 1.0, 1.0, 1.0));
  row("graded 40x40 r=1.15",
      MeshGeometry::createGraded2D(
          40, 40, 1.0, 1.0,
          mesh::AxisGrading{mesh::GradingType::Geometric, 1.15, mesh::GradingCluster::Both},
          mesh::AxisGrading{mesh::GradingType::Geometric, 1.15, mesh::GradingCluster::Both}));
  return 0;
}
