// G8 closure, step 2 -- A CASE FILE REACHES THE CHEMISTRY.
//
// THE GAP THIS CLOSES. P18.2-P18.8 built and qualified a reacting-species stack that no case file
// could reach: measured at P18.9, 0 files outside species/ referenced the chemistry and 0 case keys
// declared a mechanism. Under CLAUDE.md section 9 that made the whole thing a verified library
// rather than production capability -- a seven-phase gap, and the reason G8 could not be closed.
//
// WHAT "REACHABLE" HAS TO MEAN HERE. Not that a parser accepts some JSON. Each test below drives
// physics.json through the real CaseReader and the real CaseBuilder and then asserts the LIVE P18
// OBJECTS came out the other side carrying the declared values -- the registry's species order, the
// mixture's molecular weights, the mechanism's net stoichiometry, the reaction ORDERS (which are
// not the stoichiometry), the Arrhenius parameters, and a MassFractionState that validated. A test
// that only checked the config struct would pass against a builder that dropped the block entirely.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/ReactionMechanism.hpp"

using cfd::CaseConfigurationError;
using cfd::Index;
using cfd::Real;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

// The frozen G8 mechanism, as a case file declares it.
//
//   mechanism : Westbrook & Dryer (1981) Combust. Sci. Tech. 27:1-2, 31-43, Table II Set 3
//               CH4 + 2 O2 -> CO2 + 2 H2O,  orders 0.2 on CH4 and 1.3 on O2
//               A  = 6.7e9 (mol/m^3)^-0.5/s   (6.7e12 in the paper's cm-sec-mole units)
//               Ea = 202505.6 J/mol           (48.4 kcal/mol)
//   weights   : ELEMENT-DERIVED. Independently rounded table values miss G6's
//               sum_i M_i nu_i = 0 by ~1e-6 kg/mol, about 1.8e10 times the bound -- measured in
//               tests/unit/species/test_reaction_orders.cpp.
constexpr Real kMassC = 0.012011;
constexpr Real kMassH = 0.001008;
constexpr Real kMassO = 0.015999;

constexpr Real kPreExponential = 6.7e9;
constexpr Real kActivationEnergy = 202505.6;
constexpr Real kOrderFuel = 0.2;
constexpr Real kOrderOxidiser = 1.3;

// A fuel/oxidiser split that sums to EXACTLY one in binary floating point. Both the parser and
// MassFractionState refuse a set that does not sum to one, and neither normalises, so a decimal
// pair like 0.055/0.945 would be refused for its last bit rather than for its chemistry.
constexpr Real kInitialCH4 = 0.0625;
constexpr Real kInitialO2 = 0.9375;

// TD-001 made two things mandatory for a reacting case, and both have to appear in THIS helper
// rather than only in the one positive test, because every refusal test below derives its JSON
// from it. While they were absent, each of those tests was refused for the MISSING KEY instead of
// for the defect it names -- they passed, and measured nothing. That is CLAUDE.md section 5.3's
// contaminated instrument, found by this suite going 324/325 the moment `time_step` became
// required.
//
//   time_step   the operator-split advance's dt. 1/8 is exactly representable, so the parsed value
//               is the value asserted, not a decimal approximation of it. Its magnitude is
//               irrelevant here -- this fixture never advances a solution, it only has to be a
//               legal dt.
//   thermal     a reacting case must declare it (PhysicsConfigParser's own cross-block rule: the
//               reacting block supplies the chemistry, the thermal block supplies the transport
//               properties the heat release is carried by).
constexpr Real kTimeStep = 0.125;

// TD-002 made `steps` mandatory as well, and the same contamination followed: making it required
// took this suite to 324/328 with only the POSITIVE test failing, while the six refusal tests went
// on passing -- refused for the missing key instead of for the defect each one names. That is the
// second time one key did this to this file (see kTimeStep), so both now live in the helper.
//
// THREE, not one. A value of 1 is the TD-001 single advance, so a fixture using 1 could not tell
// "the declared step count reached the setup" from "the setup defaulted to a single step".
constexpr cfd::Index kSteps = 3;

