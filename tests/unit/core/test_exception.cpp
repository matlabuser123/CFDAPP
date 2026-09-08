#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

#include "cfd/core/Exception.hpp"

static_assert(std::is_base_of_v<std::runtime_error, cfd::Error>);
static_assert(std::is_base_of_v<cfd::Error, cfd::InvalidArgumentError>);
static_assert(std::is_base_of_v<cfd::Error, cfd::NumericalError>);
static_assert(std::is_base_of_v<cfd::NumericalError, cfd::ConvergenceError>);
static_assert(std::is_base_of_v<cfd::Error, cfd::IOError>);

TEST(CoreException, CatchableAsStdRuntimeError) {
  EXPECT_THROW(throw cfd::Error("boom"), std::runtime_error);
}

TEST(CoreException, MessageSurvivesConstruction) {
  try {
    throw cfd::Error("something went wrong");
  } catch (const cfd::Error& e) {
    EXPECT_STREQ(e.what(), "something went wrong");
  }
}

TEST(CoreException, DerivedTypesCatchableAsBase) {
  EXPECT_THROW(throw cfd::InvalidArgumentError("bad arg"), cfd::Error);
  EXPECT_THROW(throw cfd::NumericalError("nan"), cfd::Error);
  EXPECT_THROW(throw cfd::ConvergenceError("no converge"), cfd::Error);
  EXPECT_THROW(throw cfd::IOError("bad file"), cfd::Error);
}

TEST(CoreException, ConvergenceErrorIsCatchableAsNumericalError) {
  EXPECT_THROW(throw cfd::ConvergenceError("no converge"), cfd::NumericalError);
}

TEST(CoreException, CategoriesAreDistinguishable) {
  try {
    throw cfd::NumericalError("nan");
  } catch (const cfd::InvalidArgumentError&) {
    FAIL() << "NumericalError must not be caught as InvalidArgumentError";
  } catch (const cfd::NumericalError&) {
    SUCCEED();
  }
}
