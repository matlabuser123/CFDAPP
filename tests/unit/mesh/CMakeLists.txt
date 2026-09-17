add_executable(CFDMeshTests
  test_cell.cpp
  test_face.cpp
  test_boundary_patch.cpp
  test_mesh.cpp
  test_mesh_geometry.cpp
  test_mesh_quality.cpp
  test_mesh_quality_report.cpp
  test_cartesian_mesh.cpp
  test_mesh_fingerprint.cpp
  test_structured_quad_mesh.cpp
  test_mesh_grading.cpp
  test_graded_mesh.cpp
  test_multiblock_mesh.cpp
  test_cartesian3d_mesh.cpp
  # P12-MESH-007: mesh motion (geometry update, mesh velocity, swept volumes, GCL).
  test_mesh_motion.cpp
)

target_link_libraries(CFDMeshTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

# P12-NUM-003: reuses tests/unit/discretization/DistortedMesh.hpp (the
# P12-NUM-002 distorted-quad-mesh generator) rather than duplicating it --
# that header lives one directory over, so it is not found by the
# compiler's default same-directory quoted-include search.
target_include_directories(CFDMeshTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../discretization
    # P12-MESH-007: the acceptance-gate configurations shared by the
    # MESH-007 test files (tests/support/MeshMotionCases.hpp).
    ${CMAKE_CURRENT_SOURCE_DIR}/../../support
)

gtest_discover_tests(CFDMeshTests PROPERTIES LABELS "unit")
