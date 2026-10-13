// P15 -- the transient energy term.
//
// The audit found ZERO time-derivative references in the energy path: this
// equation was steady only. These tests pin the two things most easily got
// wrong -- that a steady solve is completely unaffected, and that the old-time
// RHS uses the OLD density and specific heat.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::ThermalTimeState;

namespace {

BoundaryConditionSet walls(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

TEST(ThermalTransient, OneCellCoefficientMatchesHandCalculation) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 2.0, 3.0);
  ASSERT_EQ(mesh.numberOfCells(), 1);
  EXPECT_DOUBLE_EQ(mesh.cell(0).volume(), 6.0);

  const ScalarField tOld(1, 350.0);
  const ScalarField rhoOld(1, 2.0), cpOld(1, 100.0);
  const ScalarField rhoNew(1, 4.0), cpNew(1, 500.0);

  ThermalTimeState time;
  time.transient = true;
  time.dt = 0.5;
  time.temperatureOld = &tOld;
  time.densityOld = &rhoOld;
  time.specificHeatOld = &cpOld;
  time.densityNew = &rhoNew;
  time.specificHeatNew = &cpNew;

  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  cfd::thermal::assembleThermalTransientContribution(mesh, time, builder, rhs);

  // a_P = rho^{n+1} cp^{n+1} V / dt = 4 * 500 * 6 / 0.5 = 24000
  // b_P = rho^n cp^n T^n V / dt     = 2 * 100 * 350 * 6 / 0.5 = 840000
  EXPECT_DOUBLE_EQ(builder.build().diagonal(0), 24000.0);
  EXPECT_DOUBLE_EQ(rhs[0], 840000.0);
}

TEST(ThermalTransient, OldTimeRhsUsesOldDensityAndOldSpecificHeat) {
  // The conservation trap. rho^n cp^n = 2*100 = 200; rho^{n+1} cp^{n+1} = 4*500
  // = 2000, a factor of TEN apart, so a level mix-up cannot pass by accident.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField tOld(1, 300.0);
  const ScalarField rhoOld(1, 2.0), cpOld(1, 100.0);
  const ScalarField rhoNew(1, 4.0), cpNew(1, 500.0);

  ThermalTimeState time;
  time.transient = true;
  time.dt = 1.0;
  time.temperatureOld = &tOld;
  time.densityOld = &rhoOld;
  time.specificHeatOld = &cpOld;
  time.densityNew = &rhoNew;
  time.specificHeatNew = &cpNew;

  SparseMatrixBuilder builder(1, 1);
  Vector rhs(1, 0.0);
  cfd::thermal::assembleThermalTransientContribution(mesh, time, builder, rhs);

  EXPECT_DOUBLE_EQ(rhs[0], 2.0 * 100.0 * 300.0) << "must use rho^n cp^n";
  EXPECT_NE(rhs[0], 4.0 * 500.0 * 300.0) << "used the NEW level: not conservative";
}

TEST(ThermalTransient, SteadyStateIsBitwiseUnchanged) {
  // A default (steady) ThermalTimeState must leave the assembled system
  // bit-for-bit what the pre-P15 overload produces. Not "close" -- identical.
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  ScalarField temperature(n, 0.0);
  for (Index i = 0; i < n; ++i) temperature[i] = 300.0 + 0.5 * static_cast<Real>(i);
  const ScalarField conductivity(n, 0.6), specificHeat(n, 1005.0);
  constexpr Real source = 12.0;
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) massFlux[f] = (f % 2 == 0) ? 0.2 : -0.15;

  const auto existing = cfd::thermal::assembleEnergyEquation(
      mesh, temperature, massFlux, conductivity, specificHeat, walls(mesh), source, {});
  const auto withSteadyState =
      cfd::thermal::assembleEnergyEquation(mesh, temperature, massFlux, conductivity, specificHeat,
                                           walls(mesh), source, {}, ThermalTimeState{});

  const auto& a = existing.system.matrix();
  const auto& b = withSteadyState.system.matrix();
  ASSERT_EQ(a.nonZeros(), b.nonZeros());
  for (Index k = 0; k < a.nonZeros(); ++k) {
    EXPECT_EQ(a.valuesData()[k], b.valuesData()[k]) << "value " << k;
    EXPECT_EQ(a.columnIndicesData()[k], b.columnIndicesData()[k]) << "column " << k;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(existing.system.rhs()[i], withSteadyState.system.rhs()[i]) << "rhs " << i;
  }
}

TEST(ThermalTransient, SmallerTimestepStrengthensTheDiagonal) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const ScalarField tOld(n, 300.0), rho(n, 1.0), cp(n, 1000.0);

  const auto diagonalFor = [&](Real dt) {
    ThermalTimeState time;
    time.transient = true;
    time.dt = dt;
    time.temperatureOld = &tOld;
    time.densityOld = &rho;
    time.specificHeatOld = &cp;
    time.densityNew = &rho;
    time.specificHeatNew = &cp;
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    cfd::thermal::assembleThermalTransientContribution(mesh, time, builder, rhs);
    return builder.build().diagonal(0);
  };
  EXPECT_DOUBLE_EQ(diagonalFor(0.01) / diagonalFor(0.1), 10.0);
}

TEST(ThermalTransient, RejectsIncompleteOrInvalidState) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ScalarField field(mesh.numberOfCells(), 1.0);
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  ThermalTimeState missing;
  missing.transient = true;
  missing.dt = 1.0;
  missing.densityOld = &field;
  EXPECT_THROW(cfd::thermal::assembleThermalTransientContribution(mesh, missing, builder, rhs),
               InvalidArgumentError);

  ThermalTimeState badDt;
  badDt.transient = true;
  badDt.dt = -1.0;
  badDt.temperatureOld = &field;
  badDt.densityOld = &field;
  badDt.specificHeatOld = &field;
  badDt.densityNew = &field;
  badDt.specificHeatNew = &field;
  EXPECT_THROW(cfd::thermal::assembleThermalTransientContribution(mesh, badDt, builder, rhs),
               InvalidArgumentError);
}
