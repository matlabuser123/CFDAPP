add_executable(CFDIoTests
  test_case_reader.cpp
  test_case_writer.cpp
  test_case_validation.cpp
  # P13-002: the case rejection carried as data, and the message left unchanged.
  test_case_diagnostic.cpp
  test_case_builder.cpp
  test_thermal_case.cpp
  # P15: the temperature-dependent property models, reachable from a case file.
  test_thermal_property_case.cpp
  # G8 closure step 2 -- physics.json's "reacting" block reaches the live P18 chemistry
  # objects. Before this there were 0 case keys for a mechanism.
  test_reacting_case.cpp
  test_turbulence_case.cpp
  test_boussinesq_case.cpp
  test_species_case.cpp
  test_multiphase_case.cpp
  test_compressible_case.cpp
  test_physics_compatibility.cpp
  test_csv_writer.cpp
  test_json_writer.cpp
  test_vtk_writer.cpp
  test_robustness_config.cpp
  test_structured_quad_case.cpp
  test_mesh_quality_case.cpp
  test_graded_mesh_case.cpp
  test_multiblock_case.cpp
  test_vtk3d.cpp
  test_case3d.cpp
)

target_link_libraries(CFDIoTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only: reads JSONWriter's output back to validate it, the same
    # way test_json_writer.cpp's own header comment explains -- never a
    # dependency of any public cfd/io header.
    nlohmann_json::nlohmann_json
)

target_include_directories(CFDIoTests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(CFDIoTests PROPERTIES LABELS "unit")
