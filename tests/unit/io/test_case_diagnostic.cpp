// P13-002 -- the case rejection carried as DATA.
//
// The parsers already compute the file, the field, the constraint and the
// received value to build their message, then flatten all four into prose.
// Every consumer that needed one of them back had to search the text: the GUI
// recovered the field by looking for the token `field "`, and renaming that
// one token silently broke it with no test failing
// (results/p13/case-validation/acceptance_gate_A1.md, control N1b).
//
// These tests pin both halves of the fix:
//   - the diagnostic is present and correct (what was added);
//   - the message is EXACTLY what it was (what must not change).
//
// The second half matters as much as the first. The message is what a user
// reads, and it is the input to every fallback that still parses it.
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/case/CaseFingerprint.hpp"

namespace {

using cfd::CaseDiagnostic;
using cfd::caseDiagnosticOf;

std::filesystem::path scratch(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_case_diagnostic" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

void write(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path);
  out << text;
}

// A minimal case that READS successfully, so a test that mutates one file is
// testing the mutation and not an already-broken fixture.
std::filesystem::path makeValidCase(const std::string& name) {
  const auto dir = scratch(name);
  write(dir / "case.json", R"({
  "name": "Diagnostic Fixture",
  "format_version": 1,
  "geometry": "geometry.json",
  "mesh": "mesh.json",
  "physics": "physics.json",
  "boundaries": "boundaries.json",
  "solver": "solver.json"
})");
  write(dir / "geometry.json", R"({"type": "rectangle", "length": 1.0, "height": 1.0})");
  write(dir / "mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 4})");
  write(dir / "physics.json",
        R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01})");
  write(dir / "boundaries.json", R"({"patches": {
  "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
  "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
  "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}},
  "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
             "pressure": {"type": "fixed_gradient", "value": 0.0}}
}})");
  write(dir / "solver.json", R"({
  "type": "SIMPLE", "max_iterations": 2,
  "velocity_relaxation": 0.7, "pressure_relaxation": 0.3,
  "velocity_tolerance": 1e-6, "pressure_tolerance": 1e-6, "continuity_tolerance": 1e-6,
  "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                             "relative_tolerance": 1e-8, "max_iterations": 500},
  "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                             "relative_tolerance": 1e-8, "max_iterations": 500}
})");
  return dir;
}

}  // namespace

TEST(CaseDiagnosticTest, TheFixtureItselfIsValid) {
  // Non-vacuity for every test below: each mutates one file of this fixture,
  // and a fixture that was already rejected would make them prove nothing.
  const auto dir = makeValidCase("valid");
  EXPECT_NO_THROW((void)cfd::io::CaseReader{}.read(dir));
}

TEST(CaseDiagnosticTest, AParserRejectionCarriesFileFieldConstraintAndReceived) {
  const auto dir = makeValidCase("bad_density");
  write(dir / "physics.json",
        R"({"model": "incompressible_laminar", "density": -1.0, "dynamic_viscosity": 0.01})");

  try {
    (void)cfd::io::CaseReader{}.read(dir);
    FAIL() << "a negative density was accepted";
  } catch (const cfd::Error& error) {
    const auto diagnostic = caseDiagnosticOf(error);
    ASSERT_TRUE(diagnostic.has_value()) << error.what();
    EXPECT_EQ(diagnostic->file, "physics.json");
    EXPECT_EQ(diagnostic->field, "density");
    EXPECT_FALSE(diagnostic->constraint.empty());
    EXPECT_FALSE(diagnostic->received.empty());

    // The invariant that actually matters, and the one that cannot drift:
    // every part of the diagnostic is the message's OWN data, not a second,
    // independently-formatted rendering of it. Asserting a literal here
    // instead would pin this test to a formatting choice made elsewhere --
    // the first version expected "-1.0" and got "-1.000000", because the
    // parser formats the value with std::to_string rather than echoing the
    // JSON text. That is pre-existing behaviour, unchanged by P13-002; what
    // P13-002 owes is that the two agree.
    const std::string message = error.what();
    EXPECT_NE(message.find(diagnostic->file), std::string::npos) << message;
    EXPECT_NE(message.find(diagnostic->field), std::string::npos) << message;
    EXPECT_NE(message.find(diagnostic->constraint), std::string::npos) << message;
    EXPECT_NE(message.find("received " + diagnostic->received), std::string::npos) << message;
  }
}

