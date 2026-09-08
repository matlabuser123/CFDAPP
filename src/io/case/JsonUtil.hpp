#pragma once

// Internal parsing helpers shared by every src/io/case/*.cpp parser.
// Not a public header (not under include/, not installed) -- nlohmann::
// json is deliberately kept out of every public CaseReader/config type,
// per PROJECT_STRUCTURE.md's "the CFD numerical core must not know
// anything about JSON" and TODO.md P1 section 17 (typed config, not raw
// JSON, is what the rest of the codebase sees).
//
// Every throw here is a cfd::IOError (file missing/unreadable/malformed
// JSON syntax) or cfd::CaseConfigurationError (well-formed JSON, invalid
// content), always naming the file and field (TODO.md P1 section 20: bad
// error messages like "Invalid case" are explicitly disallowed).

#include <array>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::io::detail {

// Reads and parses `path` as JSON. Throws IOError if the file does not
// exist or cannot be opened, or if nlohmann::json cannot parse it for
// any reason -- every nlohmann::json::exception (parse_error for
// syntactically malformed JSON, out_of_range for a syntactically valid
// but numerically out-of-range literal such as "1e400", ...) is caught
// and rewrapped rather than left to propagate as a raw third-party
// exception (section 22).
[[nodiscard]] nlohmann::json readJsonFile(const std::filesystem::path& path);

// field is "" for a whole-document constraint (e.g. "must be a JSON
// object"), otherwise the JSON key that failed. Only one overload
// (rather than a convenience const nlohmann::json& one too) -- a JSON
// string literal received value is otherwise ambiguous between
// std::string and nlohmann::json (both accept it implicitly); callers
// with a nlohmann::json value describe it via describeJsonValue() below.
[[noreturn]] void throwConfigError(const std::filesystem::path& path, std::string_view field,
                                   std::string_view constraint, const std::string& received);

// "missing" for a null/absent value, otherwise value.dump().
[[nodiscard]] std::string describeJsonValue(const nlohmann::json& value);

// Throws unless node.is_object().
void requireObject(const nlohmann::json& node, const std::filesystem::path& path,
                   std::string_view field = "");

// Rejects any key in node not listed in allowed (TODO.md P1 section 23:
// unknown fields are a hard error, not silently ignored -- a typo like
// "denisty" must not look like it was accepted).
void rejectUnknownKeys(const nlohmann::json& node, const std::filesystem::path& path,
                       std::string_view context, const std::vector<std::string_view>& allowed);

// Throws if node does not have `field`. `label` (defaults to `field`) is
// what appears in the error message -- callers parsing a nested object
// (e.g. one boundary patch's "velocity" block inside boundaries.json,
// where several patches all have a field literally named "type") pass a
// fully-qualified label such as `patches.top.velocity.type` so the
// message says which occurrence failed, while `field` stays the plain
// key actually looked up in the (already-nested) `node` argument.
void requireField(const nlohmann::json& node, const std::filesystem::path& path,
                  std::string_view field, std::string_view label = {});

// Each getRequired* throws CaseConfigurationError if the field is
// missing, is not the JSON type expected, or (for numeric types) is not
// finite. getRequiredIndex additionally rejects a non-integer JSON number
// (e.g. 20.5 for nx) -- section 40. See requireField above for `label`.
[[nodiscard]] std::string getRequiredString(const nlohmann::json& node,
                                            const std::filesystem::path& path,
                                            std::string_view field, std::string_view label = {});
[[nodiscard]] Real getRequiredReal(const nlohmann::json& node, const std::filesystem::path& path,
                                   std::string_view field, std::string_view label = {});
[[nodiscard]] Index getRequiredIndex(const nlohmann::json& node, const std::filesystem::path& path,
                                     std::string_view field, std::string_view label = {});

// Optional variants: return `fallback` if the field is absent, otherwise
// apply the same validation as the required form.
[[nodiscard]] std::string getOptionalString(const nlohmann::json& node,
                                            const std::filesystem::path& path,
                                            std::string_view field, std::string fallback,
                                            std::string_view label = {});
[[nodiscard]] Real getOptionalReal(const nlohmann::json& node, const std::filesystem::path& path,
                                   std::string_view field, Real fallback,
                                   std::string_view label = {});
[[nodiscard]] int getOptionalInt(const nlohmann::json& node, const std::filesystem::path& path,
                                 std::string_view field, int fallback, std::string_view label = {});

// A required 2-component finite array, e.g. "velocity": [1.0, 0.0].
[[nodiscard]] std::array<Real, 2> getRequiredVector2(const nlohmann::json& node,
                                                     const std::filesystem::path& path,
                                                     std::string_view field,
                                                     std::string_view label = {});

}  // namespace cfd::io::detail
