#pragma once

// P13-001 -- Restart / checkpointing. A deterministic fingerprint of a case
// directory's numerically meaningful configuration, so a checkpoint can refuse
// to be resumed into a case that is no longer the case it came from.
//
// The mesh fingerprint (cfd::mesh::computeMeshFingerprint) already answers
// "is this the same mesh". It cannot answer "is this the same problem": two
// runs can share a mesh and differ in viscosity, in a boundary value, in the
// convection scheme or in the relaxation factors -- every one of which makes
// the stored velocity/pressure/face-flux state mean something different.
//
// WHAT IS HASHED, and why this is hashed from the FILES rather than from the
// parsed CaseDefinition: a struct-by-struct hash has to be extended by hand
// every time a config field is added, and the failure mode of forgetting is
// silent -- the check keeps passing while no longer checking the new field.
// Hashing the case's own JSON documents is complete by construction: any
// change to any configured value changes the fingerprint, including fields
// added after this file was written.
//
// Hashed, in this order:
//   case.json        canonically re-serialized, with "name" and "description"
//                    removed -- they are documentation, and a typo fix in a
//                    description must not invalidate a running job's
//                    checkpoint. Everything else stays, including
//                    "initial_conditions" and the file references.
//   geometry.json    whole document
//   mesh.json        whole document
//   physics.json     whole document
//   boundaries.json  whole document
//   solver.json      whole document, with "max_iterations" removed
//
// "max_iterations" is excluded deliberately and it is the only numerical key
// excluded. It is a BUDGET, not part of the problem definition: it does not
// change what the stored state means, nor the trajectory of any iteration
// that follows. Raising it to let a job finish is the ordinary reason to
// resume, and refusing that would make the feature useless. Every other
// solver key -- tolerances, relaxation, schemes, correction passes, backend,
// robustness -- does change the trajectory or the convergence decision, and
// is hashed.
//
// Each document is PARSED and re-dumped before hashing, so whitespace,
// key order and numeric formatting do not affect the result; only content
// does. The paths of the five referenced files come from case.json itself,
// resolved exactly the way CaseReader resolves them.
//
// Same FNV-1a-over-raw-bits construction and the same non-cryptographic,
// same-endianness caveat as cfd::mesh::computeMeshFingerprint.

#include <filesystem>
#include <string>

namespace cfd::io {

// Throws cfd::IOError if `caseDirectory` or a referenced file is missing or
// is not well-formed JSON, and cfd::CaseConfigurationError if case.json is
// not an object or a file reference escapes the case directory -- the same
// conditions, and the same exception types, CaseReader itself raises for
// those inputs.
[[nodiscard]] std::string computeCaseFingerprint(const std::filesystem::path& caseDirectory);

}  // namespace cfd::io