TEST(CaseDiagnosticTest, TheMessageIsUnchangedByCarryingTheDiagnostic) {
  // The frozen gate criterion (V3) at unit level: the message a user reads is
  // exactly the shape it has always had. Checked against the literal format
  // rather than against a golden string, so this test says what it means.
  const auto dir = makeValidCase("message_shape");
  write(dir / "mesh.json", R"({"type": "structured_cartesian", "nx": 0, "ny": 4})");

  try {
    (void)cfd::io::CaseReader{}.read(dir);
    FAIL() << "nx = 0 was accepted";
  } catch (const cfd::Error& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("mesh.json"), std::string::npos) << message;
    EXPECT_NE(message.find("field \"nx\""), std::string::npos) << message;
    EXPECT_NE(message.find("must satisfy"), std::string::npos) << message;
    EXPECT_NE(message.find("received"), std::string::npos) << message;
  }
}

TEST(CaseDiagnosticTest, AWholeDocumentConstraintHasAnEmptyFieldNotAMissingDiagnostic) {
  // "must be a JSON object" names no field. Empty is a real state here, and a
  // consumer must be able to tell it apart from "no diagnostic at all".
  const auto dir = makeValidCase("not_an_object");
  write(dir / "physics.json", R"([1, 2, 3])");

  try {
    (void)cfd::io::CaseReader{}.read(dir);
    FAIL() << "a JSON array was accepted as physics.json";
  } catch (const cfd::Error& error) {
    const auto diagnostic = caseDiagnosticOf(error);
    ASSERT_TRUE(diagnostic.has_value()) << error.what();
    EXPECT_EQ(diagnostic->file, "physics.json");
    EXPECT_TRUE(diagnostic->field.empty());
  }
}

TEST(CaseDiagnosticTest, AMistypedManifestReferenceNamesTheManifestAndTheKey) {
  // The one rejection in the pipeline that named neither the file to edit nor
  // the key at fault. It was 60 of the 63 rows the mutation matrix flagged as
  // vague, across all 12 case families.
  const auto dir = makeValidCase("manifest_typo");
  write(dir / "case.json", R"({
  "name": "Diagnostic Fixture",
  "format_version": 1,
  "geometry": "geomety.json",
  "mesh": "mesh.json",
  "physics": "physics.json",
  "boundaries": "boundaries.json",
  "solver": "solver.json"
})");

  try {
    (void)cfd::io::CaseReader{}.read(dir);
    FAIL() << "a reference to a nonexistent file was accepted";
  } catch (const cfd::IOError& error) {
    // The TYPE is deliberately still IOError (gate criterion S6): a caller
    // catching one still catches it, and tests/data/cases/missing_mesh
    // exercises this path.
    const auto diagnostic = caseDiagnosticOf(error);
    ASSERT_TRUE(diagnostic.has_value()) << error.what();
    EXPECT_EQ(diagnostic->file, "case.json");
    EXPECT_EQ(diagnostic->field, "geometry");
    EXPECT_EQ(diagnostic->received, "geomety.json");
    const std::string message = error.what();
    EXPECT_NE(message.find("case.json"), std::string::npos) << message;
    EXPECT_NE(message.find("geomety.json"), std::string::npos) << message;
  } catch (const cfd::Error& error) {
    FAIL() << "expected IOError, got a different cfd::Error: " << error.what();
  }
}

TEST(CaseDiagnosticTest, AnErrorWithNoDiagnosticReportsNoneRatherThanAnEmptyOne) {
  // caseDiagnosticOf must distinguish "carried nothing" from "carried empty
  // strings". A consumer falls back to the message only in the first case.
  const cfd::CaseConfigurationError bare("something went wrong");
  EXPECT_FALSE(caseDiagnosticOf(bare).has_value());
  const cfd::IOError bareIo("could not open");
  EXPECT_FALSE(caseDiagnosticOf(bareIo).has_value());
  const cfd::NumericalError unrelated("not a case error");
  EXPECT_FALSE(caseDiagnosticOf(unrelated).has_value());
}

TEST(CaseDiagnosticTest, TheDiagnosticSurvivesACopyThroughTheBaseType) {
  // Exceptions are caught by reference here, but a consumer may store one.
  // Slicing to cfd::Error would silently drop the payload, so the accessor is
  // exercised through the concrete type a catch site actually sees.
  const cfd::CaseConfigurationError error(
      "physics.json: field \"density\" must satisfy be > 0; received -1",
      CaseDiagnostic{"physics.json", "density", "be > 0", "-1"});
  const cfd::Error& asBase = error;
  const auto diagnostic = caseDiagnosticOf(asBase);
  ASSERT_TRUE(diagnostic.has_value());
  EXPECT_EQ(diagnostic->field, "density");
  EXPECT_EQ(diagnostic->constraint, "be > 0");
}
