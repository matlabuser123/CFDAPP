# P5-J section 47: bundle cfdapp_gui's own Qt runtime/QML modules/
# platform plugin into the install tree via Qt's own `windeployqt`
# (Windows) -- run as a POST_BUILD step on the cfdapp_gui target itself
# (into its own build-tree output directory) rather than as a separate
# CPack-time script, so `cmake --build` alone already produces a
# directory that runs standalone, and packaging just archives it.
#
# Verified working on a real Windows build (MSVC 19.51/VS 2026, Qt
# 6.9.3 win64_msvc2022_64, see docs/developer_guide/packaging.md's own
# release-evidence log) -- `windeployqt` only exists on Windows Qt
# installs; on any other platform (this project's own Linux/WSL
# development environment included) this file is a documented no-op.

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
                  "the packaged cfdapp_gui.exe will not carry its own Qt "
                  "runtime. Install a full Qt6 Windows SDK (not just the "
                  "libraries) to fix this.")
  return()
endif()

add_custom_command(TARGET cfdapp_gui POST_BUILD
  COMMAND "${WINDEPLOYQT_EXECUTABLE}" --qmldir "${CFDApp_SOURCE_DIR}/apps/gui/qml"
          "$<TARGET_FILE:cfdapp_gui>"
  COMMENT "Running windeployqt on cfdapp_gui (bundles Qt runtime/QML/platform plugin)"
)
