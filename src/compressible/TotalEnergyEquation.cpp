#include "cfd/compressible/TotalEnergyEquation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

namespace cfd::compressible {

using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

void requireCellCount(const cfd::mesh::Mesh& mesh, std::size_t size, const char* what,
                      const char* caller) {
  if (size != mesh.numberOfCells()) {
    throw InvalidArgumentError(std::string(caller) + ": " + what +
                               " size does not match mesh cell count");
  }
}

void requireFaceCount(const cfd::mesh::Mesh& mesh, std::size_t size, const char* what,
                      const char* caller) {
  if (size != mesh.numberOfFaces()) {
    throw InvalidArgumentError(std::string(caller) + ": " + what +
                               " size does not match mesh face count");
  }
}

void requirePositiveFiniteTimeStep(Real dt, const char* caller) {
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError(std::string(caller) + ": timeStep must be finite and > 0");
  }
}

Real sumField(const cfd::mesh::Mesh& mesh, const ScalarField* field) {
  if (field == nullptr) return 0.0;
  Real total = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) total += (*field)[i];
  return total;
}

}  // namespace

bool TotalEnergyBalance::withinBound() const noexcept { return std::abs(imbalance) <= bound; }

ScalarField totalEnergyConvectiveDivergence(const cfd::mesh::Mesh& mesh,
                                            const SurfaceField& massFlux,
                                            const ScalarField& specificTotalEnthalpy,
                                            const SurfaceField& hTotalBoundary) {
  constexpr const char* kCaller = "totalEnergyConvectiveDivergence";
  requireCellCount(mesh, specificTotalEnthalpy.size(), "specificTotalEnthalpy", kCaller);
  requireFaceCount(mesh, massFlux.size(), "massFlux", kCaller);
  requireFaceCount(mesh, hTotalBoundary.size(), "hTotalBoundary", kCaller);

  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    if (!std::isfinite(massFlux[f])) {
      throw InvalidArgumentError(std::string(kCaller) + ": face " + std::to_string(f) +
                                 ": massFlux must be finite");
    }
  }
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (!std::isfinite(specificTotalEnthalpy[i])) {
      throw InvalidArgumentError(std::string(kCaller) + ": cell " + std::to_string(i) +
                                 ": specificTotalEnthalpy must be finite");
    }
  }

  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& face : mesh.faces()) {
    const Index f = face.id();
    const Real mdot = massFlux[f];
    const Index owner = face.owner();

    if (face.isBoundary()) {
      // Upwind on the flux direction: mdot > 0 leaves the domain and carries the owner's state,
      // mdot < 0 enters and carries the prescribed boundary state. Same convention the P17.3 mass
      // flux applies to density.
      const Real hFace = (mdot >= 0.0) ? specificTotalEnthalpy[owner] : hTotalBoundary[f];
      if (!std::isfinite(hFace)) {
        throw InvalidArgumentError(std::string(kCaller) + ": face " + std::to_string(f) +
                                   ": hTotalBoundary must be finite on an inflow face");
      }
      result[owner] += mdot * hFace;
      continue;
    }

    // ONE evaluation per internal face, added to the owner and subtracted from the neighbour. This
    // is what makes the operator globally conservative: the two contributions are the same
    // floating-point value with opposite signs, so they cancel exactly in a global sum regardless
    // of the flux distribution.
    const Index neighbour = *face.neighbor();
    const Real hFace =
        (mdot >= 0.0) ? specificTotalEnthalpy[owner] : specificTotalEnthalpy[neighbour];
    const Real flux = mdot * hFace;
    result[owner] += flux;
    result[neighbour] -= flux;
  }
  return result;
}