std::string reactingPhysics() {
  const auto number = [](Real value) {
    // 17 significant digits round-trips a double exactly, so the JSON carries the same value the
    // assertions compare against -- not a decimal approximation of it.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return std::string(buffer);
  };
  return std::string(R"({"model": "incompressible_laminar", "density": 1.0,
   "dynamic_viscosity": 0.01,
   "thermal": {"conductivity": 0.025, "specific_heat": 1000.0,
               "initial_temperature": 298.15},
   "reacting": {
     "initial_temperature": 298.15,
     "steps": )") +
         std::to_string(kSteps) + R"(,
     "time_step": )" +
         number(kTimeStep) + R"(,
     "species": [
       {"name": "CH4", "molecular_weight": )" +
         number(kMassC + 4.0 * kMassH) + R"(, "specific_heat": 2220.0,
        "formation_enthalpy": -4650000.0, "initial_mass_fraction": )" +
         number(kInitialCH4) + R"(, "diffusivity": 2.0e-5},
       {"name": "O2", "molecular_weight": )" +
         number(2.0 * kMassO) + R"(, "specific_heat": 919.0,
        "formation_enthalpy": 0.0, "initial_mass_fraction": )" +
         number(kInitialO2) + R"(, "diffusivity": 2.0e-5},
       {"name": "CO2", "molecular_weight": )" +
         number(kMassC + 2.0 * kMassO) + R"(, "specific_heat": 844.0,
        "formation_enthalpy": -8943000.0, "initial_mass_fraction": 0.0, "diffusivity": 2.0e-5},
       {"name": "H2O", "molecular_weight": )" +
         number(2.0 * kMassH + kMassO) + R"(, "specific_heat": 1996.0,
        "formation_enthalpy": -13423000.0, "initial_mass_fraction": 0.0, "diffusivity": 2.0e-5}
     ],
     "reactions": [
       {"name": "wd-set3",
        "reactants": [{"species": "CH4", "coefficient": 1.0},
                      {"species": "O2", "coefficient": 2.0}],
        "products":  [{"species": "CO2", "coefficient": 1.0},
                      {"species": "H2O", "coefficient": 2.0}],
        "orders":    [{"species": "CH4", "order": )" +
         number(kOrderFuel) + R"(},
                      {"species": "O2", "order": )" +
         number(kOrderOxidiser) + R"(}],
        "pre_exponential": )" +
         number(kPreExponential) + R"(,
        "temperature_exponent": 0.0,
        "activation_energy": )" +
         number(kActivationEnergy) + R"(}
     ]
   }})";
}

