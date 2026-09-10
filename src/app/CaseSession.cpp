#include "cfd/app/CaseSession.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"

namespace cfd::app {

std::optional<std::filesystem::path> CaseSession::directory() const {
  std::lock_guard<std::mutex> lock(dataMutex_);
  return directory_;
}

std::optional<cfd::io::CaseDefinition> CaseSession::caseDefinition() const {
  std::lock_guard<std::mutex> lock(dataMutex_);
  return caseDefinition_;
}

std::string CaseSession::lastError() const {
  std::lock_guard<std::mutex> lock(dataMutex_);
  return lastError_;
}

std::optional<ProjectRunResult> CaseSession::lastRun() const {
  std::lock_guard<std::mutex> lock(dataMutex_);
  return lastRun_;
}

bool CaseSession::canSave() const noexcept {
  switch (state_.load()) {
    case CaseState::Loaded:
    case CaseState::Modified:
    case CaseState::Validated:
    case CaseState::Completed:
    case CaseState::Failed:
    case CaseState::Cancelled:
      return true;
    case CaseState::Empty:
    case CaseState::Running:
      return false;
  }
  return false;
}

bool CaseSession::canRun() const noexcept {
  switch (state_.load()) {
    case CaseState::Loaded:
    case CaseState::Modified:
    case CaseState::Validated:
    case CaseState::Completed:
    case CaseState::Failed:
    case CaseState::Cancelled:
      return true;
    case CaseState::Empty:
    case CaseState::Running:
      return false;
  }
  return false;
}

void CaseSession::newCase() {
  std::lock_guard<std::mutex> lock(dataMutex_);
  cfd::io::CaseDefinition fresh;
  fresh.caseConfig.name = "Untitled Case";
  fresh.caseConfig.formatVersion = 1;
  fresh.geometry.type = "rectangle";
  fresh.mesh.type = "structured_cartesian";
  fresh.physics.model = "incompressible_laminar";
  fresh.solver.type = "SIMPLE";
  fresh.solver.momentumSolver.type = "BiCGSTAB";
  fresh.solver.pressureSolver.type = "BiCGSTAB";
  caseDefinition_ = std::move(fresh);
  directory_.reset();
  lastRun_.reset();
  lastError_.clear();
  state_.store(CaseState::Loaded);
}

bool CaseSession::open(const std::filesystem::path& caseDirectory) {
  try {
    cfd::io::CaseDefinition definition = cfd::io::CaseReader{}.read(caseDirectory);
    std::lock_guard<std::mutex> lock(dataMutex_);
    caseDefinition_ = std::move(definition);
    directory_ = caseDirectory;
    lastRun_.reset();
    lastError_.clear();
    state_.store(CaseState::Loaded);
    return true;
  } catch (const cfd::Error& e) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = e.what();
    return false;
  }
}

bool CaseSession::reload() {
  const std::optional<std::filesystem::path> dir = directory();
  if (!dir.has_value()) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ =
        "CaseSession::reload: no case directory is set (this case was never opened or saved)";
    return false;
  }
  return open(*dir);
}

bool CaseSession::save() {
  const std::optional<std::filesystem::path> dir = directory();
  if (!dir.has_value()) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = "CaseSession::save: no case directory is set -- use saveAs() first";
    return false;
  }
  return saveAs(*dir);
}

bool CaseSession::saveAs(const std::filesystem::path& caseDirectory) {
  const std::optional<cfd::io::CaseDefinition> definition = caseDefinition();
  if (!definition.has_value()) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = "CaseSession::saveAs: no case is loaded";
    return false;
  }
  try {
    cfd::io::CaseWriter::write(caseDirectory, *definition);
  } catch (const cfd::Error& e) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = e.what();
    return false;
  }
  std::lock_guard<std::mutex> lock(dataMutex_);
  directory_ = caseDirectory;
  return true;
}

void CaseSession::setCaseDefinition(cfd::io::CaseDefinition definition) {
  std::lock_guard<std::mutex> lock(dataMutex_);
  if (!caseDefinition_.has_value() && state_.load() == CaseState::Empty) {
    // Caller bug (section 6 assumes a case is already loaded before it
    // can be edited) -- left as a documented no-op rather than an
    // exception, since a stray edit callback firing once during teardown
    // should not crash the application.
    return;
  }
  caseDefinition_ = std::move(definition);
  state_.store(CaseState::Modified);
}

bool CaseSession::validate() {
  const std::optional<cfd::io::CaseDefinition> definition = caseDefinition();
  if (!definition.has_value()) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = "CaseSession::validate: no case is loaded";
    return false;
  }
  try {
    // Same production validation SIMPLE's own case-driven run performs
    // (CaseBuilder::build) -- not a second, GUI-only rule set (section
    // 7). The built SimulationSetup is discarded; only whether it built
    // without throwing matters here.
    (void)cfd::io::CaseBuilder{}.build(*definition);
  } catch (const cfd::Error& e) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = e.what();
    return false;
  }
  std::lock_guard<std::mutex> lock(dataMutex_);
  lastError_.clear();
  state_.store(CaseState::Validated);
  return true;
}

ProjectRunResult CaseSession::run(ProjectRunOptions options) {
  const std::optional<std::filesystem::path> dir = directory();
  if (!dir.has_value() || !canRun()) {
    ProjectRunResult failure;
    failure.status = ProjectRunStatus::ApplicationError;
    failure.errorMessage =
        "CaseSession::run: no case directory is set, or a solve is already running";
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastError_ = failure.errorMessage;
    return failure;
  }

  cancelRequested_.store(false);
  state_.store(CaseState::Running);

  // OR's the caller's own cancellationCheck (if any) with this session's
  // requestCancel() flag -- a caller supplying neither still gets a
  // working Stop button via requestCancel() alone.
  const cfd::pressure_velocity::SIMPLECancellationCheck callerCheck = options.cancellationCheck;
  options.cancellationCheck = [this, callerCheck]() {
    return cancelRequested_.load() || (callerCheck && callerCheck());
  };

  const ProjectRunResult result = ProjectRunner::run(*dir, options);

  CaseState nextState = CaseState::Failed;
  switch (result.status) {
    case ProjectRunStatus::Converged:
    case ProjectRunStatus::DidNotConverge:
      nextState = CaseState::Completed;
      break;
    case ProjectRunStatus::Cancelled:
      nextState = CaseState::Cancelled;
      break;
    case ProjectRunStatus::NumericalFailure:
    case ProjectRunStatus::InvalidCase:
    case ProjectRunStatus::ApplicationError:
      nextState = CaseState::Failed;
      break;
  }

  {
    std::lock_guard<std::mutex> lock(dataMutex_);
    lastRun_ = result;
    lastError_ = result.errorMessage;
  }
  state_.store(nextState);
  return result;
}

}  // namespace cfd::app