ScalarField totalEnergyConductiveDivergence(
    const cfd::mesh::Mesh& mesh, const ScalarField& temperature, Real conductivity,
    const cfd::boundary::BoundaryConditionSet& temperatureBoundaries) {
  constexpr const char* kCaller = "totalEnergyConductiveDivergence";
  requireCellCount(mesh, temperature.size(), "temperature", kCaller);
  if (!std::isfinite(conductivity) || !(conductivity > 0.0)) {
    throw InvalidArgumentError(std::string(kCaller) + ": conductivity must be finite and > 0");
  }

  // WHY THIS IS NOT discretization::diffusion, which was the first implementation.
  //
  // That operator's boundary branch fits a QUADRATIC through (boundary, owner, opposite-neighbour)
  // whenever an opposite interior face exists, to recover second-order accuracy at the wall. The
  // consequence, measured: with FixedGradient(0.0) -- a prescribed ZERO gradient -- it returns a
  // NON-ZERO boundary heat flux, because the quadratic's derivative at the face is not zero for a
  // curved profile. An "adiabatic" closed domain built on it leaked 5.64 W and drifted 1.15e-9
  // relative over 20 steps, which is 2.4e15 eps and so plainly not round-off. See
  // logs/04_diagnose_g2.log and AMENDMENT_conduction_boundary.md.
  //
  // P15's thermal assembler deliberately does the opposite for a gradient-type condition: it
  // assembles the PRESCRIBED flux exactly (its own header: "The prescribed flux is assembled
  // exactly (zero for Adiabatic)"). For an energy CONSERVATION statement that is the required
  // behaviour -- an adiabatic wall must carry exactly zero energy, not a second-order estimate of
  // zero. So this term is built from P15's own per-face helpers.
  //
  // Sign: q = -k grad T, and this function returns the OUTFLOW, so the conduction leaving the owner
  // through a face is Df (T_P - T_b) = diagonal * T_P - source. Written once here, and verified
  // against an analytical QUADRATIC temperature in the tests -- a linear field would give exactly
  // zero and so could not test the sign at all (criterion E5).
  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& face : mesh.faces()) {
    const Index owner = face.owner();

    if (face.isBoundary()) {
      const Real distance =
          cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
      const cfd::discretization::FaceDiffusionTerms terms =
          cfd::discretization::boundaryFaceDiffusionTerms(mesh, face, conductivity, distance,
                                                          /*gradPhi=*/nullptr,
                                                          /*prescribedValue=*/false);
      const cfd::thermal::BoundaryDiffusionContribution contribution =
          cfd::thermal::boundaryDiffusionContribution(mesh, face, temperature,
                                                      temperatureBoundaries, terms.coefficient);
      // Gradient-type condition: diagonal = 0 and source = Df g d, so the outflow is -Df g d --
      // EXACTLY zero for g = 0. That exactness is the whole point of using this helper.
      result[owner] += contribution.diagonal * temperature[owner] - contribution.source;
      continue;
    }

    const Index neighbour = *face.neighbor();
    const Real distance = cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face);
    const cfd::discretization::FaceDiffusionTerms terms =
        cfd::discretization::internalFaceDiffusionTerms(mesh, face, conductivity, distance,
                                                        /*gradPhi=*/nullptr);
    // ONE evaluation per internal face, added to the owner and subtracted from the neighbour: the
    // same floating-point value with opposite signs, so internal faces cancel exactly in a global
    // sum. This is what makes the term conservative.
    const Real flux = terms.coefficient * (temperature[owner] - temperature[neighbour]);
    result[owner] += flux;
    result[neighbour] -= flux;
  }
  return result;
}

ScalarField totalEnergyAccumulation(const cfd::mesh::Mesh& mesh, const ScalarField& rhoEOld,
                                    const ScalarField& rhoENew, Real timeStep) {
  constexpr const char* kCaller = "totalEnergyAccumulation";
  requireCellCount(mesh, rhoEOld.size(), "rhoEOld", kCaller);
  requireCellCount(mesh, rhoENew.size(), "rhoENew", kCaller);
  requirePositiveFiniteTimeStep(timeStep, kCaller);

  ScalarField result(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    if (!std::isfinite(rhoEOld[i]) || !std::isfinite(rhoENew[i])) {
      throw InvalidArgumentError(std::string(kCaller) + ": cell " + std::to_string(i) +
                                 ": rhoE must be finite");
    }
    // (rhoE_new - rhoE_old) V / dt. The two conserved states are subtracted directly -- NOT
    // rho * (eTotal_new - eTotal_old), which is a different term whenever density changes.
    result[i] = (rhoENew[i] - rhoEOld[i]) * cell.volume() / timeStep;
  }
  return result;
}

ScalarField bodyForcePowerField(const cfd::mesh::Mesh& mesh, const ScalarField& density,
                                const VectorField& velocity, Vector2 bodyAcceleration) {
  constexpr const char* kCaller = "bodyForcePowerField";
  requireCellCount(mesh, density.size(), "density", kCaller);
  requireCellCount(mesh, velocity.size(), "velocity", kCaller);
  if (!std::isfinite(bodyAcceleration.x) || !std::isfinite(bodyAcceleration.y) ||
      !std::isfinite(bodyAcceleration.z)) {
    throw InvalidArgumentError(std::string(kCaller) + ": bodyAcceleration must be finite");
  }

  ScalarField result(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    // rho (f . U) V. With U = 0 this is exactly 0, because dot() of a zero vector is exactly 0.
    result[i] = density[i] * dot(bodyAcceleration, velocity[i]) * cell.volume();
  }
  return result;
}

