#pragma once

// P5 -- GUI Visualization Integration: the one "solver results -> C++
// visualization/result model" bridge (see the task's own architecture
// diagram) both a live, just-completed run and a reloaded, previously-
// written results/ directory populate identically -- SimulationController
// (apps/gui/) never distinguishes "live" from "reloaded" data past this
// point, and neither does any cfd::viz algorithm consuming it (all of
// which already operate on plain coordinate/value arrays or a Mesh, see
// include/cfd/viz/*.hpp). This is what makes "open a completed case and
// post-process it without rerunning the solver" (section 9) and "watch
// a live run's fields once it finishes" the same code path.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::app {

// Every array below is in cell-id order (0..numberOfCells-1) -- the same
// order cfd::viz's mesh-based and raw-array functions both already
// assume. `valid` is false (every other field left default/empty) for
// "nothing to show yet" -- never a throw; a GUI checks this once rather
// than wrapping every access in a try/catch.
struct VisualizationSnapshot {
  bool valid{false};

  Index nx{};
  Index ny{};
  std::vector<Vector2> points;  // per-cell coordinates.

  std::vector<Real> pressure;
  std::vector<Real> velocityX;
  std::vector<Real> velocityY;
  std::vector<Real> velocityMagnitude;
  std::optional<std::vector<Real>> temperature;  // present iff the case is thermal-enabled.

  // The solver's own canonical per-iteration history (section 8/27 --
  // never a second, GUI-computed residual), 1..N in iteration order.
  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityResidualHistory;

  // The scalar fields a GUI field selector can list/plot, in a fixed,
  // stable order -- "pressure", "velocity_magnitude", and (iff
  // `temperature` is set) "temperature". Never includes velocity_x/
  // velocity_y individually (those are exposed only as a vector field,
  // for the vector-plot view, matching VTKWriter's own "velocity is a
  // real vector, not two scalars to recombine" precedent).
  [[nodiscard]] std::vector<std::string> availableScalarFields() const;

  // nullptr if `name` is not one of availableScalarFields()'s own
  // values.
  [[nodiscard]] const std::vector<Real>* scalarField(const std::string& name) const;

  // The four residual series (section 8's own always-present set; a
  // thermal/species/turbulence case would add more once those are
  // wired into ProjectRunner's dispatch -- not yet, see TODO.md).
  [[nodiscard]] std::vector<std::string> availableResidualSeries() const;
  [[nodiscard]] const std::vector<Real>* residualSeries(const std::string& name) const;
};

// Built directly from a completed (or partially-completed) live run --
// no file I/O. `valid` is true iff `run.mesh`/`run.simpleResult` are
// both set (i.e. the case at least reached a solve attempt) and every
// exported velocity/pressure value is finite -- the same "only a fully-
// finite solution is worth showing" policy ResultExporter's own field-
// file gating already applies (never plot a NaN field).
[[nodiscard]] VisualizationSnapshot buildSnapshot(const ProjectRunResult& run);

// Loaded from a previously-written results/ directory (metadata.json +
// fields.csv + residuals.csv, ResultExporter's own fixed filenames) --
// section 9's "without rerunning the solver". `valid` is false (not a
// throw) if any of the three files is missing or unparseable, or if
// fields.csv/residuals.csv are empty -- a results/ directory from a
// failed (non-finite) run legitimately has no fields.csv at all
// (ResultExporter's own policy), which is exactly this "nothing to show
// yet" outcome, not an error.
[[nodiscard]] VisualizationSnapshot loadSnapshotFromResults(
    const std::filesystem::path& resultsDirectory);

}  // namespace cfd::app
