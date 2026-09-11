# cfdcore: the library target for the current build-system phase.
#
# src/core (P0 -- Core), src/mesh (P0 -- Mesh), src/fields (P0 -- Fields),
# src/algebra (P0 -- Linear Algebra), src/boundary (P0 -- Boundary
# Conditions), src/discretization (P0 -- Finite Volume Operators), and
# src/physics (P0 -- Incompressible Physics: FluidProperties, MassFlux,
# ContinuityEquation, MomentumEquation) are wired up here, along with
# src/pressure_velocity (P0 -- SIMPLE) and src/io (P1 --
# Case System / Result Export). src/solver (P2 -- Transient CFD) has only
# TimeController.cpp so far (TASK P2-001) -- the rest of that phase's
# subdirectories are still pre-scaffolded but intentionally not added as
# sources yet -- most are still empty placeholders and compiling them now
# would create linker noise for no benefit. src/thermal (P2 -- Thermal)
# is complete (see TODO.md). src/turbulence (P2 -- Turbulence) has only
# TurbulenceModel.cpp so far (P2-TURB-001: the abstract interface) --
# no concrete RANS model exists yet, and nothing outside src/turbulence
# calls into it.

configure_file(
  "${CMAKE_SOURCE_DIR}/include/cfd/core/Version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/include/cfd/core/Version.hpp"
  @ONLY
)

add_library(cfdcore STATIC
  core/Version.cpp
  core/Logger.cpp
  core/Timer.cpp
  # P4 -- Performance: category-based profiling accumulator (Profiler/
  # ScopedTimer), built on Timer -- see its own header comment.
  core/Profiler.cpp
)
# core/Exception.hpp is header-only: every exception type only inherits
# constructors (using Base::Base) and adds no out-of-line members, so
# there is nothing to put in a .cpp.
add_library(cfdapp::core ALIAS cfdcore)

target_sources(cfdcore
  PRIVATE
    mesh/Cell.cpp
    mesh/Face.cpp
    mesh/BoundaryPatch.cpp
    mesh/Mesh.cpp
    mesh/MeshGeometry.cpp
    mesh/MeshQuality.cpp
    mesh/MeshFingerprint.cpp
)

target_sources(cfdcore
  PRIVATE
    fields/ScalarField.cpp
    fields/VectorField.cpp
)
# Field<T> and SurfaceField are header-only (a template, and a plain
# inherited-constructor alias respectively) -- no .cpp needed for either.

target_sources(cfdcore
  PRIVATE
    algebra/Vector.cpp
    algebra/SparseMatrix.cpp
    algebra/LinearSystem.cpp
    algebra/LinearSolver.cpp
    algebra/Preconditioner.cpp
    algebra/CG.cpp
    algebra/BiCGSTAB.cpp
    algebra/LinearSolverFactory.cpp
)

target_sources(cfdcore
  PRIVATE
    boundary/BoundaryCondition.cpp
    boundary/FixedValue.cpp
    boundary/FixedGradient.cpp
    boundary/Wall.cpp
    boundary/MovingWall.cpp
    boundary/Inlet.cpp
    boundary/Outlet.cpp
    boundary/Symmetry.cpp
    # P2 -- Thermal, P2-THERMAL-003: thermal-specific scalar boundary
    # conditions, placed alongside the other domain-named BC classes
    # above rather than under thermal/ (see FixedTemperature.hpp's own
    # header comment for why).
    boundary/FixedTemperature.cpp
    boundary/HeatFlux.cpp
    boundary/Adiabatic.cpp
    # P2-TURB-006: SST's near-wall omega Dirichlet value, same
    # domain-named-BC-alongside-the-generic-ones precedent as the
    # thermal conditions above (see WallOmega.hpp's own header comment).
    boundary/WallOmega.cpp
)

target_sources(cfdcore
  PRIVATE
    discretization/Interpolation.cpp
    discretization/Gradient.cpp
    discretization/Divergence.cpp
    discretization/Diffusion.cpp
    discretization/Laplacian.cpp
    discretization/Convection.cpp
    # P2 -- Transient CFD, TASK P2-002: implicit Euler time derivative.
    discretization/TimeDerivative.cpp
    # P2 -- Turbulence, P2-TURB-004: plain Green-Gauss gradient of a
    # VectorField (grad(u), grad(v)), needed for turbulent production --
    # see VectorGradient.hpp's own header comment on why Gradient.hpp's
    # scalar gradient() cannot be reused directly for this.
    discretization/VectorGradient.cpp
)

