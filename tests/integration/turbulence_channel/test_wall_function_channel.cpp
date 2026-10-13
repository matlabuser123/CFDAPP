// P16 -- the wall-function channel benchmark family.
//
// WHY THIS IS A NEW FILE AND NOT AN EDIT
// --------------------------------------
// test_channel_flow_validation.cpp and its recorded results are the PRE-P16
// baseline, preserved verbatim (results/p16/turbulence/pre_p16_baseline/).
// They gate regression on a 64x16 mesh whose first cell sits at y+ ~ 7.5 -- the
// buffer layer, where a high-Re wall function is not valid. That mesh cannot be
// the acceptance mesh for a wall-function model, so this benchmark uses its own
// family and leaves the old one untouched.
//
// THE MESH FAMILY
// ---------------
// For a channel of half-height delta = 1 at Re_tau = 180,
//
//     y_1^+ = u_tau y_1 / nu = Re_tau * y_1 / delta = 180 * y_1
//
// and a uniform mesh of ny cells over height 2 puts the first centre at
// y_1 = 1/ny. So the PREDICTED first-cell y+ is simply
//
//     y_1^+ ~ 180 / ny
//
// which is why the family below is coarse in the core: ny = 6 is already the
// finest mesh whose first cell reaches y+ = 30. That is a real property of this
// benchmark, not a modelling choice -- at Re_tau = 180 the entire channel is
// only 180 wall units tall, so the wall-function validity band and the outer
// resolution are in direct tension. It is reported rather than worked around.
//
// The family deliberately includes meshes on BOTH sides of the band, because
// the acceptance methodology requires G1 to be shown rejecting as well as
// accepting.
//
// WHERE tau_w COMES FROM, AND WHY IT MATTERS
// ------------------------------------------
// NOT from ChannelFlowValidationUtils::computeWallShearStress. That helper
// forms the laminar secant mu*(U_P - 0)/y_P, and its own comment says so --
// "CFDApp's turbulence models have no velocity wall function", which was true
// when it was written and is not any more. On a wall-function mesh it
// systematically under-reports tau_w, which would make every metric below wrong
// in the flattering direction. The wall shear is taken from the MODEL, which is
// the quantity the momentum equation actually felt.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelFlowValidationUtils.hpp"
#include "ChannelReTau180.hpp"
#include "ChannelReTau590.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/TurbulenceBackendTrace.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

// turbulenceGpuPathQualified() is defined only under cuda/kernels/, compiled
// only when CFDAPP_ENABLE_CUDA=ON, while THIS target builds in both
// configurations. The guard is a link requirement, not a style choice.
#ifdef CFDAPP_TEST_WITH_CUDA
#include "cfd/gpu/DeviceKEpsilon.hpp"
#endif

using cfd::Index;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::turbulence::KEpsilonModel;

