add_executable(cfdapp main.cpp)

target_link_libraries(cfdapp
  PRIVATE
    cfdapp::core
    cfdapp::compiler_warnings
)
