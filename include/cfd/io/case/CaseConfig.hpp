#pragma once

#include <string>

namespace cfd::io {

// The parsed case.json manifest, minus its file references (CaseReader
// resolves those directly against the case directory -- they do not need
// to survive as data once every referenced file has been read; see
// CaseReader.hpp).
struct CaseConfig {
  std::string name;
  std::string description;
  // TODO.md P1 section 44: costs nothing now, makes future schema
  // evolution safer. The only currently-supported value is 1.
  int formatVersion{1};
};

}  // namespace cfd::io