namespace {

constexpr Real kChannelHeight = 2.0;
constexpr Real kChannelLength = 16.0;
constexpr Real kDensity = 1.0;
constexpr Real kBulkVelocity = 1.0;
constexpr Index kNx = 64;

// One Reynolds number's worth of channel. Parameterised rather than duplicated:
// the two cases differ only in Re, and a second copy of this harness would be
// two things to keep in step.
struct ChannelCase {
  const char* name;
  Real reTauTarget;
  Real reBulk;
  [[nodiscard]] Real dynamicViscosity() const {
    return kDensity * kBulkVelocity * kChannelHeight / reBulk;
  }
};

// The low-Re STRESS case: preserved, and deliberately at the edge of the wall
// function's validity. ny <= 6 is all the validity band permits here.
constexpr ChannelCase kReTau180{"re_tau_180", cfd::validation::channel_re_tau_180::kReTau,
                                cfd::validation::channel_re_tau_180::kReBulk};

// The PRIMARY accuracy case: ny <= 19 satisfies the band, so validity and core
// resolution are simultaneously achievable. See ChannelReTau590.hpp for why
// this Reynolds number and where it comes from.
constexpr ChannelCase kReTau590{"re_tau_590", cfd::validation::channel_re_tau_590::kReTau,
                                cfd::validation::channel_re_tau_590::kReBulk};

// Inlet turbulence, the same intensity/mixing-length estimates the pre-P16
// harness uses, so the two differ in wall treatment and nothing else.
constexpr Real kTurbulenceIntensity = 0.05;
constexpr Real kCmu = 0.09;
Real inletK() {
  const Real fluct = kTurbulenceIntensity * kBulkVelocity;
  return 1.5 * fluct * fluct;
}
// omega = epsilon/(C_mu k) -- the same inlet state expressed in SST's variable,
// so the two models start from the same turbulence, not merely similar numbers.
Real inletOmega();

Real inletEpsilon() {
  const Real mixingLength = 0.07 * kChannelHeight;
  return std::pow(kCmu, 0.75) * std::pow(inletK(), 1.5) / mixingLength;
}

// The wall function's own log law -- the SAME kappa and B the model uses, as
// the frozen acceptance methodology requires (G3). NOTE this is B = ln(E)/kappa
// = 5.565, not the 5.0 the pre-P16 harness compared against: comparing a model
// against a different convention measures the gap between two conventions.
Real inletOmega() { return inletEpsilon() / (kCmu * inletK()); }

const cfd::turbulence::WallFunctionCoefficients kWallCoefficients{};

struct MeshCase {
  const char* label;
  Index ny;
};

// 180/ny is the predicted first-cell y+: 90, 60, 45, 36, 30, 22.5, 15.
// The last two are expected to FAIL G1 and are here to show it rejecting.
constexpr MeshCase kFamily[] = {
    {"ny02_yplus90", 2}, {"ny03_yplus60", 3}, {"ny04_yplus45", 4},  {"ny05_yplus36", 5},
    {"ny06_yplus30", 6}, {"ny08_yplus22", 8}, {"ny12_yplus15", 12},
};

std::filesystem::path evidenceRoot(const ChannelCase& channel, const char* modelName) {
  return std::filesystem::path("results/p16/turbulence/mesh_refinement") / channel.name / modelName;
}

// A private case directory driven through the PRODUCTION case path: the
// wall-function opt-in is `"wall_functions": true` in physics.json, parsed by
// CaseReader and applied by CaseBuilder. No test-only hook sets it.
class CaseDirectory {
 public:
  CaseDirectory(const ChannelCase& channel, Index nx, Index ny, bool wallFunctions,
                const char* model = "k_epsilon", const char* wallTreatment = nullptr) {
    path_ = std::filesystem::temp_directory_path() /
            ("cfdapp_p16_wf_" + std::string(channel.name) + "_" + std::to_string(nx) + "x" +
             std::to_string(ny) + "_" + (wallFunctions ? "wf" : "nowf") + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(path_);

    write("case.json", R"({"name":"p16_wall_function_channel","geometry":"geometry.json",)"
                       R"("mesh":"mesh.json","physics":"physics.json",)"
                       R"("boundaries":"boundaries.json","solver":"solver.json"})");
    write("geometry.json", "{\"type\":\"rectangle\",\"length\":" + num(kChannelLength) +
                               ",\"height\":" + num(kChannelHeight) + "}");
    write("mesh.json", "{\"type\":\"structured_cartesian\",\"nx\":" + std::to_string(nx) +
                           ",\"ny\":" + std::to_string(ny) + "}");
    write(
        "physics.json",
        "{\"model\":\"incompressible_laminar\",\"density\":" + num(kDensity) +
            ",\"dynamic_viscosity\":" + num(channel.dynamicViscosity()) +
            ",\"turbulence\":{\"model\":\"" + std::string(model) +
            "\",\"wall_functions\":" + (wallFunctions ? "true" : "false") +
            // P16b: absent unless a test asks, so every existing case string
            // below is byte-for-byte what it was.
            (wallTreatment != nullptr ? ",\"wall_treatment\":\"" + std::string(wallTreatment) + "\""
                                      : std::string()) +
            ",\"initial_k\":" + num(inletK()) +
            (std::string(model) == "sst" ? ",\"initial_omega\":" + num(inletOmega())
                                         : ",\"initial_epsilon\":" + num(inletEpsilon())) +
            "}}");
    write("boundaries.json",
          "{\"patches\":{"
          "\"left\":{\"velocity\":{\"type\":\"inlet\",\"value\":[" +
              num(kBulkVelocity) +
              ",0.0]},\"pressure\":{\"type\":\"fixed_gradient\",\"value\":0.0}},"
              "\"right\":{\"velocity\":{\"type\":\"outlet\"},"
              "\"pressure\":{\"type\":\"fixed_value\",\"value\":0.0}},"
              "\"bottom\":{\"velocity\":{\"type\":\"wall\"},"
              "\"pressure\":{\"type\":\"fixed_gradient\",\"value\":0.0}},"
              "\"top\":{\"velocity\":{\"type\":\"wall\"},"
              "\"pressure\":{\"type\":\"fixed_gradient\",\"value\":0.0}}}}");
    write("solver.json",
          R"({"type":"SIMPLE","max_iterations":4000,"velocity_relaxation":0.5,)"
          R"("pressure_relaxation":0.3,"velocity_tolerance":2e-5,"pressure_tolerance":5e-4,)"
          R"("continuity_tolerance":1e-6,)"
          R"("momentum_linear_solver":{"type":"BiCGSTAB","absolute_tolerance":1e-10,)"
          R"("relative_tolerance":1e-8,"max_iterations":500},)"
          R"("pressure_linear_solver":{"type":"BiCGSTAB","absolute_tolerance":1e-9,)"
          R"("relative_tolerance":1e-7,"max_iterations":2000}})");
  }

  ~CaseDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  CaseDirectory(const CaseDirectory&) = delete;
  CaseDirectory& operator=(const CaseDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  static std::string num(Real value) {
    std::ostringstream out;
    out.precision(17);
    out << value;
    return out.str();
  }
  void write(const std::string& name, const std::string& content) const {
    std::ofstream out(path_ / name);
    out << content;
  }
  std::filesystem::path path_;
};

struct SweepRecord {
  std::string label;
  Index ny{};
  Real firstCellYPlus{};
  Real wallShearStress{};
  Real frictionVelocity{};
  Real achievedReTau{};
  Real reTauError{};
  Real uPlusLInf{};
  Index logLayerSamples{};
  Index facesOutsideValidBand{};
  Index facesWithFlooredViscosity{};
  // P16b. Zero for k-epsilon and for the high-Re SST path, which has no
  // branches -- see SSTModel wallViscousSublayerFaces() on why the
  // counts carry no information there.
  Index viscousSublayerFaces{};
  Index blendedFaces{};
  Index logLayerFaces{};
  Index iterations{};
  Real uResidual{};
  Real pResidual{};
  Real continuityResidual{};
  bool converged{};
  bool satisfiesG1{};
  std::vector<Real> yPlusProfile;
  std::vector<Real> uPlusProfile;
  std::vector<Real> kProfile;
  std::vector<Real> epsilonProfile;

  // P16 Step 4 -- per-operation execution evidence. Captured inside runMesh,
  // because the trace is PROCESS-WIDE and a solve resets it at entry: the next
  // mesh in the sweep would overwrite it before a test could read it.
  std::array<cfd::gpu::ExecutionBackend,
             static_cast<std::size_t>(cfd::gpu::TurbulenceOperation::Count)>
      turbulenceBackends{};
  bool turbulenceAllOnGpu{};
  std::uint64_t strainRateExecutions{};
  std::uint64_t wallTreatmentExecutions{};
  bool turbulenceGpuQualified{};
  std::string backendTrace;
};

// Runs one mesh through the production case path and measures it.
enum class Model { KEpsilon, SST };

// `gpu` is the SMALLEST adapter that runs this frozen benchmark through the
// resident GPU production path. It flips backend selection and nothing else:
// geometry, mesh, forcing, Reynolds number, wall law, turbulence constants,
// solver tolerances and every acceptance criterion still come from the same
// case files, unchanged. So the GPU arm is measured against the same frozen
// benchmark rather than against a lookalike built beside it.
//
// It exists because `enableGpuDiscretization` has no case-file key (recorded
// as GPU-DISC-001R), so unlike the linear-solver backend it cannot be selected
// through the case directory.
SweepRecord runMesh(const ChannelCase& channel, const MeshCase& meshCase, bool wallFunctions,
                    Model model = Model::KEpsilon, const char* wallTreatment = nullptr,
                    bool gpu = false) {
  const CaseDirectory caseDirectory(channel, kNx, meshCase.ny, wallFunctions,
                                    model == Model::SST ? "sst" : "k_epsilon", wallTreatment);
  const auto definition = cfd::io::CaseReader{}.read(caseDirectory.path());
  auto setup = cfd::io::CaseBuilder{}.build(definition);
  if (gpu) {
    setup.solverSettings.momentumSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
    setup.solverSettings.pressureSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
    setup.solverSettings.enableGpuDiscretization = true;
    if (setup.kEpsilonConfig.has_value()) {
      setup.kEpsilonConfig->kSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
      setup.kEpsilonConfig->epsilonSolver.backend = cfd::algebra::LinearSolverBackend::GPU;
    }
  }

  // Constructed from the CASE-BUILT config: wall functions are on because
  // physics.json said so, for either model. One harness measures both -- a
  // second copy is how two benchmarks drift apart.
  std::unique_ptr<KEpsilonModel> kEpsilon;
  std::unique_ptr<cfd::turbulence::SSTModel> sst;
  cfd::turbulence::TurbulenceModel* active = nullptr;
  if (model == Model::SST) {
    sst = std::make_unique<cfd::turbulence::SSTModel>(setup.mesh, setup.fluid,
                                                      setup.velocityBoundaries, *setup.kBoundaries,
                                                      *setup.omegaBoundaries, *setup.sstConfig);
    active = sst.get();
  } else {
    kEpsilon = std::make_unique<KEpsilonModel>(setup.mesh, setup.fluid, setup.velocityBoundaries,
                                               *setup.kBoundaries, *setup.epsilonBoundaries,
                                               *setup.kEpsilonConfig);
    active = kEpsilon.get();
  }

  // The trace ACCUMULATES and no production path resets it (see its header).
  // Without this, a record would describe every solve since process start --
  // and because the host path records nothing, a CPU solve would inherit the
  // previous GPU solve's verdict wholesale.
  cfd::gpu::turbulenceBackendTrace().reset();

  const SIMPLE simple(setup.solverSettings, /*referenceCell=*/0, active);
  const SIMPLEResult result =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);

  SweepRecord record;
  record.label = meshCase.label;
  record.ny = meshCase.ny;
  record.converged = result.converged();
  record.iterations = result.iterations;
  record.uResidual = result.finalUResidual;
  record.pResidual = result.finalPressureResidual;
  record.continuityResidual = result.finalContinuityResidual;

  {
    const auto& trace = cfd::gpu::turbulenceBackendTrace();
    for (std::size_t op = 0; op < record.turbulenceBackends.size(); ++op) {
      record.turbulenceBackends[op] =
          trace.backendFor(static_cast<cfd::gpu::TurbulenceOperation>(op));
    }
    static constexpr cfd::gpu::TurbulenceOperation kEveryOperation[] = {
        cfd::gpu::TurbulenceOperation::StrainRate,
        cfd::gpu::TurbulenceOperation::Production,
        cfd::gpu::TurbulenceOperation::KAssembly,
        cfd::gpu::TurbulenceOperation::KSolve,
        cfd::gpu::TurbulenceOperation::EpsilonAssembly,
        cfd::gpu::TurbulenceOperation::EpsilonSolve,
        cfd::gpu::TurbulenceOperation::TurbulentViscosity,
        cfd::gpu::TurbulenceOperation::WallTreatment,
    };
    record.turbulenceAllOnGpu =
        trace.allOnGpu(kEveryOperation, static_cast<Index>(std::size(kEveryOperation)));
#ifdef CFDAPP_TEST_WITH_CUDA
    record.turbulenceGpuQualified = cfd::gpu::turbulenceGpuPathQualified(trace);
#endif
    record.strainRateExecutions = trace.executionCount(cfd::gpu::TurbulenceOperation::StrainRate);
    record.wallTreatmentExecutions =
        trace.executionCount(cfd::gpu::TurbulenceOperation::WallTreatment);
    record.backendTrace = trace.describe();
  }
  const ScalarField& wallShearField = sst ? sst->wallShearStress() : kEpsilon->wallShearStress();
  const ScalarField& yPlusField = sst ? sst->yPlus() : kEpsilon->yPlus();
  record.facesOutsideValidBand =
      sst ? sst->wallFacesOutsideValidBand() : kEpsilon->wallFacesOutsideValidBand();
  record.facesWithFlooredViscosity =
      sst ? sst->wallFacesWithFlooredViscosity() : kEpsilon->wallFacesWithFlooredViscosity();
  record.viscousSublayerFaces = sst ? sst->wallViscousSublayerFaces() : 0;
  record.blendedFaces = sst ? sst->wallBlendedFaces() : 0;
  record.logLayerFaces = sst ? sst->wallLogLayerFaces() : 0;

  // tau_w from the MODEL (see this file's header), averaged over the bottom
  // wall cells at the measurement station's column-independent value -- the
  // flow is fully developed, so every bottom wall cell carries the same shear
  // to within the development tolerance; the mean is used so a single
  // near-inlet cell cannot dominate.
  // Through the CORRECTED validation helper, which refuses the laminar secant
  // for a wall-function run. Both walls are measured and averaged; a developed
  // channel is symmetric, so a gap between them would itself be a defect.
  //
  // P16b: WHICH instrument is valid depends on whether a wall function ran.
  // The helper refuses the wrong pairing in either direction (its own
  // RefusesTheLaminarSecantForAWallFunctionRun and
  // RefusesAModelShearOnANoWallFunctionRun), so this is a required
  // selection, not a convenience: a low-Re run has no model wall shear to
  // report and must be measured with the secant.
  const auto kind = wallFunctions ? cfd::validation::WallTreatmentKind::WallFunction
                                  : cfd::validation::WallTreatmentKind::NoWallFunction;
  const cfd::fields::ScalarField* shearField = wallFunctions ? &wallShearField : nullptr;

  const Real station = 0.90 * kChannelLength;
  const Real bottomShear =
      cfd::validation::computeWallShearStress(setup.mesh, kNx, meshCase.ny, result.velocity,
                                              station, kChannelHeight, channel.dynamicViscosity(),
                                              /*bottomWall=*/true, kind, shearField);
  const Real topShear =
      cfd::validation::computeWallShearStress(setup.mesh, kNx, meshCase.ny, result.velocity,
                                              station, kChannelHeight, channel.dynamicViscosity(),
                                              /*bottomWall=*/false, kind, shearField);
  record.wallShearStress = 0.5 * (bottomShear + topShear);

  Real yPlusSum = 0.0;
  Index yPlusCount = 0;
  for (Index cell = 0; cell < setup.mesh.numberOfCells(); ++cell) {
    if (yPlusField[cell] > 0.0) {
      yPlusSum += yPlusField[cell];
      ++yPlusCount;
    }
  }
  record.firstCellYPlus = yPlusCount > 0 ? yPlusSum / static_cast<Real>(yPlusCount) : 0.0;
  record.frictionVelocity =
      cfd::turbulence::frictionVelocityFromWallShear(record.wallShearStress, kDensity);
  // Re_tau = u_tau * delta / nu, delta = half-height.
  record.achievedReTau =
      record.frictionVelocity * (0.5 * kChannelHeight) * kDensity / channel.dynamicViscosity();
  record.reTauError = std::abs(record.achievedReTau - channel.reTauTarget) / channel.reTauTarget;
  // G1 is a PER-FACE precondition. The frozen methodology says "y_1^+ ... at
  // every wall face", so the criterion is that NO wall face fell outside the
  // band -- not that the mean did not.
  //
  // The distinction is not academic: the first run of this sweep evaluated G1
  // on the mean and passed ny = 5, whose mean y+ is 32.7 while ten of its wall
  // faces sit below 30. A mean-based precondition lets a mesh through on the
  // strength of its better-resolved faces, which is precisely the averaging a
  // precondition exists to prevent.
  record.satisfiesG1 = record.facesOutsideValidBand == 0;

  // u+(y+) profile at the primary station, bottom half only (the top half
  // mirrors it), compared against the model's own log law.
  const auto profile = cfd::validation::extractVerticalProfileU(
      setup.mesh, kNx, meshCase.ny, result.velocity, station, kChannelHeight);
  const ScalarField& kField = sst ? sst->k() : kEpsilon->k();
  // The SECOND turbulence scalar: epsilon for k-epsilon, omega for SST. Both
  // are recorded in the same column, identified by the case directory's model.
  const ScalarField& secondField = sst ? sst->omega() : kEpsilon->epsilon();
  const auto kProfile =
      cfd::validation::extractVerticalProfileScalar(setup.mesh, kNx, meshCase.ny, kField, station);
  const auto epsProfile = cfd::validation::extractVerticalProfileScalar(
      setup.mesh, kNx, meshCase.ny, secondField, station);

  Real worst = 0.0;
  for (std::size_t i = 0; i < profile.size(); ++i) {
    const Real y = profile[i].coordinate;
    const Real wallDistance = std::min(y, kChannelHeight - y);
    if (record.frictionVelocity <= 0.0) continue;
    const Real yPlusValue = cfd::turbulence::yPlus(kDensity, record.frictionVelocity, wallDistance,
                                                   channel.dynamicViscosity());
    const Real uPlusValue = profile[i].value / record.frictionVelocity;
    record.yPlusProfile.push_back(yPlusValue);
    record.uPlusProfile.push_back(uPlusValue);
    record.kProfile.push_back(i < kProfile.size() ? kProfile[i].value : 0.0);
    record.epsilonProfile.push_back(i < epsProfile.size() ? epsProfile[i].value : 0.0);

    // G3 is evaluated over the LOG LAYER only -- comparing a log law against a
    // point in the viscous sublayer or the wake would measure the log law's own
    // inapplicability there, not the solution's quality.
    if (yPlusValue >= cfd::turbulence::kWallFunctionMinimumYPlus &&
        yPlusValue <= cfd::turbulence::kWallFunctionMaximumYPlus) {
      const Real reference = cfd::turbulence::uPlusLogLaw(yPlusValue, kWallCoefficients);
      worst = std::max(worst, std::abs(uPlusValue - reference));
      ++record.logLayerSamples;
    }
  }
  record.uPlusLInf = worst;
  return record;
}

void writeSweepCsv(const ChannelCase& channel, const char* modelName,
                   const std::vector<SweepRecord>& records, const std::string& name) {
  std::filesystem::create_directories(evidenceRoot(channel, modelName));
  std::ofstream out(evidenceRoot(channel, modelName) / name);
  out.precision(10);
  out << "mesh,ny,first_cell_y_plus,Re_tau,E_Re_tau,u_plus_Linf,log_layer_samples,tau_w,u_tau,"
         "faces_outside_band,floored_viscosity_faces,iterations,u_residual,p_residual,"
         "continuity_residual,converged,satisfies_G1,"
         "viscous_sublayer_faces,blended_faces,log_layer_faces\n";
  for (const auto& r : records) {
    out << r.label << ',' << r.ny << ',' << r.firstCellYPlus << ',' << r.achievedReTau << ','
        << r.reTauError << ',' << r.uPlusLInf << ',' << r.logLayerSamples << ','
        << r.wallShearStress << ',' << r.frictionVelocity << ',' << r.facesOutsideValidBand << ','
        << r.facesWithFlooredViscosity << ',' << r.iterations << ',' << r.uResidual << ','
        << r.pResidual << ',' << r.continuityResidual << ',' << (r.converged ? 1 : 0) << ','
        << (r.satisfiesG1 ? 1 : 0) << ',' << r.viscousSublayerFaces << ',' << r.blendedFaces << ','
        << r.logLayerFaces << '\n';
  }
}

void writeProfiles(const ChannelCase& channel, const char* modelName, const SweepRecord& record) {
  std::filesystem::create_directories(evidenceRoot(channel, modelName) / record.label);
  std::ofstream out(evidenceRoot(channel, modelName) / record.label / "profiles.csv");
  out.precision(10);
  out << "y_plus,u_plus,u_plus_loglaw,k,epsilon\n";
  for (std::size_t i = 0; i < record.yPlusProfile.size(); ++i) {
    const Real yPlusValue = record.yPlusProfile[i];
    const Real reference =
        yPlusValue > 0.0 ? cfd::turbulence::uPlusLogLaw(yPlusValue, kWallCoefficients) : 0.0;
    out << yPlusValue << ',' << record.uPlusProfile[i] << ',' << reference << ','
        << record.kProfile[i] << ',' << record.epsilonProfile[i] << '\n';
  }
}

}  // namespace

// The Re_tau = 590 family. ny <= 19 satisfies the validity band at this
// Reynolds number, so this spans the band AND carries real core resolution --
// ny = 16 gives eight cells per half-channel against the Re_tau = 180 case's
// best of two. ny = 24 and 32 are included to show G1 rejecting.
constexpr MeshCase kFamily590[] = {
    {"ny06", 6}, {"ny08", 8}, {"ny12", 12}, {"ny16", 16}, {"ny20", 20}, {"ny24", 24}, {"ny32", 32},
};

// One sweep, printed and recorded the same way for either Reynolds number.
std::vector<SweepRecord> runSweep(const ChannelCase& channel, const MeshCase* family,
                                  std::size_t count, Model model = Model::KEpsilon,
                                  const char* wallTreatment = nullptr, bool gpu = false) {
  // P16b evidence goes to its OWN directory. Writing it under "sst" would
  // overwrite the preserved high-Re sweep, which is the comparison baseline.
  const bool automatic = wallTreatment != nullptr && std::string(wallTreatment) == "automatic";
  // The GPU arm gets its OWN evidence directory for the same reason the
  // automatic wall treatment does: writing it under "k_epsilon" would
  // overwrite the preserved CPU sweep, which is the comparison baseline.
  const char* modelName = model == Model::SST ? (automatic ? "sst_automatic" : "sst")
                                              : (gpu ? "k_epsilon_gpu" : "k_epsilon");
  std::vector<SweepRecord> records;
  records.reserve(count);
  std::printf("\n%s\n%-14s %4s %10s %10s %10s %10s %8s %12s %6s %6s %5s\n", channel.name, "mesh",
              "ny", "y1+", "Re_tau", "E_Re_tau", "u+ Linf", "samples", "tau_w", "out", "floor",
              "iter");
  for (std::size_t i = 0; i < count; ++i) {
    const SweepRecord record =
        runMesh(channel, family[i], /*wallFunctions=*/true, model, wallTreatment, gpu);
    std::printf("%-14s %4zu %10.3f %10.3f %10.4f %10.4f %8zu %12.6e %6zu %6zu %5zu %s\n",
                record.label.c_str(), record.ny, record.firstCellYPlus, record.achievedReTau,
                record.reTauError, record.uPlusLInf, record.logLayerSamples, record.wallShearStress,
                record.facesOutsideValidBand, record.facesWithFlooredViscosity, record.iterations,
                record.satisfiesG1 ? "G1-OK" : "G1-REJECT");
    writeProfiles(channel, modelName, record);
    records.push_back(record);
  }
  writeSweepCsv(channel, modelName, records, "sweep.csv");
  return records;
}

TEST(WallFunctionChannel, RefinementSweep) {
  const auto records = runSweep(kReTau180, kFamily, std::size(kFamily));

  for (const auto& record : records) {
    // Stability, not accuracy: whatever the mesh, the solve must not produce a
    // non-finite field. Accuracy is judged by the metrics.
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
    EXPECT_TRUE(std::isfinite(record.achievedReTau)) << record.label;
  }

  // G1 must both ACCEPT and REJECT within this family -- a precondition that
  // never rejects is not a precondition.
  const auto accepted = std::count_if(records.begin(), records.end(),
                                      [](const SweepRecord& r) { return r.satisfiesG1; });
  EXPECT_GT(accepted, 0) << "no mesh in the family satisfies G1 -- the family is mis-designed";
  EXPECT_LT(accepted, static_cast<long>(records.size()))
      << "every mesh satisfies G1 -- the family does not exercise the rejection path";
}

TEST(WallFunctionChannel, WallFunctionsImproveOnThePreP16Baseline) {
  // The question this phase exists to answer, asked directly. The preserved
  // baseline measured E_Re_tau = 0.333 (k-epsilon, 64x16, y+ 7.5). If the best
  // wall-function mesh cannot beat that, the treatment is not an improvement
  // and no threshold should be derived from it.
  //
  // Deliberately reported rather than asserted tightly: the frozen methodology
  // sets the thresholds AFTER this sweep, from its data. Asserting a specific
  // value here would be choosing the threshold in advance.
  constexpr Real kBaselineReTauError = 0.333;

  Real best = std::numeric_limits<Real>::infinity();
  std::string bestLabel;
  for (const auto& meshCase : kFamily) {
    const SweepRecord record = runMesh(kReTau180, meshCase, /*wallFunctions=*/true);
    if (!record.satisfiesG1) continue;
    if (record.reTauError < best) {
      best = record.reTauError;
      bestLabel = record.label;
    }
  }
  ASSERT_TRUE(std::isfinite(best)) << "no G1-satisfying mesh produced a finite Re_tau";
  std::printf("\nbest G1-satisfying mesh: %s, E_Re_tau = %.4f (pre-P16 baseline %.3f)\n",
              bestLabel.c_str(), best, kBaselineReTauError);
  EXPECT_LT(best, kBaselineReTauError)
      << "the wall-function treatment does not improve on the pre-P16 baseline";
}

// THE ADOPTED P16 k-epsilon CHANNEL GATE.
//
// Derived from the sweep above and accepted after measurement, per
// acceptance/methodology.md and acceptance/threshold_derivation.md:
//
//   G1  30 <= y+ <= 300 at EVERY wall face   (hard precondition)
//   G2  E_Re_tau       <= 0.22
//   G3  E_{u+,Linf}    <= 3.9
//
// Both thresholds are the worst value over the G1-valid meshes plus 5% for
// platform floating-point variation (run-to-run noise is exactly zero -- three
// independent runs reproduced every printed digit). They are materially
// tighter than the preserved baseline's 0.270 / 9.22.
//
// ny02 sets both, and is deliberately NOT excluded. It satisfied the
// pre-declared validity criterion; removing the mesh that sets a threshold in
// order to report a better threshold is what makes a gate meaningless.
constexpr Real kGateReTauError = 0.22;
constexpr Real kGateUPlusLInf = 3.9;

// The Re_tau = 180 gate body, written once so the CPU and GPU arms cannot
// drift -- the same reason the Re_tau = 590 gate is a shared function.
void expectAdoptedGate180(bool gpu, const char* arm) {
  Index validMeshes = 0;
  for (const auto& meshCase : kFamily) {
    const SweepRecord record = runMesh(kReTau180, meshCase, /*wallFunctions=*/true, Model::KEpsilon,
                                       /*wallTreatment=*/nullptr, gpu);
    if (!record.satisfiesG1) continue;
    ++validMeshes;

    EXPECT_TRUE(record.converged) << arm << " " << record.label;
    EXPECT_LE(record.reTauError, kGateReTauError)
        << arm << " " << record.label << " E_Re_tau = " << record.reTauError;
    EXPECT_LE(record.uPlusLInf, kGateUPlusLInf)
        << arm << " " << record.label << " u+ Linf = " << record.uPlusLInf;
    EXPECT_EQ(record.facesWithFlooredViscosity, 0u)
        << arm << " " << record.label << " floored viscosity inside the valid band";

    if (gpu) {
      EXPECT_TRUE(record.turbulenceGpuQualified) << record.label << "\n" << record.backendTrace;
      EXPECT_EQ(record.strainRateExecutions, static_cast<std::uint64_t>(record.iterations))
          << record.label << "\n"
          << record.backendTrace;
      EXPECT_FALSE(record.turbulenceAllOnGpu) << record.label << "\n" << record.backendTrace;
    } else {
      // The non-vacuity half: the CPU arm must NOT look qualified, or the
      // provenance assertion above proves nothing. This is the control that
      // caught the process-lifetime trace defect during Step 4.
      EXPECT_FALSE(record.turbulenceGpuQualified) << record.label << "\n" << record.backendTrace;
      EXPECT_EQ(record.strainRateExecutions, 0u) << record.label << "\n" << record.backendTrace;
    }
  }
  EXPECT_GE(validMeshes, 3u) << arm << ": the G1-valid subset of the family shrank";
}

TEST(WallFunctionChannel, EveryG1ValidMeshPassesTheAdoptedGate) {
  expectAdoptedGate180(/*gpu=*/false, "CPU");
}

#ifdef CFDAPP_TEST_WITH_CUDA
// P16 Step 5, gate S5-2 -- the SECOND operating regime.
//
// Step 4 qualified Re_tau = 590 only. At Re_tau = 180 the channel is just 180
// wall units tall, so the wall function's validity band and the core
// resolution are in direct conflict (ny <= 6) and the adopted gate is an order
// of magnitude looser. That makes it a genuinely different regime rather than
// the same case at another Reynolds number, which is why breadth is measured
// here rather than assumed from the 590 result.
TEST(WallFunctionChannelGpu, EveryG1ValidMeshPassesTheAdoptedGate) {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no CUDA device";
  expectAdoptedGate180(/*gpu=*/true, "GPU");
}
#endif

#ifdef CFDAPP_TEST_WITH_CUDA
// P16 Step 5, gate S5-2 -- the THIRD operating regime: k-epsilon with the wall
// function OFF.
//
// This path has NO adopted physical gate, and deliberately so: it IS the
// pre-P16 baseline (E_Re_tau = 0.333 at y+ 7.5) that the wall-function work
// improved on, and the frozen methodology sets thresholds after the sweep that
// measures them. Inventing one here during qualification is exactly what a
// frozen gate exists to prevent -- see AMENDMENT A1 in step5_gate.md.
//
// So the criterion is the equivalence one: CPU numerics are the reference, and
// the GPU arm must reproduce them to the unchanged 1e-6 bound. The bound is
// NOT re-derived from the observed difference.
TEST(WallFunctionChannelGpu, NoWallFunctionPathReproducesTheCpuReference) {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no CUDA device";
  constexpr Real kEquivalenceBound = 1.0e-6;

  std::printf("\nno-wall-function k-epsilon, CPU reference vs GPU\n");
  std::printf("%-8s %14s %14s %12s %14s %14s %12s\n", "mesh", "Re_tau(C)", "Re_tau(G)", "rel|dRe|",
              "tau_w(C)", "tau_w(G)", "rel|dtau|");

  Index compared = 0;
  for (const auto& meshCase : kFamily) {
    const SweepRecord cpu = runMesh(kReTau180, meshCase, /*wallFunctions=*/false, Model::KEpsilon,
                                    /*wallTreatment=*/nullptr, /*gpu=*/false);
    const SweepRecord gpu = runMesh(kReTau180, meshCase, /*wallFunctions=*/false, Model::KEpsilon,
                                    /*wallTreatment=*/nullptr, /*gpu=*/true);
    if (!cpu.converged || !gpu.converged) continue;
    ++compared;

    const auto rel = [](Real c, Real g) {
      return c != 0.0 ? std::abs(g - c) / std::abs(c) : std::abs(g - c);
    };
    const Real dReTau = rel(cpu.achievedReTau, gpu.achievedReTau);
    const Real dTau = rel(cpu.wallShearStress, gpu.wallShearStress);
    std::printf("%-8s %14.6e %14.6e %12.3e %14.6e %14.6e %12.3e\n", cpu.label.c_str(),
                cpu.achievedReTau, gpu.achievedReTau, dReTau, cpu.wallShearStress,
                gpu.wallShearStress, dTau);

    EXPECT_LE(dReTau, kEquivalenceBound) << cpu.label << " Re_tau";
    EXPECT_LE(dTau, kEquivalenceBound) << cpu.label << " tau_w";
    EXPECT_EQ(cpu.iterations, gpu.iterations)
        << cpu.label << ": the two arms took different outer-iteration counts";

    // Provenance, and its non-vacuity half.
    EXPECT_TRUE(gpu.turbulenceGpuQualified) << cpu.label << "\n" << gpu.backendTrace;
    EXPECT_FALSE(cpu.turbulenceGpuQualified) << cpu.label << "\n" << cpu.backendTrace;
    EXPECT_EQ(cpu.strainRateExecutions, 0u) << cpu.label;
    EXPECT_GT(gpu.strainRateExecutions, 0u) << cpu.label;
  }
  EXPECT_GE(compared, 3u) << "too few meshes converged to call this a breadth check";
}

// P16 Step 5, gate S5-3 -- determinism ACROSS PROCESSES.
//
// The existing IsDeterministic tests run both arms inside one process, so a
// result that depended on process-wide state -- a cached plan, a stale
// accumulator, an allocator address -- would agree with itself. This one is
// still in-process for the solve, but it asserts BITWISE equality rather than
// a tolerance, and the cross-process half is driven by the Step-5 tool, which
// runs this binary twice and diffs all 17 printed digits.
TEST(WallFunctionChannelGpu, IsBitwiseDeterministic) {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no CUDA device";
  const MeshCase probe = kFamily590[3];
  const SweepRecord first = runMesh(kReTau590, probe, /*wallFunctions=*/true, Model::KEpsilon,
                                    /*wallTreatment=*/nullptr, /*gpu=*/true);
  const SweepRecord second = runMesh(kReTau590, probe, /*wallFunctions=*/true, Model::KEpsilon,
                                     /*wallTreatment=*/nullptr, /*gpu=*/true);

  // Bitwise, not tolerance-based: a reduction whose result depended on block
  // scheduling would show here and nowhere else.
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.achievedReTau, second.achievedReTau);
  EXPECT_EQ(first.uPlusLInf, second.uPlusLInf);
  std::printf("\nGPU determinism (in-process, bitwise): tau_w %.17e\n", first.wallShearStress);
}
#endif

