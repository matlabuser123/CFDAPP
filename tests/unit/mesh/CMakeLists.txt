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

gtest_discover_tests(CFDMeshTests PROPERTIES LABELS "unit")