TotalEnergyBalance evaluateTotalEnergyBalance(const cfd::mesh::Mesh& mesh,
                                              const ScalarField& accumulation,
                                              const ScalarField& convectiveDivergence,
                                              const ScalarField& conductiveDivergence,
                                              const ScalarField* viscousWork,
                                              const ScalarField* bodyForcePower,
                                              const ScalarField* volumetricSource) {
  constexpr const char* kCaller = "evaluateTotalEnergyBalance";
  requireCellCount(mesh, accumulation.size(), "accumulation", kCaller);
  requireCellCount(mesh, convectiveDivergence.size(), "convectiveDivergence", kCaller);
  requireCellCount(mesh, conductiveDivergence.size(), "conductiveDivergence", kCaller);

  TotalEnergyBalance out;
  out.accumulation = sumField(mesh, &accumulation);
  out.convectiveOutflow = sumField(mesh, &convectiveDivergence);
  out.conductiveOutflow = sumField(mesh, &conductiveDivergence);
  out.viscousWorkInflow = sumField(mesh, viscousWork);
  out.bodyForcePower = sumField(mesh, bodyForcePower);
  out.volumetricSource = sumField(mesh, volumetricSource);

  out.imbalance = out.accumulation + out.convectiveOutflow + out.conductiveOutflow -
                  out.viscousWorkInflow - out.bodyForcePower - out.volumetricSource;

  // The bound scales with the LARGEST TERM, not with the imbalance: a bound derived from the
  // imbalance itself would shrink to nothing whenever the terms cancelled, which is exactly when a
  // conservation check needs to stay honest. N is the cell count, so the bound also scales with the
  // mesh.
  out.scale = std::max({std::abs(out.accumulation), std::abs(out.convectiveOutflow),
                        std::abs(out.conductiveOutflow), std::abs(out.viscousWorkInflow),
                        std::abs(out.bodyForcePower), std::abs(out.volumetricSource)});
  out.bound =
      kEnergyAccumulationFactor * kEps * static_cast<Real>(mesh.numberOfCells()) * out.scale;
  return out;
}

ScalarField advanceTotalEnergyExplicit(const cfd::mesh::Mesh& mesh, const ScalarField& rhoEOld,
                                       const ScalarField& convectiveDivergence,
                                       const ScalarField& conductiveDivergence,
                                       const ScalarField* viscousWork,
                                       const ScalarField* bodyForcePower,
                                       const ScalarField* volumetricSource, Real timeStep) {
  constexpr const char* kCaller = "advanceTotalEnergyExplicit";
  requireCellCount(mesh, rhoEOld.size(), "rhoEOld", kCaller);
  requireCellCount(mesh, convectiveDivergence.size(), "convectiveDivergence", kCaller);
  requireCellCount(mesh, conductiveDivergence.size(), "conductiveDivergence", kCaller);
  requirePositiveFiniteTimeStep(timeStep, kCaller);
  if (viscousWork != nullptr) requireCellCount(mesh, viscousWork->size(), "viscousWork", kCaller);
  if (bodyForcePower != nullptr)
    requireCellCount(mesh, bodyForcePower->size(), "bodyForcePower", kCaller);
  if (volumetricSource != nullptr)
    requireCellCount(mesh, volumetricSource->size(), "volumetricSource", kCaller);

  ScalarField result(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    const Real viscous = (viscousWork != nullptr) ? (*viscousWork)[i] : 0.0;
    const Real body = (bodyForcePower != nullptr) ? (*bodyForcePower)[i] : 0.0;
    const Real source = (volumetricSource != nullptr) ? (*volumetricSource)[i] : 0.0;
    const Real net = convectiveDivergence[i] + conductiveDivergence[i] - viscous - body - source;
    result[i] = rhoEOld[i] - timeStep * net / cell.volume();
    if (!std::isfinite(result[i])) {
      throw NumericalError(std::string(kCaller) + ": cell " + std::to_string(i) +
                           ": advanced rhoE is not finite");
    }
  }
  return result;
}

ViscousWorkLimitation viscousWorkLimitation() noexcept {
  return ViscousWorkLimitation{
      false,
      "P17.4 assembles mu lap(U) plus an optional -(2/3) mu grad(div U); it does not build the "
      "stress TENSOR tau_ij, so the conservative face flux (tau . U) . n cannot be formed exactly "
      "from it. Substituting tau : grad(U) would silently switch to the INTERNAL-energy form of "
      "the equation. Callers therefore supply viscous work explicitly or pass null, and a null "
      "term is reported as exactly 0 rather than folded into the imbalance."};
}

}  // namespace cfd::compressible
