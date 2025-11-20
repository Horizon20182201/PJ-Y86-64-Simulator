cmake_minimum_required(VERSION 3.16)
project(y86sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(Y86_USE_FETCHCONTENT "Fetch nlohmann/json with FetchContent" OFF)
option(BUILD_TUI "Build ncurses TUI frontend" OFF)
option(BUILD_HTTP "Build minimal HTTP frontend" OFF)

# ---- third_party / nlohmann_json ----
if (Y86_USE_FETCHCONTENT)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(nlohmann_json)
else()
  add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
  target_include_directories(nlohmann_json::nlohmann_json
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

# ---- y86core library ----
add_library(y86core
  src/cpu.cpp
  src/loader.cpp
  src/stepper.cpp
)
target_include_directories(y86core PUBLIC include third_party)
target_link_libraries(y86core PUBLIC nlohmann_json::nlohmann_json)

# ---- CLI app (stdin -> JSON on stdout) ----
add_subdirectory(apps/y86sim)

# ---- optional TUI ----
if (BUILD_TUI)
  find_package(Curses REQUIRED)
  add_executable(y86tui frontends/tui-ncurses/main.cpp)
  target_link_libraries(y86tui PRIVATE y86core ${CURSES_LIBRARIES})
  target_include_directories(y86tui PRIVATE ${CURSES_INCLUDE_DIRS})
endif()

# ---- optional HTTP ----
if (BUILD_HTTP)
  add_executable(y86http frontends/http-server/main.cpp)
  target_link_libraries(y86http PRIVATE y86core)
  target_include_directories(y86http PRIVATE third_party)
endif()