target_sources(cfdcore
  PRIVATE
    physics/FluidProperties.cpp
    physics/MassFlux.cpp
    physics/ContinuityEquation.cpp
    physics/MomentumEquation.cpp
    # P3-PHYS-001: Boussinesq buoyancy body-force model.
    physics/BoussinesqBuoyancy.cpp
    # P3-PHYS-003: temperature-dependent property models.
    physics/TemperatureProperty.cpp
)

target_sources(cfdcore
  PRIVATE
    pressure_velocity/SIMPLESettings.cpp
    pressure_velocity/UnderRelaxation.cpp
    pressure_velocity/PressureCorrectionEquation.cpp
    pressure_velocity/RelaxedMomentum.cpp
    pressure_velocity/SIMPLE.cpp
    # P2 -- Transient CFD, TASK P2-005: the transient momentum predictor
    # building block (PISO-A/B/C), plus the actual one-timestep PISO
    # class (PISO-H) composing it with the already-validated pressure-
    # correction machinery -- no TransientSolver wiring yet (PISO-I).
    pressure_velocity/TransientMomentum.cpp
    pressure_velocity/PISO.cpp
)

# P2 -- Transient CFD: TimeController (P2-001), CFL (P2-003), and
# TransientSolver (P2-004) -- the full PISO algorithm now lives under
# pressure_velocity/ (see PISO.cpp above). RestartSnapshot (Restart-A):
# the restart data model + validation only -- no file I/O yet.
target_sources(cfdcore
  PRIVATE
    solver/TimeController.cpp
    solver/CFL.cpp
    solver/TransientSolver.cpp
    solver/RestartSnapshot.cpp
)

# io/case/*Parser.cpp and io/case/JsonUtil.cpp are compiled into cfdcore
# but never installed as public headers (they live under src/, not
# include/) -- nlohmann::json stays an implementation detail of the case-
# loading pipeline, never visible to anything that only includes
# cfd/io/CaseReader.hpp or the typed config headers under
# cfd/io/case/*.hpp (P1 -- Case System).
target_sources(cfdcore
  PRIVATE
    io/CaseReader.cpp
    io/CaseWriter.cpp
    io/CaseBuilder.cpp
    io/case/JsonUtil.cpp
    io/case/CaseConfigParser.cpp
    io/case/GeometryConfigParser.cpp
    io/case/MeshConfigParser.cpp
    io/case/PhysicsConfigParser.cpp
    io/case/BoundaryConfigParser.cpp
    io/case/SolverConfigParser.cpp
)

# P1 -- Result Export: CSV/JSON/VTK writers plus the ResultExporter
# orchestrator. StructuredMeshInfo.cpp is the shared export-only
# structured-grid-geometry inference VTKWriter/JSONWriter both use (see
# its own header comment) -- not part of the public API, same convention
# as io/case/JsonUtil.cpp.
target_sources(cfdcore
  PRIVATE
    io/StructuredMeshInfo.cpp
    io/CSVWriter.cpp
    io/JSONWriter.cpp
    io/VTKWriter.cpp
    io/ResultExporter.cpp
)

# P2 -- Restart capability, Restart-C/D: the restart file writer/reader.
# RestartReader.cpp reuses io/case/JsonUtil.cpp's parsing helpers (same
# "case/JsonUtil.hpp" relative include CaseReader.cpp itself uses).
target_sources(cfdcore
  PRIVATE
    io/RestartWriter.cpp
    io/RestartReader.cpp
)

# P2 -- Thermal: ThermalProperties (P2-THERMAL-001) and the standalone
# energy-equation assembly (P2-THERMAL-002, mirrors
# physics/MomentumEquation.cpp's contribution-level design). Thermal
# boundary condition classes are a later task in this same phase (see
# TODO.md "P2 -- Thermal").
target_sources(cfdcore
  PRIVATE
    thermal/ThermalProperties.cpp
    thermal/EnergyEquation.cpp
    thermal/ThermalSolver.cpp
    # P2-THERMAL-005: conjugate heat-transfer foundation (region model +
    # interface-conductance-aware conduction assembly).
    thermal/ThermalRegionMap.cpp
    thermal/ThermalInterface.cpp
)

