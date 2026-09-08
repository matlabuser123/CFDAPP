#include "cfd/core/Version.hpp"

namespace cfd::core {

std::string_view projectName() noexcept { return kProjectName; }

std::string versionString() {
  std::string result = std::to_string(kVersionMajor) + "." + std::to_string(kVersionMinor) + "." +
                       std::to_string(kVersionPatch);
  if (!kVersionSuffix.empty()) {
    result += "-";
    result += kVersionSuffix;
  }
  return result;
}

}  // namespace cfd::core
