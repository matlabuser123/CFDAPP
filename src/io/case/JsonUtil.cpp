#include "JsonUtil.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace cfd::io::detail {

using nlohmann::json;

nlohmann::json readJsonFile(const std::filesystem::path& path) {
  std::error_code exists_ec;
  if (!std::filesystem::exists(path, exists_ec) || exists_ec) {
    throw IOError("required file not found: " + path.string());
  }
  std::ifstream in(path);
  if (!in) {
    throw IOError("could not open file: " + path.string());
  }
  try {
    json parsed;
    in >> parsed;
    return parsed;
  } catch (const json::exception& e) {
    // Rewrap rather than let a raw nlohmann::json exception (with its own
    // "[json.exception.*] ..." formatting) reach the CLI user (TODO.md
    // P1 section 22). Catches json::exception -- nlohmann::json's common
    // base for every exception its own parser can throw -- not just
    // parse_error: a syntactically well-formed but numerically
    // out-of-range literal (e.g. "1e400", which overflows a double)
    // throws json::out_of_range during parsing itself, a sibling of
    // parse_error, not a subtype of it (discovered via Restart-D's own
    // "reject a non-finite value" testing -- P2 -- Restart capability).
    throw IOError("malformed JSON in " + path.string() + ": " + e.what());
  }
}

namespace {

std::string fieldLabel(std::string_view field) {
  return field.empty() ? std::string() : ("field \"" + std::string(field) + "\" ");
}

}  // namespace

void throwConfigError(const std::filesystem::path& path, std::string_view field,
                      std::string_view constraint, const std::string& received) {
  std::ostringstream message;
  message << path.string() << ": " << fieldLabel(field) << "must satisfy " << constraint
          << "; received " << received;
  throw CaseConfigurationError(message.str());
}

std::string describeJsonValue(const nlohmann::json& value) {
  return value.is_null() ? std::string("missing") : value.dump();
}

void requireObject(const nlohmann::json& node, const std::filesystem::path& path,
                   std::string_view field) {
  if (!node.is_object()) {
    throwConfigError(path, field, "be a JSON object", describeJsonValue(node));
  }
}

void rejectUnknownKeys(const nlohmann::json& node, const std::filesystem::path& path,
                       std::string_view context, const std::vector<std::string_view>& allowed) {
  for (const auto& [key, value] : node.items()) {
    // Bind a named reference to the structured-binding element before
    // capturing it -- some compilers still reject capturing a structured
    // binding itself by reference even under C++20 (CWG 2352's
    // relaxation is unevenly supported), so this is the portable idiom
    // rather than relying on it.
    const std::string& keyRef = key;
    const bool known = std::any_of(allowed.begin(), allowed.end(),
                                   [&keyRef](std::string_view name) { return name == keyRef; });
    if (!known) {
      throwConfigError(path, key, std::string("be a recognized field of ") + std::string(context),
                       std::string("unknown field"));
    }
  }
}

namespace {
std::string_view resolveLabel(std::string_view field, std::string_view label) {
  return label.empty() ? field : label;
}
}  // namespace

void requireField(const nlohmann::json& node, const std::filesystem::path& path,
                  std::string_view field, std::string_view label) {
  if (!node.contains(field)) {
    throwConfigError(path, resolveLabel(field, label), "be present",
                     describeJsonValue(nlohmann::json()));
  }
}

std::string getRequiredString(const nlohmann::json& node, const std::filesystem::path& path,
                              std::string_view field, std::string_view label) {
  requireField(node, path, field, label);
  const auto& value = node.at(field);
  if (!value.is_string()) {
    throwConfigError(path, resolveLabel(field, label), "be a string", describeJsonValue(value));
  }
  return value.get<std::string>();
}

Real getRequiredReal(const nlohmann::json& node, const std::filesystem::path& path,
                     std::string_view field, std::string_view label) {
  requireField(node, path, field, label);
  const auto& value = node.at(field);
  // Reject "1.0" (string), true/false, arrays, etc. -- only a JSON number
  // is an acceptable Real (TODO.md P1 section 40).
  if (!value.is_number()) {
    throwConfigError(path, resolveLabel(field, label), "be a number", describeJsonValue(value));
  }
  const Real real = value.get<Real>();
  if (!std::isfinite(real)) {
    throwConfigError(path, resolveLabel(field, label), "be finite", describeJsonValue(value));
  }
  return real;
}

Index getRequiredIndex(const nlohmann::json& node, const std::filesystem::path& path,
                       std::string_view field, std::string_view label) {
  requireField(node, path, field, label);
  const auto& value = node.at(field);
  // is_number_integer() is false for 20.5 (a JSON real), so this rejects
  // fractional grid dimensions rather than silently truncating them
  // (section 40).
  if (!value.is_number_integer() || value.get<long long>() < 0) {
    throwConfigError(path, resolveLabel(field, label), "be a non-negative integer",
                     describeJsonValue(value));
  }
  return value.get<Index>();
}

std::string getOptionalString(const nlohmann::json& node, const std::filesystem::path& path,
                              std::string_view field, std::string fallback,
                              std::string_view label) {
  if (!node.contains(field)) return fallback;
  return getRequiredString(node, path, field, label);
}

Real getOptionalReal(const nlohmann::json& node, const std::filesystem::path& path,
                     std::string_view field, Real fallback, std::string_view label) {
  if (!node.contains(field)) return fallback;
  return getRequiredReal(node, path, field, label);
}

int getOptionalInt(const nlohmann::json& node, const std::filesystem::path& path,
                   std::string_view field, int fallback, std::string_view label) {
  if (!node.contains(field)) return fallback;
  const auto& value = node.at(field);
  if (!value.is_number_integer()) {
    throwConfigError(path, resolveLabel(field, label), "be an integer", describeJsonValue(value));
  }
  return value.get<int>();
}

std::array<Real, 2> getRequiredVector2(const nlohmann::json& node,
                                       const std::filesystem::path& path, std::string_view field,
                                       std::string_view label) {
  requireField(node, path, field, label);
  const auto& value = node.at(field);
  if (!value.is_array() || value.size() != 2) {
    throwConfigError(path, resolveLabel(field, label), "be an array of exactly 2 numbers",
                     describeJsonValue(value));
  }
  std::array<Real, 2> result{};
  for (std::size_t i = 0; i < 2; ++i) {
    if (!value[i].is_number()) {
      throwConfigError(path, resolveLabel(field, label), "have numeric components",
                       describeJsonValue(value));
    }
    result.at(i) = value[i].get<Real>();
    if (!std::isfinite(result.at(i))) {
      throwConfigError(path, resolveLabel(field, label), "have finite components",
                       describeJsonValue(value));
    }
  }
  return result;
}

}  // namespace cfd::io::detail
