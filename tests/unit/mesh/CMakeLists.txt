add_executable(CFDMeshTests
  test_cell.cpp
  test_face.cpp
  test_boundary_patch.cpp
  test_mesh.cpp
  test_mesh_geometry.cpp
  test_mesh_quality.cpp
  test_cartesian_mesh.cpp
  test_mesh_fingerprint.cpp
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
)

gtest_discover_tests(CFDMeshTests PROPERTIES LABELS "unit")
