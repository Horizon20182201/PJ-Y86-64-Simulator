cmake_minimum_required(VERSION 3.16)
project(y86sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TUI "Build ncurses TUI frontend" OFF)

add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
target_include_directories(nlohmann_json::nlohmann_json
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# y86core library
add_library(y86core
  src/cpu.cpp
  src/worker.cpp
)
target_include_directories(y86core PUBLIC include third_party)
target_link_libraries(y86core PUBLIC nlohmann_json::nlohmann_json)

# y86sim
add_executable(y86sim apps/y86sim/main.cpp)
target_link_libraries(y86sim PRIVATE y86core)

# tui_ftxui
if (BUILD_TUI)
  include(FetchContent)
  # 可固定到某个稳定 tag（例如 v5.0.0）；不写 GIT_TAG 也是可以的。
  FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
  )
  FetchContent_MakeAvailable(ftxui)

  add_executable(y86_tui apps/tui_ftxui/main.cpp)
  target_link_libraries(y86_tui
    PRIVATE y86core
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
  )
  target_compile_features(y86_tui PRIVATE cxx_std_17)
endif()