TEST(WallFunctionChannel, IsDeterministic) {
  const MeshCase probe = kFamily[4];  // ny = 6, the finest G1-satisfying mesh
  const SweepRecord first = runMesh(kReTau180, probe, /*wallFunctions=*/true);
  const SweepRecord second = runMesh(kReTau180, probe, /*wallFunctions=*/true);

  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.achievedReTau, second.achievedReTau);
  EXPECT_EQ(first.uPlusLInf, second.uPlusLInf);
  ASSERT_EQ(first.uPlusProfile.size(), second.uPlusProfile.size());
  for (std::size_t i = 0; i < first.uPlusProfile.size(); ++i) {
    EXPECT_EQ(first.uPlusProfile[i], second.uPlusProfile[i]) << "profile point " << i;
  }
}

// --- the corrected validation wall shear -------------------------------------
//
// These exist because the OLD helper would happily measure a wall-function run
// with the laminar secant mu*U(y1)/y1 and report a number that is wrong in the
// flattering direction. The pairing is now enforced, and these pin that it
// stays enforced.

TEST(WallFunctionValidationShear, RefusesTheLaminarSecantForAWallFunctionRun) {
  const CaseDirectory caseDirectory(kReTau180, kNx, 4, /*wallFunctions=*/true);
  const auto definition = cfd::io::CaseReader{}.read(caseDirectory.path());
  auto setup = cfd::io::CaseBuilder{}.build(definition);
  const VectorField velocity(setup.mesh.numberOfCells(), cfd::Vector2{kBulkVelocity, 0.0});

  // Asking for the wall-function treatment without supplying the model's own
  // wall shear must THROW, not fall back.
  EXPECT_THROW(
      (void)cfd::validation::computeWallShearStress(
          setup.mesh, kNx, 4, velocity, 0.9 * kChannelLength, kChannelHeight,
          kReTau180.dynamicViscosity(), true, cfd::validation::WallTreatmentKind::WallFunction,
          /*modelWallShear=*/nullptr),
      std::invalid_argument);
}

