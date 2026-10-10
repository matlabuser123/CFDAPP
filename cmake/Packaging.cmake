# P5-J -- Packaging. Included only when CFDAPP_ENABLE_PACKAGING=ON
# (root CMakeLists.txt), and only meaningful alongside
# CFDAPP_BUILD_GUI=ON (a CLI-only package would just be the plain
# `cfdapp` executable, which needs none of this). See
# docs/developer_guide/packaging.md for this configuration's own
# disclosed, unverified-on-Windows status -- read that before treating
# anything CPack produces here as a released artifact.

# P13-006: a CLI-only package is a real configuration, not a mistake.
#
# This used to warn and `return()`, so `-DCFDAPP_BUILD_GUI=OFF
# -DCFDAPP_ENABLE_PACKAGING=ON` produced no CPackConfig.cmake at all
# (measured -- results/p13/packaging/audit.md F4). The headless case -- a
# cluster, a container, a CI image, anywhere without Qt -- was the one that
# could not be packaged. The old comment's reasoning ("a CLI-only package
# would just be the plain `cfdapp` executable, which needs none of this") is
# not what the rules below say: examples, user documentation and the licence
# are installed independently of the GUI, and a CLI user wants all three.
#
# What differs between the two packages is only the GUI executable and the Qt
# runtime that must travel with it.
set(CFDAPP_PACKAGE_INCLUDES_GUI ${CFDAPP_BUILD_GUI})

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
# P13-006: the name and the generator list are decided by one pure function,
# from the system actually being built for -- see cmake/PackageNaming.cmake
# for what this replaces and why it is a function (short version: the Windows
# name is a contract release.yml depends on, and a function taking the system
# as an argument can be tested for Windows from a Linux host).
include(${CFDApp_SOURCE_DIR}/cmake/PackageNaming.cmake)
cfdapp_package_naming(
  SYSTEM  "${CMAKE_SYSTEM_NAME}"
  ARCH    "${CMAKE_SYSTEM_PROCESSOR}"
  VERSION "${CFDApp_VERSION}"
  SUFFIX  "${CFDAPP_VERSION_SUFFIX}"
  OUT_NAME       CPACK_PACKAGE_FILE_NAME
  OUT_GENERATORS CPACK_GENERATOR
)
message(STATUS "CFDApp package: ${CPACK_PACKAGE_FILE_NAME} (${CPACK_GENERATOR})")

# The version CPack advertises must be the version the binary reports.
# CPACK_PACKAGE_VERSION_{MAJOR,MINOR,PATCH} above give CPack "0.2.0" and drop
# CFDAPP_VERSION_SUFFIX, which cfd::core::versionString() includes -- so a
# development build was named exactly like the released 0.2.0 while the
# `cfdapp --version` inside it said 0.2.0-dev (measured -- audit.md F2).
# A tagged release configures with -DCFDAPP_VERSION_SUFFIX="" (release.yml),
# so a release package's version is unchanged by this.
if(CFDAPP_VERSION_SUFFIX)
  set(CPACK_PACKAGE_VERSION "${CFDApp_VERSION}-${CFDAPP_VERSION_SUFFIX}")
else()
  set(CPACK_PACKAGE_VERSION "${CFDApp_VERSION}")
endif()

# NSIS settings are inert wherever NSIS is not in CPACK_GENERATOR.
set(CPACK_NSIS_PACKAGE_NAME "CFDApp")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH ON)

# TODO.md P5 section 45's own layout: the two executables, the CPU-only
# path always works (section 48 -- CUDA, when this build has it, is
# always an optional runtime capability, never a hard package
# dependency), example cases, user docs, and licenses -- never the
# build directory's own object files/CMakeCache/intermediate artifacts
# (section 45's "do not package source/build garbage").
if(CFDAPP_PACKAGE_INCLUDES_GUI)
  install(TARGETS cfdapp cfdapp_gui
    RUNTIME DESTINATION bin
  )
else()
  install(TARGETS cfdapp
    RUNTIME DESTINATION bin
  )
endif()

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
#
# P13-006: guarded, because a CLI-only package has no cfdapp_gui target for
# $<TARGET_FILE_DIR:> to name, and because there is no Qt runtime to carry
# when there is no GUI. The exclusion patterns below are unchanged -- the
# install tree they produce was measured clean before this item and must stay
# that way (criterion P1).
if(CFDAPP_PACKAGE_INCLUDES_GUI)
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
endif()
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
