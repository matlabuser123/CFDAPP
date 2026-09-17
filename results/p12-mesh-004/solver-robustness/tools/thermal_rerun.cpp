// P12-MESH-004 solver robustness step 7: the previously failing thermal cases, rerun unchanged
// through the campaign's production path (MeshQualityCampaign::run: CaseWriter -> CaseReader ->
// CaseBuilder -> SIMPLE -> ThermalSolver with production settings). Usage: thermal_rerun <case>
#include <cstdio>
#include <string>

#include "MeshQualityCampaign.hpp"

using namespace cfd;
using namespace cfd::test::meshq;

int main(int argc, char** argv) {
  const std::string which = argv[1];
  io::MeshConfig mesh;
  if (which == "smooth_n32" || which == "D1_Q2")
    mesh = smoothDistortedMesh(32, 0.05);
  else if (which == "smooth_n64")
    mesh = smoothDistortedMesh(64, 0.05);
  else if (which == "D2_Q4")
    mesh = roughDistortedMesh(32, 0.30);
  else
    return 2;
  const Run r = run("/tmp/m4probe/rerun_" + which, caseDefinition(mesh, Discretization{}));
  std::printf("%s\n", describe(which, r).c_str());
  std::printf(
      "%s: SIMPLE %s (%zu it); thermal %s after %zu outer iterations; last linear solve %.4e -> "
      "%.4e;"
      " T L2 %.6e; energy imbalance %.3e; mass imbalance %.3e\n",
      which.c_str(), r.status.c_str(), static_cast<std::size_t>(r.iterations),
      r.thermalStatus.c_str(), static_cast<std::size_t>(r.thermalIterations),
      r.thermalLinearInitial, r.thermalLinearFinal, r.temperature.l2, r.energyImbalance,
      r.massImbalance);
  return r.thermalConverged ? 0 : 1;
}