TEST(WallFunctionValidationShear, RefusesAModelShearOnANoWallFunctionRun) {
  // The mirror case. Being handed the model's value and silently ignoring it is
  // just as wrong as the reverse, and just as invisible.
  const CaseDirectory caseDirectory(kReTau180, kNx, 4, /*wallFunctions=*/true);
  const auto definition = cfd::io::CaseReader{}.read(caseDirectory.path());
  auto setup = cfd::io::CaseBuilder{}.build(definition);
  const VectorField velocity(setup.mesh.numberOfCells(), cfd::Vector2{kBulkVelocity, 0.0});
  const ScalarField shear(setup.mesh.numberOfCells(), 1.0);

  EXPECT_THROW((void)cfd::validation::computeWallShearStress(
                   setup.mesh, kNx, 4, velocity, 0.9 * kChannelLength, kChannelHeight,
                   kReTau180.dynamicViscosity(), true,
                   cfd::validation::WallTreatmentKind::NoWallFunction, &shear),
               std::invalid_argument);
}

TEST(WallFunctionValidationShear, TheSecantUnderReportsAWallFunctionWallShear) {
  // The quantitative reason the refusal exists. Run a real wall-function solve,
  // then measure its wall shear both ways. If the two agreed, the enforcement
  // would be pedantry; they do not, and the secant is the smaller -- which is
  // exactly the direction that would flatter every derived metric.
  const CaseDirectory caseDirectory(kReTau180, kNx, 4, /*wallFunctions=*/true);
  const auto definition = cfd::io::CaseReader{}.read(caseDirectory.path());
  auto setup = cfd::io::CaseBuilder{}.build(definition);
  KEpsilonModel model(setup.mesh, setup.fluid, setup.velocityBoundaries, *setup.kBoundaries,
                      *setup.epsilonBoundaries, *setup.kEpsilonConfig);
  const SIMPLE simple(setup.solverSettings, /*referenceCell=*/0, &model);
  const SIMPLEResult result =
      simple.solve(setup.mesh, setup.fluid, setup.velocityBoundaries, setup.pressureBoundaries,
                   setup.initialVelocity, setup.initialPressure);
  ASSERT_TRUE(result.converged());

  const Real station = 0.90 * kChannelLength;
  const Real modelShear = cfd::validation::computeWallShearStress(
      setup.mesh, kNx, 4, result.velocity, station, kChannelHeight, kReTau180.dynamicViscosity(),
      true, cfd::validation::WallTreatmentKind::WallFunction, &model.wallShearStress());
  const Real secantShear = cfd::validation::computeWallShearStress(
      setup.mesh, kNx, 4, result.velocity, station, kChannelHeight, kReTau180.dynamicViscosity(),
      true, cfd::validation::WallTreatmentKind::NoWallFunction, nullptr);

  EXPECT_GT(modelShear, secantShear)
      << "model tau_w " << modelShear << " vs laminar secant " << secantShear;
  // Not a rounding difference: at y+ ~ 40 the secant misses by a large factor.
  EXPECT_GT(modelShear / secantShear, 2.0)
      << "the two estimators agree too closely for this to be a meaningful guard";
}

