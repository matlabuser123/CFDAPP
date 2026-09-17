// P12-DIFF-002 W9 instrument: run ONE committed case directory (a scratch copy) through the
// production entry point ProjectRunner::run -- the function the CLI and GUI call -- and print the
// solver-level facts the exported fields.csv / metadata.json do not already carry. The exported
// files themselves are compared field by field by w9_compare.py.
#include <cstdio>
#include <string>

#include "cfd/app/ProjectRunner.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: w9_run <case-directory>\n");
    return 2;
  }
  const cfd::app::ProjectRunResult run = cfd::app::ProjectRunner::run(argv[1]);
  std::printf("status %d\n", static_cast<int>(run.status));
  if (!run.errorMessage.empty()) std::printf("error %s\n", run.errorMessage.c_str());
  if (run.simpleResult.has_value()) {
    const auto& r = *run.simpleResult;
    std::printf("simple_status %d\nsimple_iterations %zu\nface_flux %s\n"
                "final_u %.6e\nfinal_v %.6e\nfinal_p %.6e\nfinal_continuity %.6e\n"
                "global_mass_imbalance %.6e\n",
                static_cast<int>(r.status), static_cast<std::size_t>(r.iterations),
                cfd::pressure_velocity::faceFluxSchemeName(r.faceFlux), r.finalUResidual,
                r.finalVResidual, r.finalPressureResidual, r.finalContinuityResidual,
                r.globalMassImbalance);
  }
  if (run.compressibleSimpleResult.has_value()) {
    const auto& c = *run.compressibleSimpleResult;
    std::printf("compressible_simple_iterations %zu\n", static_cast<std::size_t>(c.iterations));
  }
  return run.status == cfd::app::ProjectRunStatus::ApplicationError ? 1 : 0;
}
