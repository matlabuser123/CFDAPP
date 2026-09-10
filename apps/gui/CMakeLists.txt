# P5-B -- GUI Solver Workflow. Only reached when CFDAPP_BUILD_GUI=ON
# (root CMakeLists.txt) -- the default CPU-only/CLI build is completely
# unaffected either way (section 67: "P5 must not make the GUI
# mandatory").
find_package(Qt6 COMPONENTS Core Gui Qml Quick QuickControls2 Test QUIET)

if(NOT Qt6_FOUND)
  message(WARNING
    "CFDAPP_BUILD_GUI is ON, but Qt6 (Core/Gui/Qml/Quick/QuickControls2) "
    "was not found -- apps/gui (cfdapp_gui) will not be built. The CLI "
    "(cfdapp) and every library/test target are unaffected.")
  return()
endif()

set(CMAKE_AUTOMOC ON)

qt6_add_resources(CFDAPP_GUI_QML_RESOURCES qml.qrc)

add_executable(cfdapp_gui
  main.cpp
  SimulationController.hpp
  SimulationController.cpp
  ${CFDAPP_GUI_QML_RESOURCES}
)

target_link_libraries(cfdapp_gui
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# P5-J section 47: windeployqt (Windows only, no-ops elsewhere -- see
# QtDeploy.cmake's own header comment). Deliberately included *here*,
# not from cmake/Packaging.cmake -- add_custom_command(TARGET
# cfdapp_gui ...) must be called from the same directory scope that
# created the cfdapp_gui target (a real, longstanding CMake constraint:
# "TARGET '...' was not created in this directory"), which this file is
# and Packaging.cmake (included from the root CMakeLists.txt) is not.
# Bundling Qt's runtime next to cfdapp_gui.exe on every Windows GUI
# build (not only a CFDAPP_ENABLE_PACKAGING=ON one) is also simply more
# useful -- a Windows developer can run the build-tree executable
# standalone without CFDAPP_ENABLE_PACKAGING at all.
include(${CFDApp_SOURCE_DIR}/cmake/QtDeploy.cmake)

# P5-B section 59: controller-level tests -- plain QObject logic under a
# bare QCoreApplication, no display/offscreen platform plugin needed.
add_executable(CFDGuiControllerTests
  tests/test_simulation_controller.cpp
  SimulationController.hpp
  SimulationController.cpp
)

target_link_libraries(CFDGuiControllerTests
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
    Qt6::Core
    Qt6::Test
    GTest::gtest_main
)

include(GoogleTest)  # idempotent -- harmless even though tests/ (add_subdirectory()'d before apps/gui, root CMakeLists.txt) already includes this.
gtest_discover_tests(CFDGuiControllerTests
  PROPERTIES
    LABELS "unit"
    WORKING_DIRECTORY ${CFDApp_SOURCE_DIR}
)