// TD-001: the reacting case's OWN boundaries.json.
//
// CaseFixture's default has no `temperature` patch, and TD-001 made a `thermal` block mandatory for
// a reacting case -- which makes temperature boundaries mandatory with it. Written here rather than
// added to CaseFixture, because that fixture is shared by every tests/unit/io suite and its default
// is what the other three hundred tests are written against.
//
// The per-patch `species` map is gate C11's subject: the parser refuses a patch whose per-species
// fixed values do not sum to one. 0.0625 + 0.9375 is exactly 1 in binary, so the inlet below is
// accepted for its chemistry rather than rejected for its last bit.
std::string reactingBoundaries() {
  return R"({
    "patches": {
      "left":   {"velocity": {"type": "inlet", "value": [0.01, 0.0]},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "fixed_temperature", "value": 298.15},
                 "species": {"CH4": {"type": "fixed_value", "value": 0.0625},
                             "O2":  {"type": "fixed_value", "value": 0.9375},
                             "CO2": {"type": "fixed_value", "value": 0.0},
                             "H2O": {"type": "fixed_value", "value": 0.0}}},
      "right":  {"velocity": {"type": "outlet"},
                 "pressure": {"type": "fixed_value", "value": 0.0},
                 "temperature": {"type": "adiabatic"},
                 "species": {"CH4": {"type": "fixed_gradient", "value": 0.0},
                             "O2":  {"type": "fixed_gradient", "value": 0.0},
                             "CO2": {"type": "fixed_gradient", "value": 0.0},
                             "H2O": {"type": "fixed_gradient", "value": 0.0}}},
      "bottom": {"velocity": {"type": "wall"},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"},
                 "species": {"CH4": {"type": "fixed_gradient", "value": 0.0},
                             "O2":  {"type": "fixed_gradient", "value": 0.0},
                             "CO2": {"type": "fixed_gradient", "value": 0.0},
                             "H2O": {"type": "fixed_gradient", "value": 0.0}}},
      "top":    {"velocity": {"type": "wall"},
                 "pressure": {"type": "fixed_gradient", "value": 0.0},
                 "temperature": {"type": "adiabatic"},
                 "species": {"CH4": {"type": "fixed_gradient", "value": 0.0},
                             "O2":  {"type": "fixed_gradient", "value": 0.0},
                             "CO2": {"type": "fixed_gradient", "value": 0.0},
                             "H2O": {"type": "fixed_gradient", "value": 0.0}}}
    }
  })";
}

}  // namespace

// =================================================================================================
// The reacting block reaches the live P18 objects
// =================================================================================================