TEST(WallFunctionValidationShear, TheSecantIsUnchangedForANonWallFunctionRun) {
  // The other half: every pre-P16 caller must get exactly what it got before.
  const CaseDirectory caseDirectory(kReTau180, kNx, 16, /*wallFunctions=*/false);
  const auto definition = cfd::io::CaseReader{}.read(caseDirectory.path());
  auto setup = cfd::io::CaseBuilder{}.build(definition);
  const VectorField velocity(setup.mesh.numberOfCells(), cfd::Vector2{kBulkVelocity, 0.0});

  const Real shear = cfd::validation::computeWallShearStress(
      setup.mesh, kNx, 16, velocity, 0.9 * kChannelLength, kChannelHeight,
      kReTau180.dynamicViscosity(), true, cfd::validation::WallTreatmentKind::NoWallFunction,
      nullptr);

  // mu * U / y1 with y1 = H/(2*ny).
  const Real y1 = kChannelHeight / (2.0 * 16.0);
  EXPECT_DOUBLE_EQ(shear, kReTau180.dynamicViscosity() * kBulkVelocity / y1);
}

// =====================================================================
// Re_tau = 590 -- the PRIMARY wall-function accuracy benchmark
// =====================================================================
//
// The Re_tau = 180 sweep above stays as the low-Re stress case. It sits at the
// edge of the wall function's validity by construction: ny <= Re_tau/30 = 6
// there, so a mesh can satisfy the band or resolve the core, not both.
//
// At Re_tau = 590 the same inequality gives ny <= 19, which is what makes this
// the case where accuracy can actually be judged.

TEST(WallFunctionChannelHighRe, RefinementSweep) {
  const auto records = runSweep(kReTau590, kFamily590, std::size(kFamily590));

  for (const auto& record : records) {
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
    EXPECT_TRUE(std::isfinite(record.achievedReTau)) << record.label;
    EXPECT_TRUE(record.converged) << record.label << " did not converge";
  }

  const auto accepted = std::count_if(records.begin(), records.end(),
                                      [](const SweepRecord& r) { return r.satisfiesG1; });
  EXPECT_GT(accepted, 0) << "no Re_tau = 590 mesh satisfies G1 -- the family is mis-designed";
  EXPECT_LT(accepted, static_cast<long>(records.size()))
      << "every mesh satisfies G1 -- the family does not exercise the rejection path";
}

TEST(WallFunctionChannelHighRe, ResolvesTheCoreWhileStayingValid) {
  // The property that makes this the primary benchmark, and the one Re_tau =
  // 180 structurally cannot have: a mesh that satisfies G1 at every wall face
  // AND has enough wall-normal cells for the profile comparison to mean
  // something.
  //
  // Asserted rather than assumed, because if it turned out to be false the
  // higher-Re case would have no advantage over the one it supplements.
  const auto records = runSweep(kReTau590, kFamily590, std::size(kFamily590));

  Index bestValidNy = 0;
  for (const auto& record : records) {
    if (record.satisfiesG1) bestValidNy = std::max(bestValidNy, record.ny);
  }
  EXPECT_GE(bestValidNy, 12u)
      << "the finest G1-valid mesh has only " << bestValidNy
      << " wall-normal cells -- no better than the Re_tau = 180 case, so this Reynolds number "
         "buys nothing";
}

TEST(WallFunctionChannelHighRe, IsDeterministic) {
  const SweepRecord first = runMesh(kReTau590, kFamily590[3], /*wallFunctions=*/true);
  const SweepRecord second = runMesh(kReTau590, kFamily590[3], /*wallFunctions=*/true);
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.achievedReTau, second.achievedReTau);
  EXPECT_EQ(first.uPlusLInf, second.uPlusLInf);
}

// THE Re_tau = 590 GATE -- the primary wall-function accuracy gate.
//
// Derived from the sweep above by the same frozen procedure as the Re_tau = 180
// one: worst value over the G1-valid meshes {ny06, ny08, ny12, ny16}, plus 5%
// for platform floating-point variation.
//
//   worst E_Re_tau   = 0.0587 (ny06)  -> 0.062
//   worst u+ L_inf   = 1.4903 (ny06)  -> 1.6
//
// An order of magnitude tighter than the Re_tau = 180 gate (0.22 / 3.9), which
// is the whole reason this case exists: at Re_tau = 590 the wall function can
// be judged on accuracy rather than on how gracefully it degrades at the edge
// of its validity.
constexpr Real kHighReGateReTauError = 0.062;
constexpr Real kHighReGateUPlusLInf = 1.6;

// THE gate body, written ONCE. Both the CPU and the GPU arm call it, so the
// two cannot drift: a change to the criterion changes both or neither. This
// file's header already states that principle for the measurement harness --
// it applies at least as strongly to the acceptance criterion.
void expectAdoptedGate(const std::vector<SweepRecord>& records, const char* arm) {
  Index validMeshes = 0;
  for (const auto& record : records) {
    if (!record.satisfiesG1) continue;
    ++validMeshes;
    EXPECT_TRUE(record.converged) << arm << " " << record.label;
    EXPECT_LE(record.reTauError, kHighReGateReTauError)
        << arm << " " << record.label << " E_Re_tau = " << record.reTauError;
    EXPECT_LE(record.uPlusLInf, kHighReGateUPlusLInf)
        << arm << " " << record.label << " u+ Linf = " << record.uPlusLInf;
    EXPECT_EQ(record.facesWithFlooredViscosity, 0u) << arm << " " << record.label;
  }
  EXPECT_GE(validMeshes, 4u) << arm << ": the G1-valid subset of the Re_tau = 590 family shrank";
}

TEST(WallFunctionChannelHighRe, EveryG1ValidMeshPassesTheAdoptedGate) {
  expectAdoptedGate(runSweep(kReTau590, kFamily590, std::size(kFamily590)), "CPU");
}

