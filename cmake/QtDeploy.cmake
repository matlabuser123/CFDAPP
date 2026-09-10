# P5-J section 47: bundle a Windows Qt target's own Qt runtime/QML modules/
# platform plugin into the build tree via Qt's own `windeployqt`, run as a
# POST_BUILD step on the target itself (into its own build-tree output
# directory) rather than as a separate CPack-time script, so `cmake --build`
# alone already produces a directory that runs standalone, and packaging
# just archives it.
#
# Verified working on a real Windows build (MSVC 19.51/VS 2026, Qt
# 6.9.3 win64_msvc2022_64, see docs/developer_guide/packaging.md's own
# release-evidence log) -- `windeployqt` only exists on Windows Qt
# installs; on any other platform (this project's own Linux/WSL
# development environment included) this file is a documented no-op.
#
# Also deploys CFDGuiControllerTests (not just cfdapp_gui): a real bug
# (found via a Windows "Entry Point Not Found" crash on
# QSignalSpy::wait(chrono::duration<...>)) showed that a Qt-linked test
# binary with NO windeployqt step of its own has no local copy of
# Qt6Test.dll, so Windows' DLL search order falls through to PATH -- on
# a machine with an unrelated/older Qt install reachable via PATH (e.g.
# a Miniconda environment's own bundled Qt, or a leftover older Qt SDK),
# that stale Qt6Test.dll gets loaded instead and is missing symbols the
# test binary actually needs. Deploying every Qt-linked target's own
# runtime locally (exe-local search order always wins over PATH) is the
# fix, not just cfdapp_gui.

if(NOT WIN32)
  message(STATUS "QtDeploy.cmake: not on Windows -- windeployqt step skipped "
                 "(this is expected on the Linux/WSL development environment; "
                 "see docs/developer_guide/packaging.md).")
  return()
endif()

get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

if(NOT WINDEPLOYQT_EXECUTABLE)
  message(WARNING "QtDeploy.cmake: windeployqt not found next to qmake -- "
                  "Windows Qt targets will not carry their own Qt runtime. "
                  "Install a full Qt6 Windows SDK (not just the libraries) "
                  "to fix this.")
  return()
endif()

# cfdapp_deploy_qt_runtime(<target> [EXTRA_WINDEPLOYQT_ARGS...])
#
# Runs windeployqt on <target>'s own output DLL/exe as a POST_BUILD step,
# from the SAME directory scope that created <target> (a real,
# longstanding CMake constraint: add_custom_command(TARGET ...) fails
# with "TARGET '...' was not created in this directory" otherwise).
function(cfdapp_deploy_qt_runtime target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" ${ARGN} "$<TARGET_FILE:${target}>"
    COMMENT "Running windeployqt on ${target} (bundles its own Qt runtime/platform plugin)"
  )
endfunction()