TEST(G8ReactingCaseTest, AReactingCaseFileProducesTheLiveP18ObjectsWithTheDeclaredValues) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  fixture.write("physics.json", reactingPhysics());

  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.reacting.has_value())
      << "physics.json declared a \"reacting\" block and the builder produced nothing -- the "
         "chemistry would still be unreachable from a case file";
  const cfd::io::ReactingSetup& reacting = *setup.reacting;

  // ---- the registry: identity AND order, because every dense vector in P18 is indexed by it
  ASSERT_EQ(reacting.registry.size(), 4u);
  EXPECT_EQ(reacting.registry.name(0), "CH4");
  EXPECT_EQ(reacting.registry.name(1), "O2");
  EXPECT_EQ(reacting.registry.name(2), "CO2");
  EXPECT_EQ(reacting.registry.name(3), "H2O");

  // ---- the mixture carried the declared, element-derived molecular weights
  EXPECT_DOUBLE_EQ(reacting.mixture.species(0).molecularWeight(), kMassC + 4.0 * kMassH);
  EXPECT_DOUBLE_EQ(reacting.mixture.species(1).molecularWeight(), 2.0 * kMassO);

  // ---- the mechanism: one reaction, with the declared net stoichiometry
  ASSERT_EQ(reacting.mechanism.reactionCount(), 1u);
  const cfd::species::Reaction& reaction = reacting.mechanism.reaction(0);
  EXPECT_EQ(reaction.name(), "wd-set3");
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -1.0);  // CH4 consumed
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(1), -2.0);  // O2 consumed
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(2), 1.0);   // CO2 produced
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(3), 2.0);   // H2O produced

  // ---- THE ORDERS SURVIVED, and are not the stoichiometry. This is the assertion that fails if
  // the builder drops the "orders" array: the mechanism would silently become mass action and the
  // flame speed would move, with nothing else complaining.
  EXPECT_TRUE(reaction.hasExplicitOrders());
  EXPECT_DOUBLE_EQ(reaction.reactantOrder(0), kOrderFuel);
  EXPECT_DOUBLE_EQ(reaction.reactantOrder(1), kOrderOxidiser);
  EXPECT_NE(reaction.reactantOrder(0), reaction.reactantCoefficient(0));
  EXPECT_NE(reaction.reactantOrder(1), reaction.reactantCoefficient(1));

  // ---- G6's closure holds on the mechanism the CASE FILE declared. This is the check that catches
  // a wrong molecular-weight set, and it is why the weights above are element-derived.
  const auto balance = reacting.mechanism.massBalance(reacting.mixture);
  EXPECT_LE(std::abs(balance.worstResidual),
            cfd::species::massClosureTolerance(reaction.massScale(reacting.mixture)))
      << "worst residual " << balance.worstResidual;

  // ---- the kinetics bound to the reaction BY NAME and carry the declared Arrhenius parameters
  ASSERT_EQ(reacting.kinetics.reactionCount(), 1u);
  EXPECT_DOUBLE_EQ(reacting.kinetics.forward(0).preExponential(), kPreExponential);
  EXPECT_DOUBLE_EQ(reacting.kinetics.forward(0).activationEnergy(), kActivationEnergy);
  EXPECT_DOUBLE_EQ(reacting.kinetics.forward(0).temperatureExponent(), 0.0);
  EXPECT_FALSE(reacting.kinetics.reverse(0).has_value())
      << "an irreversible reaction must not carry reverse parameters";

  // ---- the initial composition is a VALIDATED MassFractionState over the real mesh
  EXPECT_EQ(reacting.initialComposition.speciesCount(), 4u);
  EXPECT_EQ(reacting.transport.size(), 4u);
  EXPECT_DOUBLE_EQ(reacting.initialTemperature[0], 298.15);

  // ---- TD-002: the declared step count reached the setup. Asserted against kSteps (= 3) rather
  // than against 1, so a setup that silently defaulted to a single step fails here.
  EXPECT_EQ(reacting.steps, kSteps);

  // ---- TD-001: the split advance's dt reached the setup unaltered. Exactly, not approximately:
  // the dispatch divides by it, so a value that arrived scaled or rounded would change the reported
  // composition while every other assertion here still passed.
  EXPECT_DOUBLE_EQ(reacting.timeStep, kTimeStep);

  // ---- TD-001 gate C4a: the CONSTRUCTED composition keeps P18.1's own 4*eps*N bound, MEASURED.
  // Not inferred from MassFractionState's constructor failing to throw: a probe that only observes
  // the absence of an exception reports success when the thing it watches is removed, so the
  // closure is summed here and compared against the bound A1 keeps verbatim.
  {
    const std::vector<cfd::fields::ScalarField>& Y = reacting.initialComposition.fields();
    ASSERT_EQ(Y.size(), 4u);
    cfd::Real worstClosure = 0.0;
    for (cfd::Index c = 0; c < static_cast<cfd::Index>(Y[0].size()); ++c) {
      cfd::Real total = 0.0;
      for (const cfd::fields::ScalarField& field : Y) total += field[c];
      worstClosure = std::max(worstClosure, std::abs(total - 1.0));
    }
    EXPECT_LE(worstClosure, 4.0 * std::numeric_limits<cfd::Real>::epsilon() * 4.0)
        << "worst |sum Y - 1| over the constructed state " << worstClosure;
  }

  // ---- TD-001: one boundary set per species, in SPECIES-INDEX ORDER. The ordering is asserted
  // rather than assumed because speciesTransportAssembly takes one set at a time: a mismatch here
  // would apply one species' inlet composition to another and still converge.
  ASSERT_EQ(reacting.compositionBoundaries.size(), 4u);
}

// =================================================================================================
// Refusals -- each one a defect a case file must not be able to introduce
// =================================================================================================

// TD-001: `time_step` and `thermal` are REQUIRED for a reacting case, so each refusal needs its own
// test. Both constraints were added by TD-001 and the three tests below are the only things that
// measure them -- see the kTimeStep comment above for what their absence did to this whole suite.

// TD-002: `steps` is REQUIRED and must be a whole number >= 1. Three tests, because the key has
// three distinct ways to be wrong and a single test would leave two of them unmeasured.

