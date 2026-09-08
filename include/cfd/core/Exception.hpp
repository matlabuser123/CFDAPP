#pragma once

#include <stdexcept>

namespace cfd {

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
};

}  // namespace cfd