# P2 -- Turbulence: the abstract TurbulenceModel interface (P2-TURB-001),
# LaminarModel (P2-TURB-002, mu_t=0 everywhere), SIMPLE/PISO's RANS
# framework (P2-TURB-003 -- lives under pressure_velocity/, not here),
# and the standard k-epsilon model (P2-TURB-004): TurbulenceProduction
# (turbulent production P_k), KEpsilonEquation (model-agnostic scalar-
# transport assembly reused by both k and epsilon), and KEpsilonModel
# itself (see TODO.md "P2 -- Turbulence").
target_sources(cfdcore
  PRIVATE
    turbulence/TurbulenceModel.cpp
    turbulence/LaminarModel.cpp
    turbulence/TurbulenceProduction.cpp
    turbulence/KEpsilonEquation.cpp
    turbulence/KEpsilonModel.cpp
    # P2-TURB-005: standard k-omega -- KOmegaEquation.cpp holds only the
    # linear effective-diffusivity formula distinct from k-epsilon's own
    # (see that file's own header comment); everything else k-omega's
    # scalar-transport assembly needs is reused directly from
    # KEpsilonEquation.cpp above.
    turbulence/KOmegaEquation.cpp
    turbulence/KOmegaModel.cpp
    # P2-TURB-006: Menter SST -- WallDistance.cpp (reusable, computed
    # once per model construction) and SSTEquation.cpp (F1/F2 blending,
    # cross-diffusion, the eddy-viscosity/production limiters -- the
    # only genuinely SST-specific formulas; everything else reuses
    # KEpsilonEquation.cpp's model-agnostic scalar-transport assembly
    # and TurbulenceProduction.cpp's strain/production machinery).
    turbulence/WallDistance.cpp
    turbulence/SSTEquation.cpp
    turbulence/SSTModel.cpp
    turbulence/TurbulenceModelFactory.cpp
)

# P3-PHYS-004: passive, non-reacting species transport -- mirrors
# src/thermal/'s own layering (Properties value type, contribution-level
# Equation assembly, Solver outer-Picard-loop layer).
target_sources(cfdcore
  PRIVATE
    species/SpeciesProperties.cpp
    species/SpeciesEquation.cpp
    species/SpeciesSolver.cpp
)

# P3-PHYS-005: two-phase volume-fraction foundation -- PhaseProperties/
# TwoPhaseSystem (identity + linear mixture laws), VolumeFractionEquation
# (transient, convection-only assembly -- see its own header comment for
# why no diffusion term exists here), VolumeFractionSolver (single-
# timestep solving layer, mirrors PISO's own "no TransientSolver wiring
# yet" scope boundary).
target_sources(cfdcore
  PRIVATE
    multiphase/PhaseProperties.cpp
    multiphase/MultiphaseProperties.cpp
    multiphase/VolumeFractionEquation.cpp
    multiphase/VolumeFractionSolver.cpp
)

# P3-PHYS-006: single-phase ideal-gas compressible foundation --
# EquationOfState/IdealGasEOS/ThermodynamicProperties, the canonical
# compressible mass flux, the transient compressible continuity
# diagnostic, and the compressible momentum transient-storage term +
# combiner (reusing physics::MomentumEquation's own contribution
# assemblers unchanged). No compressible pressure-correction/SIMPLE
# solve is added -- see TODO.md's own P3-PHYS-006 status note for the
# disclosed scope boundary.
target_sources(cfdcore
  PRIVATE
    compressible/IdealGasEOS.cpp
    compressible/ThermodynamicProperties.cpp
    compressible/CompressibleMassFlux.cpp
    compressible/CompressibleContinuity.cpp
    compressible/CompressibleMomentum.cpp
)

# P4 -- Performance: GPU backend detection (cfd::gpu::cudaAvailable).
# The stub here (always returns false) is compiled in only when CUDA is
# NOT enabled; the real, runtime-checking implementation instead lives in
# cuda/kernels/CsrSpmvKernel.cu (part of the cfdcuda target, linked below)
# -- exactly one of the two is ever compiled, see GPUBackend.hpp's own
# header comment.
if(NOT CFDAPP_ENABLE_CUDA)
  target_sources(cfdcore
    PRIVATE
      gpu/GPUBackend.cpp
      gpu/GpuResidencyManager.cpp
      gpu/GpuLinearSolver.cpp
  )
endif()