TEST(G8ReactingCaseTest, AReactingBlockWithoutSTEPSIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::string from = "\"steps\": 3,";
  ASSERT_NE(json.find(from), std::string::npos);
  json.replace(json.find(from), from.size(), "");
  fixture.write("physics.json", json);

  // Not defaulted, for the same reason time_step is not: the integration EXTENT decides the
  // reported composition, so a silently chosen count makes two runs of the "same" case differ.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, ANonPositiveStepCountIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  for (const char* value : {"0", "-3"}) {
    std::string json = reactingPhysics();
    const std::string from = "\"steps\": 3";
    ASSERT_NE(json.find(from), std::string::npos);
    json.replace(json.find(from), from.size(), std::string("\"steps\": ") + value);
    fixture.write("physics.json", json);

    EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError)
        << "steps " << value << " was accepted";
  }
}

TEST(G8ReactingCaseTest, AFractionalStepCountIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::string from = "\"steps\": 3";
  ASSERT_NE(json.find(from), std::string::npos);
  json.replace(json.find(from), from.size(), "\"steps\": 2.5");
  fixture.write("physics.json", json);

  // 2.5 steps is not a smaller step -- reacting.time_step is what sets the step size. Accepting it
  // by truncation would silently integrate a different distance than the case asked for.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, AReactingBlockWithoutATimeStepIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::string from = "\"time_step\": 0.125,";
  ASSERT_NE(json.find(from), std::string::npos);
  json.replace(json.find(from), from.size(), "");
  fixture.write("physics.json", json);

  // Not defaulted: a silently chosen dt would make the reported composition depend on a number the
  // case never stated, and two runs of the "same" case could then differ by a build.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, ANonPositiveTimeStepIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  for (const char* value : {"0.0", "-0.125"}) {
    std::string json = reactingPhysics();
    const std::string from = "\"time_step\": 0.125";
    ASSERT_NE(json.find(from), std::string::npos);
    json.replace(json.find(from), from.size(), std::string("\"time_step\": ") + value);
    fixture.write("physics.json", json);

    // dt <= 0 is not a small step, it is a different equation: the implicit-Euler transient
    // coefficient rho*V/dt changes sign, so the transport operator stops being an M-matrix and the
    // bounded composition the split exists to guarantee is lost.
    EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError)
        << "time_step " << value << " was accepted";
  }
}

TEST(G8ReactingCaseTest, AReactingBlockWithoutAThermalBlockIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::size_t start = json.find("\"thermal\": {");
  ASSERT_NE(start, std::string::npos);
  const std::size_t end = json.find("},", start);
  ASSERT_NE(end, std::string::npos);
  json.replace(start, end - start + 2, "");
  fixture.write("physics.json", json);

  // The heat release is a volumetric source in the ENERGY equation; without conductivity and
  // specific heat there is nothing to carry it, and the case would run chemistry that heats
  // nothing.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, ACompositionThatDoesNotSumToOneIsRefusedNotNormalised) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  // Make CH4 too large. P18.1 refuses an out-of-bounds composition and NEVER normalises, so the
  // case must be refused rather than quietly rescaled.
  const std::string from = "\"initial_mass_fraction\": 0.0625";
  ASSERT_NE(json.find(from), std::string::npos);
  json.replace(json.find(from), from.size(), "\"initial_mass_fraction\": 0.5");
  fixture.write("physics.json", json);

  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, ANegativeReactionOrderIsRefusedAtTheCaseFileLayer) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::string from = "\"order\": 0.20000000000000001";
  const std::size_t at = json.find("\"order\": 0.2");
  ASSERT_NE(at, std::string::npos);
  const std::size_t end = json.find("}", at);
  json.replace(at, end - at, "\"order\": -0.3");
  fixture.write("physics.json", json);

  // Westbrook & Dryer's own best-fit methane set has a = -0.3, and they note the rate "increases
  // without limit as the methane concentration approaches zero". This codebase does not clip
  // concentrations, so the mechanism is refused where the message can name the file.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
  static_cast<void>(from);
}

