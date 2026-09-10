// P2-TURB-002: LaminarModel tests -- mu_t must be exactly 0.0 in every
// cell, always, regardless of construction or repeated correct() calls;
// mu_eff (via the inherited, non-virtual
// TurbulenceModel::effectiveViscosity()) must reduce to exactly the
// molecular viscosity.
#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/LaminarModel.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::LaminarModel;

TEST(LaminarModelTest, NameIsLaminar) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const LaminarModel model(mesh);
  EXPECT_EQ(model.name(), "laminar");
}

TEST(LaminarModelTest, TurbulentViscosityIsZeroEverywhere) {
  // Multi-cell mesh (not 1x1) -- catches a partial-initialization
  // mistake a trivially-passing single-cell test would miss.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const LaminarModel model(mesh);
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_EQ(model.turbulentViscosity()[i], 0.0) << "cell " << i;
  }
}

TEST(LaminarModelTest, TurbulentViscositySizeMatchesMesh) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const LaminarModel model(mesh);
  EXPECT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
}

TEST(LaminarModelTest, EffectiveViscosityEqualsMolecularViscosity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const LaminarModel model(mesh);
  // A nontrivial value, not mu=0 -- mu=0 would hide a real bug (e.g. an
  // effectiveViscosity() that silently returned mu_t alone).
  const Real mu = 0.017;
  const ScalarField effective = model.effectiveViscosity(mu);
  ASSERT_EQ(effective.size(), mesh.numberOfCells());
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_EQ(effective[i], mu) << "cell " << i;
  }
}

TEST(LaminarModelTest, CorrectLeavesTurbulentViscosityZero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  LaminarModel model(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{2.0, -1.0});
  const ScalarField pressure(mesh.numberOfCells(), 5.0);

  model.correct(mesh, velocity, pressure);

  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_EQ(model.turbulentViscosity()[i], 0.0) << "cell " << i;
  }
}

TEST(LaminarModelTest, RepeatedCorrectIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  LaminarModel model(mesh);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.5});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);

  for (int k = 0; k < 5; ++k) {
    model.correct(mesh, velocity, pressure);
    for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
      EXPECT_EQ(model.turbulentViscosity()[i], 0.0) << "iteration " << k << " cell " << i;
    }
  }
}

TEST(LaminarModelTest, DifferentFiniteFlowFieldsStillProduceZeroMuT) {
  // The exact sequence the task spec's own worked example describes:
  // construct -> correct(A) -> correct(B) -> correct(A) again -- mu_t
  // must stay exactly 0.0 throughout, never influenced by which flow
  // state correct() was last called with.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  LaminarModel model(mesh);
  const Index n = mesh.numberOfCells();
  const ScalarField pressure(n, 0.0);
  const VectorField flowA(n, Vector2{10.0, 0.0});
  const VectorField flowB(n, Vector2{-3.5, 7.2});

  auto expectAllZero = [&] {
    for (Index i = 0; i < n; ++i) EXPECT_EQ(model.turbulentViscosity()[i], 0.0) << "cell " << i;
  };

  expectAllZero();
  model.correct(mesh, flowA, pressure);
  expectAllZero();
  model.correct(mesh, flowB, pressure);
  expectAllZero();
  model.correct(mesh, flowA, pressure);
  expectAllZero();
}

TEST(LaminarModelTest, ConstructionIsDeterministicAcrossInstances) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const LaminarModel a(mesh);
  const LaminarModel b(mesh);
  ASSERT_EQ(a.turbulentViscosity().size(), b.turbulentViscosity().size());
  for (Index i = 0; i < a.turbulentViscosity().size(); ++i) {
    EXPECT_EQ(a.turbulentViscosity()[i], b.turbulentViscosity()[i]);
  }
}

TEST(LaminarModelTest, EffectiveViscosityRejectsInvalidMolecularViscosity) {
  // Base-class validation (TurbulenceModel::effectiveViscosity), reused
  // here rather than duplicated -- LaminarModel adds nothing of its own.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const LaminarModel model(mesh);
  EXPECT_THROW((void)model.effectiveViscosity(0.0), InvalidArgumentError);
  EXPECT_THROW((void)model.effectiveViscosity(-1.0), InvalidArgumentError);
}

TEST(LaminarModelTest, PolymorphicUseThroughBaseInterfaceWorks) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const cfd::turbulence::TurbulenceModel& base = LaminarModel(mesh);
  EXPECT_EQ(base.name(), "laminar");
  for (Index i = 0; i < base.turbulentViscosity().size(); ++i) {
    EXPECT_EQ(base.turbulentViscosity()[i], 0.0);
  }
}
