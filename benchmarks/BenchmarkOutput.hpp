#pragma once

// P13-007 -- where a benchmark writes its results.
//
// Every benchmark used to hardcode a path relative to the working directory:
//
//   std::filesystem::create_directories("results/performance/baseline");
//
// so where the output landed was decided by the directory the benchmark
// happened to be launched from. Launched from the repository root -- the
// obvious place -- it landed on top of committed evidence: 34 tracked files
// across five directories (results/p13/benchmarks/audit.md, B1/B2).
//
// That is not hypothetical. During P13's own work
// results/validation/grid_convergence/natural_convection_ra1e3.json changed
// underneath the session -- runtime_seconds only, every physical quantity
// identical -- because a run wrote into a tracked path.
//
// The default is unchanged, deliberately: an existing invocation with no
// arguments behaves exactly as it did, so no documented recipe and no
// recorded result becomes wrong. --output-dir is the way to send a run
// somewhere else, and the suite driver always passes it.

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace cfd::benchmarks {

// Parses `--output-dir <path>` out of argv and returns the directory this
// benchmark should write into, creating it.
//
// `defaultRelative` is the benchmark's historical path, e.g.
// "results/performance/baseline", used verbatim when --output-dir is absent.
//
// A missing argument to --output-dir is an error rather than a silent
// fallback to the default: a run told to write elsewhere that quietly wrote
// into the repository is exactly the failure this exists to prevent.
[[nodiscard]] inline std::filesystem::path benchmarkOutputDir(int argc, char** argv,
                                                              std::string_view defaultRelative) {
  std::filesystem::path out{std::string(defaultRelative)};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg != "--output-dir") continue;
    if (i + 1 >= argc) {
      std::cerr << "Error: --output-dir requires a path\n";
      std::exit(2);
    }
    out = std::filesystem::path(argv[++i]);
  }
  std::error_code ec;
  std::filesystem::create_directories(out, ec);
  if (ec) {
    std::cerr << "Error: could not create output directory " << out << ": " << ec.message() << "\n";
    std::exit(2);
  }
  return out;
}

// The same, for the benchmarks whose main() takes no arguments.
[[nodiscard]] inline std::filesystem::path benchmarkOutputDir(std::string_view defaultRelative) {
  return benchmarkOutputDir(0, nullptr, defaultRelative);
}

}  // namespace cfd::benchmarks