# P6-PERF-001 -- Performance: transfer/allocation/kernel-launch counters.
# Plain counters, no CUDA dependency -- always compiled, in both build
# configurations, so a CPU-only build can honestly report "zero GPU
# activity" via the same API a CUDA build uses to prove reduced transfer
# volume (see GPUExecutionStats.hpp's own header comment).
target_sources(cfdcore
  PRIVATE
    gpu/GPUExecutionStats.cpp
)

# P5 -- Application: ProjectRunner, the one production solver-backend
# entry point shared by apps/cli and apps/gui (section 0's "ONE SOLVER
# BACKEND" -- see include/cfd/app/ProjectRunner.hpp's own header
# comment).
target_sources(cfdcore
  PRIVATE
    app/ProjectRunner.cpp
    app/CaseSession.cpp
    app/VisualizationSnapshot.cpp
)

# P5-C/D/E/G -- Field Visualization, Contours, Vector Plots, Post-
# processing: pure algorithmic building blocks (no Qt, no rendering) a
# future GUI's visualization/post-processing views consume -- see each
# header's own comment.
target_sources(cfdcore
  PRIVATE
    viz/MarchingSquares.cpp
    viz/DerivedFields.cpp
    viz/FieldProbe.cpp
    viz/VectorSampling.cpp
)

target_include_directories(cfdcore
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/include>"
)

target_link_libraries(cfdcore
  PRIVATE
    cfdapp::compiler_warnings
    nlohmann_json::nlohmann_json
  PUBLIC
    # PUBLIC (unlike compiler_warnings, above): a sanitized cfdcore
    # linked into a non-sanitized executable/test binary is an ODR/
    # runtime-mismatch hazard, so every consumer of cfdapp::core must
    # inherit the same sanitizer flags, not just cfdcore's own
    # translation units (TODO.md "P1 -- Quality Gate" section 9-10).
    cfdapp::sanitizers
)

# P4 -- Performance: only when CFDAPP_ENABLE_OPENMP=ON (cmake/OpenMP.cmake
# already guarantees OpenMP_CXX_FOUND by the time this runs, or turns the
# option back off) -- PUBLIC for the same ODR/runtime-mismatch reason
# cfdapp::sanitizers is PUBLIC above: a TU built with -fopenmp calling into
# one built without it (or vice versa) through the same SparseMatrix code
# is a correctness hazard, not just a missed optimization.
if(CFDAPP_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
  target_link_libraries(cfdcore PUBLIC OpenMP::OpenMP_CXX)
endif()

# P4 -- Performance: cfdcuda supplies the real cfd::gpu::cudaAvailable()/
# csrSpmvCuda() when CUDA is enabled (see the gpu/GPUBackend.cpp
# target_sources guard above).
if(CFDAPP_ENABLE_CUDA)
  target_link_libraries(cfdcore PUBLIC cfdapp::cuda)

  # P6-PERF-001 -- Performance: the reverse edge of a genuine link-time
  # cycle. cuda/kernels/CsrSpmvKernel.cu (cfdcuda's only translation
  # unit) calls into cfd::algebra::SparseMatrix/Vector -- symbols cfdcore
  # defines -- so cfdcuda depends on cfdcore just as much as cfdcore
  # depends on cfdcuda for cfd::gpu::cudaAvailable()/csrSpmvCuda(). CMake
  # supports a PUBLIC cycle between two STATIC library targets (it
  # repeats them on the final link line as needed); leaving this edge
  # undeclared, as it was before this task, is silently fine only for a
  # consumer that never causes the linker to pull a cfdcuda object file
  # in isolation. Any consumer that does -- e.g. CFDCoreTests calling
  # only cfd::gpu::cudaAvailable(), never csrSpmvCuda() -- extracts all
  # of CsrSpmvKernel.cu.o's other undefined references (SparseMatrix::
  # rows(), Vector::size(), ...) at once, and a single-pass linker that
  # already finished processing cfdcore.a earlier on the link line can
  # never resolve them: an "undefined reference" link failure with every
  # symbol actually defined right there in cfdcore.a. Declared here
  # (inside src/CMakeLists.txt, not cuda/CMakeLists.txt) because
  # cuda/ is add_subdirectory()'d before src/ in the root
  # CMakeLists.txt -- cfdapp::core does not exist yet at cuda/
  # CMakeLists.txt's own scope, but both targets already exist by this
  # point.
  target_link_libraries(cfdcuda PUBLIC cfdapp::core)
endif()
