# P5-J -- Packaging. Included only when CFDAPP_ENABLE_PACKAGING=ON
# (root CMakeLists.txt), and only meaningful alongside
# CFDAPP_BUILD_GUI=ON (a CLI-only package would just be the plain
# `cfdapp` executable, which needs none of this). See
# docs/developer_guide/packaging.md for this configuration's own
# disclosed, unverified-on-Windows status -- read that before treating
# anything CPack produces here as a released artifact.

if(NOT CFDAPP_BUILD_GUI)
  message(WARNING
    "CFDAPP_ENABLE_PACKAGING is ON but CFDAPP_BUILD_GUI is OFF -- "
    "packaging is only meaningful for the GUI build. Skipping.")
  return()
endif()

# TODO.md P5 section 49: one authoritative version source -- the same
# project(CFDApp VERSION ...) in the root CMakeLists.txt that
# cfd::core::versionString()/`cfdapp --version` already read from
# (include/cfd/core/Version.hpp.in), never a second, separately-
# maintained packaging-only version string.
set(CPACK_PACKAGE_NAME "CFDApp")
set(CPACK_PACKAGE_VERSION_MAJOR ${CFDApp_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${CFDApp_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${CFDApp_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "CFDApp project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CFDApp_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "CFDApp-${CFDApp_VERSION}-Windows-x64")

set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_NSIS_PACKAGE_NAME "CFDApp")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH ON)

# TODO.md P5 section 45's own layout: the two executables, the CPU-only
# path always works (section 48 -- CUDA, when this build has it, is
# always an optional runtime capability, never a hard package
# dependency), example cases, user docs, and licenses -- never the
# build directory's own object files/CMakeCache/intermediate artifacts
# (section 45's "do not package source/build garbage").
install(TARGETS cfdapp cfdapp_gui
  RUNTIME DESTINATION bin
)

# A real Windows package build exposed a genuine gap here: windeployqt
# (apps/gui/CMakeLists.txt's own POST_BUILD step) drops Qt's runtime
# DLLs, the platform/QML/image-format/TLS plugins, and translations
# into cfdapp_gui's own build-tree output directory -- but a plain
# `install(TARGETS ...)` only ever packages the *executable itself*,
# never files a separate custom command placed next to it. Without this
# rule the produced ZIP/installer contained only the two .exe files and
# would fail to launch on any machine without a matching Qt install
# already on PATH (section 46's own "do not assume DLLs on the
# developer machine will exist for the end user" -- confirmed by
# extracting and inspecting the first package build before this rule
# existed). Excludes this same build directory's own intermediate/
# build-only artifacts (CMakeFiles/, *_autogen/, generated .cpp/.cmake,
# and the test binary CFDGuiControllerTests.exe, which happens to link
# into this same output directory but is never part of a release).
install(DIRECTORY "$<TARGET_FILE_DIR:cfdapp_gui>/"
  DESTINATION bin
  PATTERN "CMakeFiles" EXCLUDE
  PATTERN "*_autogen" EXCLUDE
  PATTERN "*.cmake" EXCLUDE
  PATTERN "*.cpp" EXCLUDE
  PATTERN "*.depends" EXCLUDE
  PATTERN "cfdapp_gui.exe" EXCLUDE
  PATTERN "cfdapp.exe" EXCLUDE
  PATTERN "CFDGuiControllerTests*" EXCLUDE
)
install(DIRECTORY ${CFDApp_SOURCE_DIR}/cases/
  DESTINATION examples
  PATTERN "results" EXCLUDE  # section 45: no leftover run output in the package.
)
install(DIRECTORY ${CFDApp_SOURCE_DIR}/docs/user_guide/
  DESTINATION docs
)
if(EXISTS ${CFDApp_SOURCE_DIR}/LICENSE)
  install(FILES ${CFDApp_SOURCE_DIR}/LICENSE DESTINATION licenses)
endif()

# Qt's own runtime/QML modules/platform plugin (section 47) is bundled
# via a POST_BUILD step on cfdapp_gui, included from apps/gui/
# CMakeLists.txt itself -- not from here (a real Windows build exposed
# why: add_custom_command(TARGET cfdapp_gui ...) must run in the same
# directory scope that created that target, which this file is not --
# see apps/gui/CMakeLists.txt's own comment on this).

include(CPack)