#ifdef CFDAPP_TEST_WITH_CUDA
// P16 Step 4 gate 2 -- the SAME frozen benchmark, through the resident GPU
// k-epsilon production path.
//
// The GPU arm has to satisfy the physical benchmark ON ITS OWN. A CPU/GPU
// field differential is a different gate, and it already exists elsewhere;
// two arms can agree with each other while both being wrong, so agreement is
// reported here as evidence and is not the acceptance criterion.
TEST(WallFunctionChannelHighReGpu, EveryG1ValidMeshPassesTheAdoptedGate) {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no CUDA device";

  const auto gpuRecords = runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::KEpsilon,
                                   /*wallTreatment=*/nullptr, /*gpu=*/true);
  expectAdoptedGate(gpuRecords, "GPU");

  // Provenance. The seven flow-side operations must have run on the device.
  // WallTreatment stays on the host by the recorded P16 scope decision, so
  // allOnGpu() must be FALSE -- asserted rather than merely tolerated, because
  // a true here would mean that scope decision had silently changed.
  for (const auto& record : gpuRecords) {
    if (!record.satisfiesG1) continue;
    EXPECT_TRUE(record.turbulenceGpuQualified) << record.label << "\n" << record.backendTrace;
    // A boolean says an operation ran on the device at SOME point. The count
    // says it ran on EVERY outer iteration of THIS solve, which is the claim
    // the benchmark actually needs.
    EXPECT_EQ(record.strainRateExecutions, static_cast<std::uint64_t>(record.iterations))
        << record.label << "\n"
        << record.backendTrace;
    EXPECT_FALSE(record.turbulenceAllOnGpu) << record.label << "\n" << record.backendTrace;
    EXPECT_EQ(record.turbulenceBackends[static_cast<std::size_t>(
                  cfd::gpu::TurbulenceOperation::WallTreatment)],
              cfd::gpu::ExecutionBackend::Cpu)
        << record.label << "\n"
        << record.backendTrace;
  }

  const auto cpuRecords = runSweep(kReTau590, kFamily590, std::size(kFamily590));
  std::printf("\nCPU vs GPU -- Re_tau = 590 frozen benchmark\n");
  std::printf("%-8s %4s %10s %10s %10s %10s %10s %10s %14s %14s\n", "mesh", "G1", "Re_tau(C)",
              "Re_tau(G)", "E(C)", "E(G)", "u+Linf(C)", "u+Linf(G)", "tau_w(C)", "tau_w(G)");
  for (std::size_t i = 0; i < cpuRecords.size() && i < gpuRecords.size(); ++i) {
    std::printf("%-8s %4s %10.4f %10.4f %10.5f %10.5f %10.4f %10.4f %14.6e %14.6e\n",
                cpuRecords[i].label.c_str(), cpuRecords[i].satisfiesG1 ? "ok" : "rej",
                cpuRecords[i].achievedReTau, gpuRecords[i].achievedReTau, cpuRecords[i].reTauError,
                gpuRecords[i].reTauError, cpuRecords[i].uPlusLInf, gpuRecords[i].uPlusLInf,
                cpuRecords[i].wallShearStress, gpuRecords[i].wallShearStress);
  }

  // The G1-valid SET must be identical on both arms, or the two were not
  // judged on the same meshes and the comparison above means nothing.
  ASSERT_EQ(cpuRecords.size(), gpuRecords.size());
  for (std::size_t i = 0; i < cpuRecords.size(); ++i) {
    EXPECT_EQ(cpuRecords[i].satisfiesG1, gpuRecords[i].satisfiesG1) << cpuRecords[i].label;
  }

  // NON-VACUITY. The provenance assertions above are the only thing standing
  // between this test and running the CPU benchmark twice under two names: if
  // the adapter silently failed to select the device, every physical
  // criterion would still pass. So the SAME predicate is required to be FALSE
  // on the CPU arm, and the iteration counts are printed at full precision --
  // "the two agree" is worth nothing until it is known they were not the same
  // computation.
  for (const auto& record : cpuRecords) {
    EXPECT_FALSE(record.turbulenceGpuQualified)
        << "the CPU arm reports the GPU path qualified, so the provenance check above proves "
           "nothing\n"
        << record.label << "\n"
        << record.backendTrace;
    EXPECT_EQ(record.strainRateExecutions, 0u) << record.label << "\n" << record.backendTrace;
  }

  std::printf("\nfull-precision CPU/GPU difference (evidence, not a criterion)\n");
  std::printf("%-8s %5s %5s %8s %8s %24s %24s %12s\n", "mesh", "it(C)", "it(G)", "gpuOps",
              "wallOps", "tau_w(C)", "tau_w(G)", "rel|dtau_w|");
  for (std::size_t i = 0; i < cpuRecords.size(); ++i) {
    const Real c = cpuRecords[i].wallShearStress;
    const Real g = gpuRecords[i].wallShearStress;
    const Real rel = c != 0.0 ? std::abs(g - c) / std::abs(c) : std::abs(g - c);
    std::printf("%-8s %5zu %5zu %8llu %8llu %24.17e %24.17e %12.3e\n", cpuRecords[i].label.c_str(),
                cpuRecords[i].iterations, gpuRecords[i].iterations,
                static_cast<unsigned long long>(gpuRecords[i].strainRateExecutions),
                static_cast<unsigned long long>(gpuRecords[i].wallTreatmentExecutions), c, g, rel);
  }
}
#endif

TEST(WallFunctionChannelHighRe, AccuracyAndValidityAreNoLongerInTension) {
  // The property that makes Re_tau = 590 the primary benchmark and Re_tau = 180
  // the stress case, stated as a test rather than left in a document.
  //
  // At Re_tau = 180 the most accurate mesh was the LEAST valid: E_Re_tau fell
  // monotonically to 0.0075 at ny = 12 while y+ fell to 14.9, outside the band.
  // A score-based gate would have selected a result obtained where the model's
  // own derivation does not hold.
  //
  // Here the most accurate mesh must be INSIDE the band. If that ever stops
  // being true, this case has lost the advantage it was chosen for.
  const auto records = runSweep(kReTau590, kFamily590, std::size(kFamily590));

  const auto best = std::min_element(
      records.begin(), records.end(),
      [](const SweepRecord& a, const SweepRecord& b) { return a.reTauError < b.reTauError; });
  ASSERT_NE(best, records.end());
  EXPECT_TRUE(best->satisfiesG1)
      << "the most accurate mesh (" << best->label << ", E_Re_tau = " << best->reTauError
      << ") lies OUTSIDE the wall function's validity band -- the same tension Re_tau = 180 has";
}

// =====================================================================
// SST on the shared wall treatment
// =====================================================================
//
// Same harness, same meshes, same metrics -- only the turbulence model differs.
// That is deliberate: a separate SST benchmark would eventually measure
// something slightly different and the comparison below would stop meaning
// anything.

TEST(SSTWallFunctionChannel, LowReStressSweep) {
  const auto records = runSweep(kReTau180, kFamily, std::size(kFamily), Model::SST);
  for (const auto& record : records) {
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
    EXPECT_TRUE(std::isfinite(record.achievedReTau)) << record.label;
  }
  const auto accepted = std::count_if(records.begin(), records.end(),
                                      [](const SweepRecord& r) { return r.satisfiesG1; });
  EXPECT_GT(accepted, 0);
  EXPECT_LT(accepted, static_cast<long>(records.size()));
}

TEST(SSTWallFunctionChannel, HighReSweep) {
  const auto records = runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST);
  for (const auto& record : records) {
    EXPECT_TRUE(record.converged) << record.label << " did not converge";
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
  }
  const auto accepted = std::count_if(records.begin(), records.end(),
                                      [](const SweepRecord& r) { return r.satisfiesG1; });
  EXPECT_GT(accepted, 0) << "no SST mesh satisfies G1 at Re_tau = 590";
}

TEST(SSTWallFunctionChannel, ImprovesOnThePreP16SSTBaseline) {
  // The preserved SST baseline measured E_Re_tau = 0.270 (64x16, y+ 8.21) with
  // WallOmega -- a viscous-sublayer asymptotic applied at log-layer spacing.
  // If the shared wall treatment cannot beat that, it is not an improvement for
  // SST and no SST gate should be derived from it.
  constexpr Real kBaselineReTauError = 0.270;

  Real best = std::numeric_limits<Real>::infinity();
  std::string bestLabel;
  for (const auto& meshCase : kFamily590) {
    const SweepRecord record = runMesh(kReTau590, meshCase, /*wallFunctions=*/true, Model::SST);
    if (!record.satisfiesG1) continue;
    if (record.reTauError < best) {
      best = record.reTauError;
      bestLabel = record.label;
    }
  }
  ASSERT_TRUE(std::isfinite(best)) << "no G1-satisfying SST mesh produced a finite Re_tau";
  std::printf("\nbest G1-valid SST mesh: %s, E_Re_tau = %.4f (pre-P16 SST baseline %.3f)\n",
              bestLabel.c_str(), best, kBaselineReTauError);
  EXPECT_LT(best, kBaselineReTauError);
}

TEST(SSTWallFunctionChannel, IsDeterministic) {
  const SweepRecord first = runMesh(kReTau590, kFamily590[3], true, Model::SST);
  const SweepRecord second = runMesh(kReTau590, kFamily590[3], true, Model::SST);
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.achievedReTau, second.achievedReTau);
  EXPECT_EQ(first.uPlusLInf, second.uPlusLInf);
}

// THE ADOPTED SST GATES.
//
// Derived by the same frozen procedure, from SST's OWN sweeps -- not inherited
// from k-epsilon. The evidence happens to support the same numbers at
// Re_tau = 180 and TIGHTER ones at Re_tau = 590, which is a result rather than
// an assumption:
//
//   Re_tau = 180, G1-valid {ny02,ny03,ny04}: worst 0.2091 / 3.6990 -> 0.22 / 3.9
//   Re_tau = 590, G1-valid {ny06,ny08,ny12,ny16}: worst 0.0556 / 1.3072 -> 0.059 / 1.4
constexpr Real kSSTLowReGateReTauError = 0.22;
constexpr Real kSSTLowReGateUPlusLInf = 3.9;
constexpr Real kSSTHighReGateReTauError = 0.059;
constexpr Real kSSTHighReGateUPlusLInf = 1.4;

TEST(SSTWallFunctionChannel, EveryG1ValidLowReMeshPassesItsGate) {
  const auto records = runSweep(kReTau180, kFamily, std::size(kFamily), Model::SST);
  Index valid = 0;
  for (const auto& record : records) {
    if (!record.satisfiesG1) continue;
    ++valid;
    EXPECT_LE(record.reTauError, kSSTLowReGateReTauError) << record.label;
    EXPECT_LE(record.uPlusLInf, kSSTLowReGateUPlusLInf) << record.label;
  }
  EXPECT_GE(valid, 3u);
}

TEST(SSTWallFunctionChannel, EveryG1ValidHighReMeshPassesItsGate) {
  const auto records = runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST);
  Index valid = 0;
  for (const auto& record : records) {
    if (!record.satisfiesG1) continue;
    ++valid;
    EXPECT_TRUE(record.converged) << record.label;
    EXPECT_LE(record.reTauError, kSSTHighReGateReTauError) << record.label;
    EXPECT_LE(record.uPlusLInf, kSSTHighReGateUPlusLInf) << record.label;
    EXPECT_EQ(record.facesWithFlooredViscosity, 0u) << record.label;
  }
  EXPECT_GE(valid, 4u);
}

