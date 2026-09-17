# P1 -- Case System integration test: load -> validate -> build ->
# solve -> assert convergence, against both a fast fixture and (DISABLED_)
# the real production case.
add_executable(CFDCaseIntegrationTests
  test_case_lid_driven_cavity.cpp
  test_species_production_case.cpp
  test_multiphase_production_case.cpp
  test_compressible_production_case.cpp
  test_compressible_coupled_production_case.cpp
  test_heated_species_diffusion_production_case.cpp
  test_structured_quad_production_case.cpp
  test_graded_mesh_production_case.cpp
  test_multiblock_production_case.cpp
  # P12-MESH-004: mesh quality & validation campaign (production path).
  test_mesh_quality_campaign.cpp
  MeshQualityCampaign.cpp
  # P12-MESH-006: the 3D solver through the production path (G5, G6, G7, G9.3).
  test_3d_production_cases.cpp
  ${CFDApp_SOURCE_DIR}/tests/support/CaseFixtureCopy.cpp
)

target_link_libraries(CFDCaseIntegrationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
    # Test-only (P12-MESH-001): reads exported metadata and patches a
    # physics.json key CaseWriter does not write.
    nlohmann_json::nlohmann_json
)

# P7-TEST-001: CaseFixtureCopy.hpp -- see its own header comment.
target_include_directories(CFDCaseIntegrationTests PRIVATE "${CFDApp_SOURCE_DIR}/tests/support")

gtest_discover_tests(CFDCaseIntegrationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
