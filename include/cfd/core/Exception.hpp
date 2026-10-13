#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace cfd {

// P13-002 -- a case-file rejection, as DATA.
//
// The case parsers already compute all four of these to build their message:
//
//   <file>: field "<field>" must satisfy <constraint>; received <received>
//
// and then flatten them into prose. Every consumer that needs one of them
// back has to search the text for it -- apps/gui/SimulationControllerEditing
// .cpp recovers the field by looking for the token `field "` and the section
// by looking for `mesh.json` and friends. That coupling is invisible, and
// renaming one token in the message silently breaks it: see
// results/p13/case-validation/acceptance_gate_A1.md, control N1b.
//
// So the message stays exactly as it was -- byte for byte, which is a frozen
// gate criterion measured against a 2537-row corpus -- and this rides
// alongside it.
//
// `file` is the case-relative name ("physics.json"), not the absolute path:
// the message already carries the path for the human, and a consumer wants
// the thing it can switch on. `field` is empty for a whole-document
// constraint ("must be a JSON object"), which is a real state and not a
// missing value.
struct CaseDiagnostic {
  std::string file;
  std::string field;
  std::string constraint;
  std::string received;
};

// Base of CFDApp's exception hierarchy. Code that raises an error
// condition specific to CFDApp (as opposed to one from the standard
// library or a third-party dependency) should throw one of the types
// below rather than a bare std::runtime_error, so callers can catch
// cfd::Error to mean specifically "an error CFDApp itself raised", e.g.:
//
//   try {
//     runCase(...);
//   } catch (const cfd::Error& e) {
//     std::cerr << "CFDApp error: " << e.what() << '\n';
//     return EXIT_FAILURE;
//   }
//
// Kept to a small number of meaningful categories -- add a new one only
// when call sites actually need to distinguish it from the others.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// A caller supplied an argument that is structurally invalid (e.g. a mesh
// generator given nx == 0, a mesh with no cells).
class InvalidArgumentError : public Error {
 public:
  using Error::Error;
};

// A numerical computation produced an invalid or unusable result (e.g. a
// non-finite residual).
class NumericalError : public Error {
 public:
  using Error::Error;
};

// A specific NumericalError: an iterative process failed to converge.
class ConvergenceError : public NumericalError {
 public:
  using NumericalError::NumericalError;
};

// Reading or writing a case/result file failed -- the file could not be
// found, opened, or parsed as well-formed syntax (e.g. malformed JSON).
class IOError : public Error {
 public:
  using Error::Error;

  // P13-002: an IOError raised while resolving a case file can carry the same
  // diagnostic. The one place that matters is a mistyped file reference in
  // case.json -- "required geometry configuration not found: <path>". That is
  // a case-configuration mistake reported as an I/O failure, and it stays an
  // IOError (a caller catching one still catches it; tests/data/cases/
  // missing_mesh exercises exactly this path), but there is no reason a
  // consumer should have to care which exception type carried the field.
  IOError(const std::string& message, CaseDiagnostic diagnostic)
      : Error(message), diagnostic_(std::move(diagnostic)) {}

  [[nodiscard]] const std::optional<CaseDiagnostic>& diagnostic() const noexcept {
    return diagnostic_;
  }

 private:
  std::optional<CaseDiagnostic> diagnostic_;
};

// A case file parsed as well-formed JSON but its *content* is invalid --
// a missing required field, a value outside its documented constraint, an
// unsupported type string, a cross-file reference that doesn't resolve
// (e.g. a boundary patch the mesh doesn't have). Distinct from IOError:
// this is "the file said something wrong", not "the file couldn't be
// read at all" (P1 -- Case System section 22).
class CaseConfigurationError : public Error {
 public:
  using Error::Error;

  // P13-002: the structured form of what the message already says. Populated
  // by cfd::io::detail::throwConfigError, which every one of the ~140 case
  // rejection sites routes through -- so they are all instrumented without a
  // single call site changing. Empty for the rejections raised directly
  // (CaseBuilder's construction failures), which is a documented category
  // rather than an oversight: see results/p13/case-validation/summary.md.
  CaseConfigurationError(const std::string& message, CaseDiagnostic diagnostic)
      : Error(message), diagnostic_(std::move(diagnostic)) {}

  [[nodiscard]] const std::optional<CaseDiagnostic>& diagnostic() const noexcept {
    return diagnostic_;
  }

 private:
  std::optional<CaseDiagnostic> diagnostic_;
};

// The diagnostic of `error`, whichever of the two carrying types it is, or
// std::nullopt. One place to ask, so a consumer never needs a chain of
// dynamic_casts and never silently misses a type added later.
[[nodiscard]] inline std::optional<CaseDiagnostic> caseDiagnosticOf(const Error& error) {
  if (const auto* config = dynamic_cast<const CaseConfigurationError*>(&error)) {
    return config->diagnostic();
  }
  if (const auto* io = dynamic_cast<const IOError*>(&error)) {
    return io->diagnostic();
  }
  return std::nullopt;
}

}  // namespace cfd