// =====================================================================
// P16b -- THE AUTOMATIC (BLENDED) NEAR-WALL TREATMENT ON THE SAME CHANNELS
// =====================================================================
//
// Section 9's requirement, in order: the explicit high-Re path must first
// reproduce the validated results (the suites above, unchanged, are that
// evidence), and only then is the automatic treatment measured on the SAME
// channels, the SAME meshes and the SAME metrics.
//
// The adopted SST gates are NOT re-derived here. They are the frozen high-Re
// numbers, applied unchanged to the automatic treatment -- a blended treatment
// that cannot meet the gate its predecessor met on attached flow has damaged
// canonical channel flow, whatever it does for the backward-facing step.

TEST(SSTAutomaticWallChannel, TheDefaultCaseFileIsBitwiseTheHighRePath) {
  // The case-file counterpart of WallTreatmentMode.DefaultIsBitwise... : a
  // physics.json with no "wall_treatment" key must produce EXACTLY the run
  // that produced the preserved evidence, and an explicit "high_re" must
  // produce exactly the same thing again.
  const SweepRecord implicitDefault = runMesh(kReTau590, kFamily590[3], true, Model::SST);
  const SweepRecord explicitHighRe = runMesh(kReTau590, kFamily590[3], true, Model::SST, "high_re");

  EXPECT_EQ(implicitDefault.iterations, explicitHighRe.iterations);
  EXPECT_EQ(implicitDefault.wallShearStress, explicitHighRe.wallShearStress);
  EXPECT_EQ(implicitDefault.achievedReTau, explicitHighRe.achievedReTau);
  EXPECT_EQ(implicitDefault.uPlusLInf, explicitHighRe.uPlusLInf);
  EXPECT_EQ(implicitDefault.facesOutsideValidBand, explicitHighRe.facesOutsideValidBand);
  EXPECT_EQ(implicitDefault.facesWithFlooredViscosity, explicitHighRe.facesWithFlooredViscosity);
}

TEST(SSTAutomaticWallChannel, HighReSweep) {
  const auto records =
      runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST, "automatic");
  for (const auto& record : records) {
    EXPECT_TRUE(record.converged) << record.label << " did not converge";
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
    EXPECT_TRUE(std::isfinite(record.achievedReTau)) << record.label;
    // Structural, and proved at unit level: the blend reaches the molecular
    // value through its own viscous branch, so it never needs the floor.
    EXPECT_EQ(record.facesWithFlooredViscosity, 0u)
        << record.label << " floored mu_t under the automatic treatment";
    // Every wall face must be accounted for by exactly one branch -- the
    // diagnostic that replaces G1 for this mode (section 6).
    // Both the bottom and the top patch are walls, so the mesh has exactly
    // 2*kNx wall faces and every one of them must be attributed to exactly one
    // branch. (An earlier draft of this line compared an expression with
    // itself and would have passed on any input.)
    EXPECT_EQ(record.viscousSublayerFaces + record.blendedFaces + record.logLayerFaces, 2u * kNx)
        << record.label << ": branch counts do not account for every wall face";
  }
}

TEST(SSTAutomaticWallChannel, DoesNotRegressTheAdoptedHighReGate) {
  // Section 9's "do not accept a blended treatment that fixes BFS while
  // damaging canonical channel flow", stated as the frozen gate rather than as
  // a judgement. Evaluated on the SAME G1-valid mesh subset the high-Re
  // evidence used, so the comparison is like-for-like.
  const auto highRe = runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST);
  const auto automatic =
      runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST, "automatic");
  ASSERT_EQ(highRe.size(), automatic.size());

  std::printf("\nRe_tau = 590 -- high-Re vs automatic on the same meshes\n");
  std::printf("%-8s %8s %10s %10s %10s %10s %6s\n", "mesh", "y1+", "E hiRe", "E auto", "u+ hiRe",
              "u+ auto", "G1");
  Index valid = 0;
  for (std::size_t i = 0; i < automatic.size(); ++i) {
    std::printf("%-8s %8.3f %10.4f %10.4f %10.4f %10.4f %6s\n", automatic[i].label.c_str(),
                automatic[i].firstCellYPlus, highRe[i].reTauError, automatic[i].reTauError,
                highRe[i].uPlusLInf, automatic[i].uPlusLInf,
                highRe[i].satisfiesG1 ? "OK" : "REJECT");
    if (!highRe[i].satisfiesG1) continue;
    ++valid;
    EXPECT_TRUE(automatic[i].converged) << automatic[i].label;
    EXPECT_LE(automatic[i].reTauError, kSSTHighReGateReTauError)
        << automatic[i].label << ": the automatic treatment fails the ADOPTED high-Re gate "
        << "that the high-Re path passes (" << highRe[i].reTauError << ")";
    EXPECT_LE(automatic[i].uPlusLInf, kSSTHighReGateUPlusLInf) << automatic[i].label;
  }
  EXPECT_GE(valid, 4u) << "the G1-valid subset shrank, so this is not the same comparison";
}

TEST(SSTAutomaticWallChannel, LowReStressSweep) {
  const auto records = runSweep(kReTau180, kFamily, std::size(kFamily), Model::SST, "automatic");
  for (const auto& record : records) {
    EXPECT_TRUE(std::isfinite(record.wallShearStress)) << record.label;
    EXPECT_TRUE(std::isfinite(record.achievedReTau)) << record.label;
    EXPECT_EQ(record.facesWithFlooredViscosity, 0u) << record.label;
  }
}

TEST(SSTAutomaticWallChannel, DoesNotRegressTheAdoptedLowReGate) {
  const auto highRe = runSweep(kReTau180, kFamily, std::size(kFamily), Model::SST);
  const auto automatic = runSweep(kReTau180, kFamily, std::size(kFamily), Model::SST, "automatic");
  ASSERT_EQ(highRe.size(), automatic.size());

  Index valid = 0;
  for (std::size_t i = 0; i < automatic.size(); ++i) {
    if (!highRe[i].satisfiesG1) continue;
    ++valid;
    EXPECT_LE(automatic[i].reTauError, kSSTLowReGateReTauError)
        << automatic[i].label << ": automatic " << automatic[i].reTauError << " vs high-Re "
        << highRe[i].reTauError;
    EXPECT_LE(automatic[i].uPlusLInf, kSSTLowReGateUPlusLInf) << automatic[i].label;
  }
  EXPECT_GE(valid, 3u);
}

TEST(SSTAutomaticWallChannel, IsDeterministic) {
  const SweepRecord first = runMesh(kReTau590, kFamily590[3], true, Model::SST, "automatic");
  const SweepRecord second = runMesh(kReTau590, kFamily590[3], true, Model::SST, "automatic");
  EXPECT_EQ(first.iterations, second.iterations);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.achievedReTau, second.achievedReTau);
  EXPECT_EQ(first.uPlusLInf, second.uPlusLInf);
  EXPECT_EQ(first.viscousSublayerFaces, second.viscousSublayerFaces);
  EXPECT_EQ(first.blendedFaces, second.blendedFaces);
  EXPECT_EQ(first.logLayerFaces, second.logLayerFaces);
}

TEST(SSTAutomaticWallChannel, StaysWellBehavedOnMeshesTheHighRePathRejects) {
  // A CORRECTION to what this test was first written to assert.
  //
  // The premise was that the high-Re treatment degrades under refinement once
  // the first cell drops below y+ = 30, so the automatic treatment would have
  // to beat it there. The preserved Re_tau = 590 evidence says otherwise: on
  // this channel the high-Re error FALLS as the mesh is refined past the band
  // (ny20 0.0056, ny24 0.0038, ny32 0.0018), better than several G1-valid
  // meshes. Asserting "automatic must be better" would have been asserting a
  // false premise against an already-excellent baseline.
  //
  // WHY the channel does not reproduce the Driver & Seegmiller failure: it is
  // attached everywhere, so tau_w is bounded away from zero at every wall face
  // and the log law always has something to stand on. The BFS failure is
  // specifically about tau_w -> 0 at separation and reattachment, which no
  // attached channel contains. The channel is therefore a NON-REGRESSION check
  // and cannot be evidence that the mode fixes anything -- that evidence has to
  // come from the separated-flow benchmark.
  //
  // So what is asserted here is only that refining past the band does not break
  // the automatic treatment, measured against the frozen gate rather than
  // against a number chosen after the fact.
  const auto highRe = runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST);
  const auto automatic =
      runSweep(kReTau590, kFamily590, std::size(kFamily590), Model::SST, "automatic");
  ASSERT_EQ(highRe.size(), automatic.size());

  Index rejected = 0;
  for (std::size_t i = 0; i < automatic.size(); ++i) {
    if (highRe[i].satisfiesG1) continue;
    ++rejected;
    std::printf(
        "G1-rejected %-8s y1+ = %7.3f : E high-Re = %.4f, E automatic = %.4f "
        "(sublayer %zu / blended %zu / log %zu)\n",
        automatic[i].label.c_str(), automatic[i].firstCellYPlus, highRe[i].reTauError,
        automatic[i].reTauError, automatic[i].viscousSublayerFaces, automatic[i].blendedFaces,
        automatic[i].logLayerFaces);
    EXPECT_TRUE(automatic[i].converged) << automatic[i].label;
    EXPECT_TRUE(std::isfinite(automatic[i].achievedReTau)) << automatic[i].label;
    EXPECT_EQ(automatic[i].facesWithFlooredViscosity, 0u) << automatic[i].label;
    EXPECT_LE(automatic[i].reTauError, kSSTHighReGateReTauError)
        << automatic[i].label << ": refining past the validity band broke the automatic treatment";
  }
  ASSERT_GE(rejected, 1u) << "no mesh in this family is G1-rejected, so this test proves nothing";
}

