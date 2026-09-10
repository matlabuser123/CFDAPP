#pragma once

// P5-A -- Production Case Manager: the authoritative application-facing
// layer between the UI (CLI or GUI) and the solver backend (TODO.md P5
// section 3). Wraps CaseReader/CaseWriter/CaseBuilder/ProjectRunner --
// the same production case-loading and solver-dispatch pipeline the CLI
// already used before P5, never a second GUI-only path (section 0).
//
// Explicit state machine (section 5), not scattered booleans:
//
//   Empty -> Loaded -> Modified -> Validated -> Running -> Completed
//                                             \          \-> Failed
//                                              \----------> Cancelled
//
// open()/newCase()/reload() always return to Loaded; any edit
// (setCaseDefinition()) moves Loaded/Validated/Completed/Failed/
// Cancelled to Modified; validate() moves Loaded/Modified to Validated
// on success (stays put, with lastError() set, on failure); run() moves
// through Running to Completed/Failed/Cancelled. save()/saveAs() do not
// change state (a saved case may still need re-validating if the
// solver.json on disk was hand-edited since).

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/case/CaseDefinition.hpp"

namespace cfd::app {

enum class CaseState {
  Empty,
  Loaded,
  Modified,
  Validated,
  Running,
  Completed,
  Failed,
  Cancelled,
};

class CaseSession {
 public:
  CaseSession() = default;

  [[nodiscard]] CaseState state() const noexcept { return state_.load(); }
  [[nodiscard]] bool isModified() const noexcept { return state_.load() == CaseState::Modified; }

  // These four return copies under dataMutex_ (not references) precisely
  // so they stay safe to call from the UI thread while run() executes on
  // a worker thread (section 63) -- state()/isModified()/canSave()/
  // canRun()/canStop() stay lock-free (derived from the atomic state_
  // alone) so a UI-thread poll of "is a solve running" never blocks on a
  // solve in progress.
  [[nodiscard]] std::optional<std::filesystem::path> directory() const;
  [[nodiscard]] std::optional<cfd::io::CaseDefinition> caseDefinition() const;
  [[nodiscard]] std::string lastError() const;
  [[nodiscard]] std::optional<ProjectRunResult> lastRun() const;

  // Section 5's "the UI should know whether actions... are currently
  // legal" -- one place computing this, not re-derived ad hoc in QML.
  [[nodiscard]] bool canSave() const noexcept;
  [[nodiscard]] bool canRun() const noexcept;
  [[nodiscard]] bool canStop() const noexcept { return state_.load() == CaseState::Running; }

  // Empty/Loaded/Modified/Validated/Completed/Failed/Cancelled -> Loaded
  // with a fresh, empty case (formatVersion 1, no directory -- Save
  // requires saveAs() first). Never throws.
  void newCase();

  // Reads `caseDirectory` via CaseReader (section 3's "New case, Open
  // case"). On success: -> Loaded, directory()/caseDefinition() set. On
  // failure: state unchanged, lastError() set, returns false -- never
  // throws (a GUI open-file action reports failure through state, not an
  // exception crossing into QML).
  [[nodiscard]] bool open(const std::filesystem::path& caseDirectory);

  // Re-reads from the current directory() (must be set -- i.e. this case
  // was open()ed or already saveAs()'d), discarding any in-memory edits
  // (section 3's "Reload case"). Same success/failure contract as
  // open().
  [[nodiscard]] bool reload();

  // Writes to the current directory() via CaseWriter (must be set).
  // Returns false (lastError() set) if directory() is unset or the write
  // fails -- never throws.
  [[nodiscard]] bool save();

  // Sets directory() to `caseDirectory` and writes there (section 3's
  // "Save As") -- always succeeds unless the write itself fails
  // (IOError caught, false returned, directory() left at its prior
  // value).
  [[nodiscard]] bool saveAs(const std::filesystem::path& caseDirectory);

  // Replaces the in-memory case definition (a GUI editor committing a
  // mesh/physics/BC/solver change, section 6) -- moves to Modified from
  // any other state. Requires a case to already be loaded (Empty stays
  // Empty; this is a caller bug, not reported via lastError()).
  void setCaseDefinition(cfd::io::CaseDefinition definition);

  // Attempts CaseBuilder::build() against the in-memory case definition
  // -- the same production validation the solver itself would run
  // (section 7: "the GUI must call the same production case validation
  // used by CLI execution... validation belongs in the C++ backend").
  // On success: -> Validated, returns true. On failure: state unchanged,
  // lastError() set to CaseBuilder's own message, returns false.
  [[nodiscard]] bool validate();

  // Runs the current directory()'s case via ProjectRunner (must be
  // Loaded, Modified, or Validated -- canRun() reports this). Moves to
  // Running for the duration of the call, then Completed/Failed/
  // Cancelled based on the result. `options.cancellationCheck`, if set,
  // is combined (OR'd) with this session's own requestCancel() flag --
  // pass an empty options to rely solely on requestCancel(). Blocking;
  // a GUI caller runs this on a worker thread (section 12) and polls
  // state()/lastRun() (or supplies options.progressCallback) from the UI
  // thread.
  ProjectRunResult run(ProjectRunOptions options = {});

  // Sets the cooperative-cancellation flag run() consults every outer
  // iteration (section 13). Only meaningful while canStop() is true; a
  // call outside a running solve is a harmless no-op consumed by the
  // next run().
  void requestCancel() noexcept { cancelRequested_.store(true); }

 private:
  std::atomic<CaseState> state_{CaseState::Empty};
  std::atomic<bool> cancelRequested_{false};
  std::optional<std::filesystem::path> directory_;
  std::optional<cfd::io::CaseDefinition> caseDefinition_;
  std::optional<ProjectRunResult> lastRun_;
  std::string lastError_;
  // Guards directory_/caseDefinition_/lastError_/lastRun_ against a
  // concurrent read from the UI thread while run() (on a worker thread)
  // is writing them -- state_/cancelRequested_ are lock-free atomics
  // precisely so a UI-thread canRun()/canStop()/state() poll never blocks
  // on this mutex while a solve is in progress (section 63: "choose the
  // simplest safe model compatible with performance").
  mutable std::mutex dataMutex_;
};

}  // namespace cfd::app
