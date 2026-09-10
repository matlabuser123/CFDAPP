// P3-PHYS-004: SpeciesProperties -- identity + constant diffusivity model
// (this task's own section 12: constant diffusivity is the mandatory
// completion target; D=0 is deliberately allowed for pure advection).
#include <gtest/gtest.h>

#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/species/SpeciesProperties.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::species::SpeciesProperties;

TEST(SpeciesPropertiesTest, StoresNameAndDiffusivity) {
  const SpeciesProperties species("tracer", 1.0e-5);
  EXPECT_EQ(species.name(), "tracer");
  EXPECT_DOUBLE_EQ(species.diffusivity(), 1.0e-5);
}

TEST(SpeciesPropertiesTest, AllowsZeroDiffusivityForPureAdvection) {
  const SpeciesProperties species("inert_tracer", 0.0);
  EXPECT_DOUBLE_EQ(species.diffusivity(), 0.0);
}

TEST(SpeciesPropertiesTest, RejectsEmptyName) {
  EXPECT_THROW(SpeciesProperties("", 1.0e-5), InvalidArgumentError);
}

TEST(SpeciesPropertiesTest, RejectsNegativeDiffusivity) {
  EXPECT_THROW(SpeciesProperties("tracer", -1.0e-5), InvalidArgumentError);
}

TEST(SpeciesPropertiesTest, RejectsNonFiniteDiffusivity) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(SpeciesProperties("tracer", nan), InvalidArgumentError);
  EXPECT_THROW(SpeciesProperties("tracer", inf), InvalidArgumentError);
}

TEST(SpeciesPropertiesTest, TwoIndependentSpeciesKeepSeparateIdentityAndDiffusivity) {
  // Section 3's own "multiple independent passive species without solver
  // duplication" requirement, checked at the value-type level: two
  // SpeciesProperties instances never share state.
  const SpeciesProperties speciesA("O2", 2.0e-5);
  const SpeciesProperties speciesB("N2", 1.8e-5);
  EXPECT_NE(speciesA.name(), speciesB.name());
  EXPECT_NE(speciesA.diffusivity(), speciesB.diffusivity());
}
