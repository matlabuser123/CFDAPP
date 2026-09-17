// P12-DIFF-002 W5/W6/W9 instrument: run committed cases through the production path and report
// status, outer-iteration count, conservation and a solution checksum, so the same program gives
// directly comparable before/after numbers.
//
// The checksum is a volume-weighted L2 norm of the solution (not a bitwise hash): it detects a
// changed solution while staying meaningful to read.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/CaseReader.hpp"

using namespace cfd;

namespace {

void report(const std::string& directory) {
  try {
    const app::ProjectRunResult run = app::ProjectRunner::run(directory);
    if (!run.mesh.has_value()) {
      std::printf("K   %-44s NO MESH: %s\n", directory.c_str(), run.errorMessage.c_str());
      return;
    }
    const mesh::Mesh& m = *run.mesh;
    if (!run.simpleResult.has_value()) {
      std::printf("K   %-44s cells %6zu NO SIMPLE RESULT (%s)\n", directory.c_str(),
                  static_cast<std::size_t>(m.numberOfCells()), run.errorMessage.c_str());
      return;
    }
    const auto& r = *run.simpleResult;
    Real velocity = 0.0;
    Real pressure = 0.0;
    Real volume = 0.0;
    for (const auto& c : m.cells()) {
      const Real v = c.volume();
      velocity += dot(r.velocity[c.id()], r.velocity[c.id()]) * v;
      pressure += r.pressure[c.id()] * r.pressure[c.id()] * v;
      volume += v;
    }
    std::printf("K   %-44s cells %6zu status %d iterations %6zu | |u|2 %.12e |p|2 %.12e | mass "
                "%.3e\n",
                directory.c_str(), static_cast<std::size_t>(m.numberOfCells()),
                static_cast<int>(run.status), static_cast<std::size_t>(r.iterations),
                std::sqrt(velocity / volume), std::sqrt(pressure / volume),
                r.globalMassImbalance);
  } catch (const std::exception& e) {
    std::printf("K   %-44s SKIPPED: %s\n", directory.c_str(), e.what());
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("# P12-DIFF-002 committed-case run: status, iterations, solution norms, conservation\n");
  std::vector<std::string> cases;
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) cases.emplace_back(argv[i]);
  } else {
    for (const auto& entry : std::filesystem::directory_iterator("cases")) {
      if (entry.is_directory()) cases.push_back(entry.path().generic_string());
    }
    std::sort(cases.begin(), cases.end());
  }
  for (const std::string& c : cases) report(c);
  return 0;
}
