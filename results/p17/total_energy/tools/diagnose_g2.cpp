// Diagnose the G2 failure: an adiabatic closed domain drifted by 1.15e-9 RELATIVE over 20 steps,
// which is ~5e6 eps and so cannot be round-off. Measure which term fails to sum to zero instead of
// guessing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/compressible/TotalEnergyEquation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::compressible::TotalEnergyState;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 7.0, 5.0);
  const Index n = mesh.numberOfCells();
  const TemperatureDependentProperty cp(std::vector<Real>{1005.0}, 0.0, 3000.0, "cp");
  const TotalEnergyState state(ThermodynamicProperties(287.05, 1005.0),
                               SensibleEnthalpy(cp, 0.0, 0.0));

  ScalarField temperature(n);
  VectorField velocity(n);
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = 320.0 + 15.0 * std::sin(cell.centroid().x);
    velocity[cell.id()] = Vector2{1.5 + 0.2 * cell.centroid().y, 0.0};
  }
  const ScalarField density(n, 1.25);
  ScalarField rhoE =
      cfd::compressible::conservedTotalEnergyField(mesh, density, temperature, velocity, state);

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real f = static_cast<Real>(face.id());
    massFlux[face.id()] = 0.41 * std::sin(0.7 * f) + 0.13 * static_cast<Real>(face.id() % 7);
  }

  BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    bcs.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }

  const auto domainEnergy = [&](const ScalarField& field) {
    Real total = 0.0;
    for (const auto& cell : mesh.cells()) total += field[cell.id()] * cell.volume();
    return total;
  };
  const auto sumOf = [&](const ScalarField& field) {
    Real total = 0.0;
    for (Index i = 0; i < n; ++i) total += field[i];
    return total;
  };

  const Real dt = 1.0e-4;
  const Real initial = domainEnergy(rhoE);
  std::printf("initial domain energy %.17g\n\n", initial);
  std::printf("%-5s %-24s %-24s %-24s %-14s\n", "step", "sum(convOut)", "sum(condOut)",
              "domain energy", "drift so far");

  for (int step = 0; step < 20; ++step) {
    const ScalarField hTotal =
        cfd::compressible::specificTotalEnthalpyField(mesh, temperature, velocity, state);
    const ScalarField convective = cfd::compressible::totalEnergyConvectiveDivergence(
        mesh, massFlux, hTotal, SurfaceField(mesh.numberOfFaces(), 0.0));
    const ScalarField conductive =
        cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, 0.4, bcs);
    const Real sumConv = sumOf(convective);
    const Real sumCond = sumOf(conductive);

    rhoE = cfd::compressible::advanceTotalEnergyExplicit(mesh, rhoE, convective, conductive,
                                                         nullptr, nullptr, nullptr, dt);
    VectorField momentum(n);
    for (Index i = 0; i < n; ++i) {
      momentum[i] = Vector2{density[i] * velocity[i].x, density[i] * velocity[i].y};
    }
    const auto recovered =
        cfd::compressible::recoverPrimitiveFields(mesh, density, momentum, rhoE, state);
    temperature = recovered.temperature;

    if (step < 5 || step == 19) {
      std::printf("%-5d %-24.6g %-24.6g %-24.17g %-14.6g\n", step, sumConv, sumCond,
                  domainEnergy(rhoE), domainEnergy(rhoE) - initial);
    }
  }

  std::printf("\nmax |convective| per cell, and the scale the residue should be judged against:\n");
  const ScalarField hTotal =
      cfd::compressible::specificTotalEnthalpyField(mesh, temperature, velocity, state);
  const ScalarField convective = cfd::compressible::totalEnergyConvectiveDivergence(
      mesh, massFlux, hTotal, SurfaceField(mesh.numberOfFaces(), 0.0));
  Real worst = 0.0;
  for (Index i = 0; i < n; ++i) worst = std::max(worst, std::abs(convective[i]));
  std::printf("  worst per-cell convective outflow %.6g\n", worst);
  std::printf("  sum(convOut)                      %.6g   = %.3g eps of the worst cell\n",
              sumOf(convective), std::abs(sumOf(convective)) / (worst * 2.22e-16));
  const ScalarField conductive =
      cfd::compressible::totalEnergyConductiveDivergence(mesh, temperature, 0.4, bcs);
  Real worstCond = 0.0;
  for (Index i = 0; i < n; ++i) worstCond = std::max(worstCond, std::abs(conductive[i]));
  std::printf("  worst per-cell conductive outflow %.6g\n", worstCond);
  std::printf("  sum(condOut)                      %.6g   = %.3g eps of the worst cell\n",
              sumOf(conductive), std::abs(sumOf(conductive)) / (worstCond * 2.22e-16));
  std::printf("\ndone\n");
  return 0;
}
