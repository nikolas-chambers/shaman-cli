cmake_minimum_required(VERSION 3.24)
project(shaman-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SHAMAN_BUILD_TESTS "Build unit tests" ON)
option(SHAMAN_STATIC "Link statically where possible" OFF)

# --- dependencies ------------------------------------------------------------
# libcurl: HTTP + TLS. nlohmann_json: header-only JSON (fetched if not installed).
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)  # SHA-256 and randomness for OAuth PKCE
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

# --- library -----------------------------------------------------------------
file(GLOB_RECURSE SHAMAN_SOURCES CONFIGURE_DEPENDS src/shaman/*.cpp)
if(WIN32)
  list(FILTER SHAMAN_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
else()
  list(FILTER SHAMAN_SOURCES EXCLUDE REGEX "_win\\.cpp$")
endif()

add_library(shaman_core STATIC ${SHAMAN_SOURCES})
target_include_directories(shaman_core PUBLIC src)
target_link_libraries(shaman_core PUBLIC CURL::libcurl OpenSSL::Crypto nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(shaman_core PUBLIC ws2_32)
endif()
if(MSVC)
  target_compile_options(shaman_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(shaman_core PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# --- executable --------------------------------------------------------------
add_executable(shaman apps/main.cpp)
target_link_libraries(shaman PRIVATE shaman_core)
if(SHAMAN_STATIC AND NOT APPLE)
  target_link_options(shaman PRIVATE -static-libstdc++ -static-libgcc)
endif()
install(TARGETS shaman RUNTIME DESTINATION bin)

# --- tests -------------------------------------------------------------------
if(SHAMAN_BUILD_TESTS)
  enable_testing()
  file(GLOB SHAMAN_TESTS CONFIGURE_DEPENDS tests/unit/*.cpp)
  add_executable(shaman_tests ${SHAMAN_TESTS})
  target_link_libraries(shaman_tests PRIVATE shaman_core)
  add_test(NAME unit COMMAND shaman_tests)

  # End-to-end: the real binary against tests/mock/mock_zen.py (POSIX + python3).
  find_program(PYTHON3 python3)
  find_program(BASH bash)
  if(PYTHON3 AND BASH AND NOT WIN32)
    add_test(NAME e2e COMMAND ${BASH} ${CMAKE_SOURCE_DIR}/tests/e2e/run.sh $<TARGET_FILE:shaman>)
  endif()
endif()
