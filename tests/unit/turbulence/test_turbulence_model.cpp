// P2-TURB-001: TurbulenceModel interface tests. A small test-only dummy
// implementation exercises the abstract contract -- no real turbulence
// model exists yet (k-epsilon/k-omega/SST are later tasks), matching
// this task's own "do not implement a real turbulence model just to
// test the interface" instruction.
#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string_view>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::TurbulenceModel;
using cfd::turbulence::validateTurbulentViscosityField;

namespace {

// Uniform, constructor-supplied mu_t; correct() just counts calls, no
// actual physics -- proof the lifecycle hook is callable, nothing more.
class DummyTurbulenceModel final : public TurbulenceModel {
 public:
  DummyTurbulenceModel(const Mesh& mesh, Real value) : turbulentViscosity_(mesh.numberOfCells()) {
    for (Index i = 0; i < turbulentViscosity_.size(); ++i) turbulentViscosity_[i] = value;
    validateTurbulentViscosityField(mesh, turbulentViscosity_);
  }

  [[nodiscard]] std::string_view name() const noexcept override { return "Dummy"; }

  [[nodiscard]] const ScalarField& turbulentViscosity() const override {
    return turbulentViscosity_;
  }

  void correct(const Mesh& /*mesh*/, const VectorField& /*velocity*/,
               const ScalarField& /*pressure*/) override {
    ++correctionCount_;
  }

  [[nodiscard]] Index correctionCount() const noexcept { return correctionCount_; }

 private:
  ScalarField turbulentViscosity_;
  Index correctionCount_{0};
};

}  // namespace

TEST(TurbulenceModelTest, PolymorphicDestructionWorks) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  std::unique_ptr<TurbulenceModel> model = std::make_unique<DummyTurbulenceModel>(mesh, 0.01);
  model.reset();  // must not crash/leak (run under ASan/UBSan in the quality gate).
  SUCCEED();
}

TEST(TurbulenceModelTest, ModelNameIsAccessible) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.0);
  EXPECT_EQ(model.name(), "Dummy");
}

TEST(TurbulenceModelTest, ZeroTurbulentViscosityIsAccepted) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.0);
  ASSERT_EQ(model.turbulentViscosity().size(), mesh.numberOfCells());
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(model.turbulentViscosity()[i], 0.0);
  }
}

TEST(TurbulenceModelTest, PositiveTurbulentViscosityIsAccepted) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.05);
  for (Index i = 0; i < model.turbulentViscosity().size(); ++i) {
    EXPECT_DOUBLE_EQ(model.turbulentViscosity()[i], 0.05);
  }
}

TEST(TurbulenceModelTest, NegativeTurbulentViscosityRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  EXPECT_THROW((DummyTurbulenceModel(mesh, -0.01)), InvalidArgumentError);
}

TEST(TurbulenceModelTest, NonFiniteTurbulentViscosityRejected) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((DummyTurbulenceModel(mesh, nan)), InvalidArgumentError);
  EXPECT_THROW((DummyTurbulenceModel(mesh, inf)), InvalidArgumentError);
}

TEST(TurbulenceModelTest, CorrectIsCallable) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  DummyTurbulenceModel model(mesh, 0.0);
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  EXPECT_EQ(model.correctionCount(), 0u);
  model.correct(mesh, velocity, pressure);
  EXPECT_EQ(model.correctionCount(), 1u);
}

// --- validateTurbulentViscosityField (standalone, not just via the dummy) ---

TEST(ValidateTurbulentViscosityFieldTest, AcceptsCorrectlySizedFiniteNonNegativeField) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField field(mesh.numberOfCells(), 0.02);
  EXPECT_NO_THROW(validateTurbulentViscosityField(mesh, field));
}

TEST(ValidateTurbulentViscosityFieldTest, RejectsMismatchedSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ScalarField field(mesh.numberOfCells() + 1, 0.02);
  EXPECT_THROW((void)validateTurbulentViscosityField(mesh, field), InvalidArgumentError);
}

TEST(ValidateTurbulentViscosityFieldTest, RejectsNegativeEntry) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  ScalarField field(mesh.numberOfCells(), 0.0);
  field[1] = -1e-6;
  EXPECT_THROW((void)validateTurbulentViscosityField(mesh, field), InvalidArgumentError);
}

TEST(ValidateTurbulentViscosityFieldTest, RejectsNonFiniteEntry) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  ScalarField field(mesh.numberOfCells(), 0.0);
  field[2] = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)validateTurbulentViscosityField(mesh, field), InvalidArgumentError);
}

// --- TurbulenceModel::effectiveViscosity ---

TEST(EffectiveViscosityTest, IsSumOfMolecularAndTurbulent) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.05);
  const ScalarField effective = model.effectiveViscosity(0.01);
  ASSERT_EQ(effective.size(), mesh.numberOfCells());
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_DOUBLE_EQ(effective[i], 0.06);
  }
}

TEST(EffectiveViscosityTest, ZeroTurbulentViscosityGivesJustMolecular) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.0);
  const ScalarField effective = model.effectiveViscosity(0.01);
  for (Index i = 0; i < effective.size(); ++i) {
    EXPECT_DOUBLE_EQ(effective[i], 0.01);
  }
}

TEST(EffectiveViscosityTest, RejectsNonPositiveMolecularViscosity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.0);
  EXPECT_THROW((void)model.effectiveViscosity(0.0), InvalidArgumentError);
  EXPECT_THROW((void)model.effectiveViscosity(-0.01), InvalidArgumentError);
}

TEST(EffectiveViscosityTest, RejectsNonFiniteMolecularViscosity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)model.effectiveViscosity(nan), InvalidArgumentError);
  EXPECT_THROW((void)model.effectiveViscosity(inf), InvalidArgumentError);
}

TEST(EffectiveViscosityTest, RepeatedEvaluationIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const DummyTurbulenceModel model(mesh, 0.03);
  const ScalarField a = model.effectiveViscosity(0.01);
  const ScalarField b = model.effectiveViscosity(0.01);
  ASSERT_EQ(a.size(), b.size());
  for (Index i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i], b[i]);
  }
}
