# cfdcore: the library target for the current build-system phase.
#
# src/core (P0 -- Core), src/mesh (P0 -- Mesh), src/fields (P0 -- Fields),
# src/algebra (P0 -- Linear Algebra), src/boundary (P0 -- Boundary
# Conditions), src/discretization (P0 -- Finite Volume Operators), and
# src/physics (P0 -- Incompressible Physics: FluidProperties, MassFlux,
# ContinuityEquation, MomentumEquation -- EnergyEquation/
# TransportProperties are deferred to the thermal phase) are wired up
# here, along with src/pressure_velocity (P0 -- SIMPLE) and src/io (P1 --
# Case System / Result Export). src/solver (P2 -- Transient CFD) has only
# TimeController.cpp so far (TASK P2-001) -- the rest of that phase's
# subdirectories are still pre-scaffolded per PROJECT_STRUCTURE.md but
# intentionally not added as sources yet -- most are still empty
# placeholders and compiling them now would create linker noise for no
# benefit.

configure_file(
  "${CMAKE_SOURCE_DIR}/include/cfd/core/Version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/include/cfd/core/Version.hpp"
  @ONLY
)

add_library(cfdcore STATIC
  core/Version.cpp
  core/Logger.cpp
  core/Timer.cpp
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
)

target_sources(cfdcore
  PRIVATE
    physics/FluidProperties.cpp
    physics/MassFlux.cpp
    physics/ContinuityEquation.cpp
    physics/MomentumEquation.cpp
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
