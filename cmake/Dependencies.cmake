# Third-party dependencies for production code (as opposed to
# tests/CMakeLists.txt's own FetchContent of GoogleTest, which is
# test-only and stays there). Centralized here per PROJECT_STRUCTURE.md
# so dependency setup isn't scattered across the tree.
#
# nlohmann/json: the case-loading pipeline (P1 -- Case System) parses
# case.json/geometry.json/mesh.json/physics.json/boundaries.json/
# solver.json. A mature, header-only, single-dependency JSON library is
# used rather than a hand-rolled parser (TODO.md P1 section 19).
include(FetchContent)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