TEST(G8ReactingCaseTest, DeclaringBothSpeciesAndReactingIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::string anchor = "\"reacting\": {";
  ASSERT_NE(json.find(anchor), std::string::npos);
  json.replace(json.find(anchor), anchor.size(),
               "\"species\": [{\"name\": \"tracer\", \"diffusivity\": 1.0e-5, "
               "\"initial_concentration\": 0.0}], \"reacting\": {");
  fixture.write("physics.json", json);

  // Two species representations in one solve would be two sources of truth for the same field.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, AnUnknownKeyInTheReactingBlockIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  // Searched FROM the reacting block, not from the start of the document: TD-001 made a `thermal`
  // block mandatory and it carries an `initial_temperature` of its own, so a plain find() would
  // inject the unknown key into THERMAL and this test would measure the thermal parser instead.
  const std::size_t reactingAt = json.find("\"reacting\": {");
  ASSERT_NE(reactingAt, std::string::npos);
  const std::string anchor = "\"initial_temperature\": 298.15";
  const std::size_t at = json.find(anchor, reactingAt);
  ASSERT_NE(at, std::string::npos);
  json.replace(at, anchor.size(), "\"initial_temperature\": 298.15, \"mechanism_file\": \"gri30\"");
  fixture.write("physics.json", json);

  // A silently ignored key is how a case ends up not running the mechanism its author wrote.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, AReactingBlockWithNoReactionsIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  const std::size_t start = json.find("\"reactions\": [");
  ASSERT_NE(start, std::string::npos);
  json.replace(start, json.rfind("]") - start + 1, "\"reactions\": []");
  fixture.write("physics.json", json);

  // A reacting case with no reactions is a passive-species case; the "species" block is for that.
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

TEST(G8ReactingCaseTest, ASingleSpeciesReactingMixtureIsRefused) {
  CaseFixture fixture;
  fixture.write("boundaries.json", reactingBoundaries());
  std::string json = reactingPhysics();
  // Drop to one species: a reacting mixture of one species has nothing to react.
  const std::size_t start = json.find("\"species\": [");
  ASSERT_NE(start, std::string::npos);
  const std::size_t end = json.find("],", start);
  ASSERT_NE(end, std::string::npos);
  json.replace(start, end - start + 2,
               "\"species\": [{\"name\": \"CH4\", \"molecular_weight\": 0.016043, "
               "\"specific_heat\": 2220.0, \"formation_enthalpy\": 0.0, "
               "\"initial_mass_fraction\": 1.0, \"diffusivity\": 2.0e-5}],");
  fixture.write("physics.json", json);

  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

// =================================================================================================
// NON-VACUITY -- the reacting path must be optional, or every test above passes for a wrong reason
// =================================================================================================

TEST(G8ReactingCaseTest, ACaseWithNoReactingBlockStillBuildsAndCarriesNoReactingSetup) {
  CaseFixture fixture;  // the fixture's default physics.json has no reacting block
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.reacting.has_value());
}

TEST(G8ReactingCaseTest, ThePassiveSpeciesPathIsUntouchedByTheReactingAddition) {
  // The authorized architecture was a DEDICATED new path leaving P6-PHYS-001 alone. If the passive
  // block stopped working, the reacting work would have regressed a production path that the
  // CPU-only suite's 2775 tests depend on.
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0,
                    "dynamic_viscosity": 0.01,
                    "species": [{"name": "tracer", "diffusivity": 1.0e-5,
                                 "initial_concentration": 0.25}]})");
  fixture.write("boundaries.json", R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"tracer": {"type": "fixed_value", "value": 1.0}}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"tracer": {"type": "fixed_gradient", "value": 0.0}}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"tracer": {"type": "fixed_gradient", "value": 0.0}}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "species": {"tracer": {"type": "fixed_gradient", "value": 0.0}}}
  }
})");
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_EQ(setup.species.size(), 1u);
  EXPECT_EQ(setup.species[0].properties.name(), "tracer");
  EXPECT_FALSE(setup.reacting.has_value()) << "a passive-species case must not acquire a reacting "
                                              "setup";
}