// =====================================================================
// P16b attempt 3 -- THE ARCHITECTURE DIAGNOSTIC (section 4)
// =====================================================================
//
// The hypothesis is that the automatic treatment fails the attached-flow gate
// because it still modifies the wall-adjacent CELL (imposed omega, replaced
// production, substituted wall viscosity) instead of applying a wall-FACE
// boundary condition and letting the SST PDE solve that cell.
//
// The cheapest possible test of that hypothesis requires NO new physics and no
// production change at all, because the architecture in question already
// exists and is untouched: SSTConfig::wallFunctions = false IS
//
//     wall BC at the face (k = 0, omega = 60 nu/(beta1 y^2))
//       -> full SST transport solved in the first cell
//       -> momentum uses the natural effective viscosity
//
// So the question "does letting SST solve the near-wall cell restore the
// Re_tau = 590 channel result?" is answered by running the EXISTING low-Re path
// on the meshes where the automatic treatment failed.
//
// This is a DIAGNOSTIC, not a gate: the low-Re path has its own known
// limitation (it is not meant for y+ ~ 40-95 either), and what is being read
// off is the DIRECTION of the answer, not a pass.
TEST(WallArchitectureDiagnostic, LowReSSTOnTheMeshesTheAutomaticTreatmentFailed) {
  std::printf("\nARCHITECTURE DIAGNOSTIC -- Re_tau = 590, SST\n");
  std::printf("low-Re = wallFunctions OFF: wall-face BC + full SST PDE in the first cell\n");
  std::printf("%-8s %9s %11s %11s %11s %11s %11s %9s\n", "mesh", "y1+(hiRe)", "E hiRe", "E auto",
              "E lowRe", "u+ hiRe", "u+ lowRe", "conv");

  for (const auto& meshCase : kFamily590) {
    const SweepRecord highRe = runMesh(kReTau590, meshCase, /*wallFunctions=*/true, Model::SST);
    const SweepRecord automatic =
        runMesh(kReTau590, meshCase, /*wallFunctions=*/true, Model::SST, "automatic");
    const SweepRecord lowRe = runMesh(kReTau590, meshCase, /*wallFunctions=*/false, Model::SST);

    std::printf("%-8s %9.3f %11.4f %11.4f %11.4f %11.4f %11.4f %9d\n", highRe.label.c_str(),
                highRe.firstCellYPlus, highRe.reTauError, automatic.reTauError, lowRe.reTauError,
                highRe.uPlusLInf, lowRe.uPlusLInf, lowRe.converged ? 1 : 0);

    EXPECT_TRUE(std::isfinite(lowRe.reTauError)) << lowRe.label;
  }
  SUCCEED() << "diagnostic only -- read the table, no threshold applied here";
}

// The single cheapest discriminating case, run on its own so it can be invoked
// without the whole family: ny16 is where the automatic treatment first breaks
// the adopted gate (E_Re_tau = 0.0698 > 0.059) while the high-Re path passes
// comfortably (0.0059).
TEST(WallArchitectureDiagnostic, Ny16IsTheDecidingCase) {
  const MeshCase ny16{"ny16", 16};
  const SweepRecord highRe = runMesh(kReTau590, ny16, /*wallFunctions=*/true, Model::SST);
  const SweepRecord automatic =
      runMesh(kReTau590, ny16, /*wallFunctions=*/true, Model::SST, "automatic");
  const SweepRecord lowRe = runMesh(kReTau590, ny16, /*wallFunctions=*/false, Model::SST);

  std::printf("\nny16 (y+ ~ %.1f) -- the case that decides the architecture hypothesis\n",
              highRe.firstCellYPlus);
  std::printf("  high-Re wall function : Re_tau %8.3f  E %.4f  u+ Linf %.4f  iters %zu\n",
              highRe.achievedReTau, highRe.reTauError, highRe.uPlusLInf, highRe.iterations);
  std::printf("  automatic (blended)   : Re_tau %8.3f  E %.4f  u+ Linf %.4f  iters %zu\n",
              automatic.achievedReTau, automatic.reTauError, automatic.uPlusLInf,
              automatic.iterations);
  std::printf("  low-Re (PDE in cell)  : Re_tau %8.3f  E %.4f  u+ Linf %.4f  iters %zu\n",
              lowRe.achievedReTau, lowRe.reTauError, lowRe.uPlusLInf, lowRe.iterations);
  std::printf("  adopted gate          : E <= %.3f, u+ Linf <= %.1f\n", kSSTHighReGateReTauError,
              kSSTHighReGateUPlusLInf);
  std::printf("  HYPOTHESIS %s: letting the PDE solve the cell %s the gate here\n",
              lowRe.reTauError <= kSSTHighReGateReTauError ? "SUPPORTED" : "NOT SUPPORTED",
              lowRe.reTauError <= kSSTHighReGateReTauError ? "RESTORES" : "does NOT restore");

  EXPECT_TRUE(std::isfinite(lowRe.reTauError));
  SUCCEED() << "diagnostic only";
}

// P16b attempt 4 -- the composite (Spalding) wall shear on the four
// previously discriminating Re_tau = 590 meshes only. Cheap by design: the
// full family is run only if this clears the gate.
TEST(SSTCompositeWallChannel, DiscriminatingMeshesAtReTau590) {
  const MeshCase cases[] = {{"ny06", 6}, {"ny12", 12}, {"ny16", 16}, {"ny32", 32}};
  std::printf("\nRe_tau = 590 -- composite (Spalding) wall shear\n");
  std::printf("%-6s %8s %10s %10s %10s %10s %10s %10s %6s\n", "mesh", "y1+", "E hiRe", "E blend",
              "E comp", "u+ hiRe", "u+ comp", "iters", "G1");

  Index gated = 0;
  for (const auto& meshCase : cases) {
    const SweepRecord highRe = runMesh(kReTau590, meshCase, true, Model::SST);
    const SweepRecord blend = runMesh(kReTau590, meshCase, true, Model::SST, "automatic");
    const SweepRecord composite =
        runMesh(kReTau590, meshCase, true, Model::SST, "automatic_spalding");

    std::printf("%-6s %8.3f %10.4f %10.4f %10.4f %10.4f %10.4f %10zu %6s\n", meshCase.label,
                highRe.firstCellYPlus, highRe.reTauError, blend.reTauError, composite.reTauError,
                highRe.uPlusLInf, composite.uPlusLInf, composite.iterations,
                highRe.satisfiesG1 ? "OK" : "REJECT");

    EXPECT_TRUE(composite.converged) << meshCase.label;
    EXPECT_EQ(composite.facesWithFlooredViscosity, 0u) << meshCase.label;
    // The gate is applied on the SAME G1-valid subset the adopted thresholds
    // were derived from -- unchanged, not re-derived.
    if (!highRe.satisfiesG1) continue;
    ++gated;
    EXPECT_LE(composite.reTauError, kSSTHighReGateReTauError)
        << meshCase.label << ": composite " << composite.reTauError << " vs blend "
        << blend.reTauError << " vs high-Re " << highRe.reTauError;
    EXPECT_LE(composite.uPlusLInf, kSSTHighReGateUPlusLInf) << meshCase.label;
  }
  EXPECT_GE(gated, 3u) << "fewer G1-valid meshes than expected in this subset";
}

TEST(SSTCompositeWallChannel, HighRePathIsStillBitwiseUnchanged) {
  // Re-asserted here because attempt 4 touched WallFunctions.cpp again.
  const SweepRecord implicitDefault = runMesh(kReTau590, kFamily590[3], true, Model::SST);
  const SweepRecord explicitHighRe = runMesh(kReTau590, kFamily590[3], true, Model::SST, "high_re");
  EXPECT_EQ(implicitDefault.wallShearStress, explicitHighRe.wallShearStress);
  EXPECT_EQ(implicitDefault.achievedReTau, explicitHighRe.achievedReTau);
  EXPECT_EQ(implicitDefault.iterations, explicitHighRe.iterations);
}

// P16b attempt 5 -- the published two-scale momentum wall flux, F_U = -rho u_tau u*,
// against every treatment tried so far on the four discriminating meshes.
TEST(SSTMomentumFluxChannel, FourWayComparisonAtReTau590) {
  const MeshCase cases[] = {{"ny06", 6}, {"ny12", 12}, {"ny16", 16}, {"ny32", 32}};
  std::printf("\nRe_tau = 590 -- four wall treatments, same meshes, same metrics\n");
  std::printf("%-6s %8s %9s %9s %9s %9s %9s %9s %9s %9s %7s %6s\n", "mesh", "y1+", "E hiRe",
              "E blend", "E comp", "E flux", "u+ hiRe", "u+ flux", "tau flux", "u_tau flx", "iters",
              "G1");

  Index gated = 0;
  for (const auto& meshCase : cases) {
    const SweepRecord highRe = runMesh(kReTau590, meshCase, true, Model::SST);
    const SweepRecord blend = runMesh(kReTau590, meshCase, true, Model::SST, "automatic");
    const SweepRecord comp = runMesh(kReTau590, meshCase, true, Model::SST, "automatic_spalding");
    const SweepRecord flux =
        runMesh(kReTau590, meshCase, true, Model::SST, "automatic_momentum_flux");

    std::printf("%-6s %8.3f %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f %9.3e %9.5f %7zu %6s\n",
                meshCase.label, highRe.firstCellYPlus, highRe.reTauError, blend.reTauError,
                comp.reTauError, flux.reTauError, highRe.uPlusLInf, flux.uPlusLInf,
                flux.wallShearStress, flux.frictionVelocity, flux.iterations,
                highRe.satisfiesG1 ? "OK" : "REJECT");

    EXPECT_TRUE(flux.converged) << meshCase.label;
    if (!highRe.satisfiesG1) continue;
    ++gated;
    // BOTH adopted metrics, unchanged. Passing only E_Re_tau is not a pass.
    EXPECT_LE(flux.reTauError, kSSTHighReGateReTauError) << meshCase.label;
    EXPECT_LE(flux.uPlusLInf, kSSTHighReGateUPlusLInf) << meshCase.label;
  }
  EXPECT_GE(gated, 3u);
}
