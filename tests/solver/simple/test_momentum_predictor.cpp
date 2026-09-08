#include <gtest/gtest.h>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/pressure_velocity/UnderRelaxation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::pressure_velocity::applyImplicitUnderRelaxation;

namespace {

// A tiny, hand-built 2x2 "momentum-shaped" system: aP=4 diagonal, aN=-1
// off-diagonal coupling, matching the diffusion sign convention
// (TODO.md P0 -- Incompressible Physics section 10). Not derived from
// any mesh/physics -- this is purely the algebraic relaxation formula
// under test (TODO.md P0 -- SIMPLE section 52).
SparseMatrixBuilder makeSampleBuilder() {
  SparseMatrixBuilder builder(2, 2);
  builder.add(0, 0, 4.0);
  builder.add(0, 1, -1.0);
  builder.add(1, 1, 5.0);
  builder.add(1, 0, -1.0);
  return builder;
}

}  // namespace

TEST(MomentumPredictorRelaxationTest, RelaxedDiagonalMatchesAPOverAlphaExactly) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector previousValue{1.0, 2.0};
  const Real alpha = 0.7;

  applyImplicitUnderRelaxation(builder, rhs, previousValue, alpha);
  const auto matrix = builder.build();

  EXPECT_NEAR(matrix.diagonal(0), 4.0 / alpha, 1e-12);
  EXPECT_NEAR(matrix.diagonal(1), 5.0 / alpha, 1e-12);
}

TEST(MomentumPredictorRelaxationTest, RelaxedSourceMatchesDerivationExactly) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector previousValue{1.0, 2.0};
  const Real alpha = 0.7;

  // b_relaxed = b + (1-alpha)/alpha * aP * phiOld
  const Real expected0 = 10.0 + ((1.0 - alpha) / alpha) * 4.0 * 1.0;
  const Real expected1 = 20.0 + ((1.0 - alpha) / alpha) * 5.0 * 2.0;

  applyImplicitUnderRelaxation(builder, rhs, previousValue, alpha);

  EXPECT_NEAR(rhs[0], expected0, 1e-12);
  EXPECT_NEAR(rhs[1], expected1, 1e-12);
}

TEST(MomentumPredictorRelaxationTest, OffDiagonalCoefficientsAreUnchanged) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector previousValue{1.0, 2.0};

  applyImplicitUnderRelaxation(builder, rhs, previousValue, 0.5);
  const auto matrix = builder.build();

  Vector e1(2, 0.0);
  e1[1] = 1.0;
  Vector e0(2, 0.0);
  e0[0] = 1.0;
  EXPECT_NEAR(matrix.multiply(e1)[0], -1.0, 1e-12);  // A(0,1) untouched
  EXPECT_NEAR(matrix.multiply(e0)[1], -1.0, 1e-12);  // A(1,0) untouched
}

TEST(MomentumPredictorRelaxationTest, AlphaOneIsAnExactNoOp) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector previousValue{1.0, 2.0};

  applyImplicitUnderRelaxation(builder, rhs, previousValue, 1.0);
  const auto matrix = builder.build();

  EXPECT_EQ(matrix.diagonal(0), 4.0);
  EXPECT_EQ(matrix.diagonal(1), 5.0);
  EXPECT_EQ(rhs[0], 10.0);
  EXPECT_EQ(rhs[1], 20.0);
}

TEST(MomentumPredictorRelaxationTest, RejectsAlphaOutOfRange) {
  {
    SparseMatrixBuilder builder = makeSampleBuilder();
    Vector rhs{10.0, 20.0};
    const Vector previousValue{1.0, 2.0};
    EXPECT_THROW(applyImplicitUnderRelaxation(builder, rhs, previousValue, 0.0),
                 InvalidArgumentError);
  }
  {
    SparseMatrixBuilder builder = makeSampleBuilder();
    Vector rhs{10.0, 20.0};
    const Vector previousValue{1.0, 2.0};
    EXPECT_THROW(applyImplicitUnderRelaxation(builder, rhs, previousValue, -0.5),
                 InvalidArgumentError);
  }
  {
    SparseMatrixBuilder builder = makeSampleBuilder();
    Vector rhs{10.0, 20.0};
    const Vector previousValue{1.0, 2.0};
    EXPECT_THROW(applyImplicitUnderRelaxation(builder, rhs, previousValue, 1.5),
                 InvalidArgumentError);
  }
}

TEST(MomentumPredictorRelaxationTest, RejectsMismatchedPreviousValueSize) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0, 20.0};
  const Vector previousValue{1.0};  // wrong size
  EXPECT_THROW(applyImplicitUnderRelaxation(builder, rhs, previousValue, 0.5),
               InvalidArgumentError);
}

TEST(MomentumPredictorRelaxationTest, RejectsMismatchedRhsSize) {
  SparseMatrixBuilder builder = makeSampleBuilder();
  Vector rhs{10.0};  // wrong size
  const Vector previousValue{1.0, 2.0};
  EXPECT_THROW(applyImplicitUnderRelaxation(builder, rhs, previousValue, 0.5),
               InvalidArgumentError);
}
