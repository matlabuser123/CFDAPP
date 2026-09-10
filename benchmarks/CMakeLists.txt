# P4 -- Performance: standalone, explicitly-invoked benchmark tooling --
# deliberately NOT part of the `tests/` tree or the routine CTest suite
# (TODO.md P4 section 27: "Do not put fragile wall-clock limits into
# routine unit tests"). Each benchmark executable reuses the existing,
# unmodified production solver code directly.
add_subdirectory(cpu)

if(CFDAPP_ENABLE_CUDA)
  add_subdirectory(gpu)
endif()
