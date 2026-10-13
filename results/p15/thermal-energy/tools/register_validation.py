import io
p = 'tests/integration/thermal/CMakeLists.txt'
s = io.open(p, encoding='utf-8', newline='').read()
if 'CFDThermalValidationTests' in s:
    print('already registered')
else:
    s = s.rstrip('\n') + '''

# P15 -- the thermal validation suite: analytical cases A-G plus the discrete
# energy-conservation gate. Its own executable, matching every other file in
# this directory.
add_executable(CFDThermalValidationTests
  test_thermal_validation.cpp
)

target_link_libraries(CFDThermalValidationTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    GTest::gtest_main
)

gtest_discover_tests(CFDThermalValidationTests
  WORKING_DIRECTORY "${CFDApp_SOURCE_DIR}"
  PROPERTIES LABELS "validation"
)
'''
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('validation suite registered')
