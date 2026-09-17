// P12-MESH-007 performance baseline, memory part (measurement only, NOT timed): runs ONE item of
// tools/perf_baseline.cpp per process, so that each process's peak resident set size
// (/usr/bin/time -v, "Maximum resident set size") belongs to that item. The item code is
// perf_baseline.cpp itself, included unchanged; only its main() is renamed away.
//   usage: perf_memory <none|geom2d128|geom2d256|geom3d32|geom3d64|solvers128>
// "solvers128" runs the static PISO and the AlePISO item one after the other (perf_baseline's
// solvers()), so its peak is the larger of the two; "none" is the process floor.
#include <cstring>

#define main perf_baseline_main_unused
#include "perf_baseline.cpp"
#undef main

int main(int argc, char** argv) {
  const char* item = argc > 1 ? argv[1] : "none";
  if (std::strcmp(item, "geom2d128") == 0) {
    geometry("2D 128^2", mesh::MeshGeometry::createCartesian2D(128, 128, 1.0, 1.0),
             Vector3{0.05, 0.05, 0}, 2);
  } else if (std::strcmp(item, "geom2d256") == 0) {
    geometry("2D 256^2", mesh::MeshGeometry::createCartesian2D(256, 256, 1.0, 1.0),
             Vector3{0.05, 0.05, 0}, 2);
  } else if (std::strcmp(item, "geom3d32") == 0) {
    geometry("3D 32^3", mesh::MeshGeometry::createCartesian3D(32, 32, 32, 1.0, 1.0, 1.0),
             Vector3{0.05, 0.025, -0.0375}, 3);
  } else if (std::strcmp(item, "geom3d64") == 0) {
    geometry("3D 64^3", mesh::MeshGeometry::createCartesian3D(64, 64, 64, 1.0, 1.0, 1.0),
             Vector3{0.05, 0.025, -0.0375}, 3);
  } else if (std::strcmp(item, "solvers128") == 0) {
    solvers(128);
  }
  return 0;
}
