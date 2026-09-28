cmake_minimum_required(VERSION 3.24)
project(shaman-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Only used by the `site` target below, so it is found unconditionally.
find_program(PYTHON3 python3)

option(SHAMAN_BUILD_TESTS "Build unit tests" ON)
option(SHAMAN_STATIC "Link statically where possible" OFF)
option(SHAMAN_BUILD_DESKTOP "Build shaman-desktop (needs WebView2 / WKWebView / WebKitGTK)" OFF)

# --- dependencies ------------------------------------------------------------
# libcurl: HTTP + TLS. nlohmann_json: header-only JSON (fetched if not installed).
find_package(CURL REQUIRED)
if(NOT WIN32)
  find_package(OpenSSL REQUIRED)  # SHA-256 and randomness for OAuth PKCE (Windows uses BCrypt)
endif()
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

# The web UI is a single HTML file compiled into the binary.
set(SHAMAN_WEB_CPP ${CMAKE_BINARY_DIR}/generated/web_index.cpp)
add_custom_command(
  OUTPUT ${SHAMAN_WEB_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/web/index.html -DOUT=${SHAMAN_WEB_CPP} -DNAME=web_index
          -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/web/index.html ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Embedding web UI")

# Built-in skills (skills/*/SKILL.md) are compiled in as well.
file(GLOB_RECURSE SHAMAN_SKILL_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/skills/*)
set(SHAMAN_SKILLS_CPP ${CMAKE_BINARY_DIR}/generated/skills.cpp)
add_custom_command(
  OUTPUT ${SHAMAN_SKILLS_CPP}
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/skills -DOUT=${SHAMAN_SKILLS_CPP} -P ${CMAKE_SOURCE_DIR}/cmake/embed_skills.cmake
  DEPENDS ${SHAMAN_SKILL_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_skills.cmake
  COMMENT "Embedding built-in skills")

add_library(shaman_core STATIC ${SHAMAN_SOURCES} ${SHAMAN_WEB_CPP} ${SHAMAN_SKILLS_CPP}
            third_party/qrcodegen/qrcodegen.cpp)  # MIT, Project Nayuki: QR codes for `serve --pair`
target_include_directories(shaman_core PUBLIC src PRIVATE third_party/qrcodegen)
target_link_libraries(shaman_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)
if(NOT WIN32)
  target_link_libraries(shaman_core PUBLIC OpenSSL::Crypto)
endif()
if(WIN32)
  target_link_libraries(shaman_core PUBLIC ws2_32 bcrypt advapi32)
  target_compile_definitions(shaman_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE)
  target_link_libraries(shaman_core PUBLIC "-framework Security" "-framework CoreFoundation")  # Keychain
endif()
if(MSVC)
  target_compile_options(shaman_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(shaman_core PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# --- executable --------------------------------------------------------------
set(SHAMAN_RESOURCES "")
if(WIN32)
  set(SHAMAN_RESOURCES apps/shaman.rc)  # app icon
endif()
add_executable(shaman apps/main.cpp ${SHAMAN_RESOURCES})
target_link_libraries(shaman PRIVATE shaman_core)
if(SHAMAN_STATIC AND NOT APPLE)
  target_link_options(shaman PRIVATE -static-libstdc++ -static-libgcc)
endif()
install(TARGETS shaman RUNTIME DESTINATION bin)
# Portable dev builds: a shaman.ini in the source tree (git-ignored) is copied next to the binary.
if(EXISTS ${CMAKE_SOURCE_DIR}/shaman.ini)
  add_custom_command(TARGET shaman POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/shaman.ini $<TARGET_FILE_DIR:shaman>/shaman.ini)
endif()

# --- desktop app -------------------------------------------------------------
if(SHAMAN_BUILD_DESKTOP)
  include(FetchContent)
  FetchContent_Declare(webview GIT_REPOSITORY https://github.com/webview/webview GIT_TAG cbbdee44afff22867de9fd88a9fc8350d9bdd399)
  set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(webview)
  add_executable(shaman-desktop WIN32 apps/desktop.cpp ${SHAMAN_RESOURCES})
  target_link_libraries(shaman-desktop PRIVATE shaman_core webview::core)
  if(APPLE)  # a proper Shaman.app bundle with the icon
    set(SHAMAN_ICNS ${CMAKE_BINARY_DIR}/Shaman.icns)
    find_program(ICONUTIL iconutil)
    find_program(SIPS sips)
    if(ICONUTIL AND SIPS AND NOT EXISTS ${SHAMAN_ICNS})
      set(iconset ${CMAKE_BINARY_DIR}/Shaman.iconset)
      file(MAKE_DIRECTORY ${iconset})
      foreach(size 16 32 128 256 512)
        math(EXPR size2 "${size} * 2")
        execute_process(COMMAND ${SIPS} -z ${size} ${size} ${CMAKE_SOURCE_DIR}/docs/assets/icon-512.png --out ${iconset}/icon_${size}x${size}.png OUTPUT_QUIET)
        execute_process(COMMAND ${SIPS} -z ${size2} ${size2} ${CMAKE_SOURCE_DIR}/docs/assets/icon-512.png --out ${iconset}/icon_${size}x${size}@2x.png OUTPUT_QUIET)
      endforeach()
      execute_process(COMMAND ${ICONUTIL} -c icns ${iconset} -o ${SHAMAN_ICNS})
    endif()
    set_target_properties(shaman-desktop PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME Shaman
      MACOSX_BUNDLE_BUNDLE_NAME Shaman MACOSX_BUNDLE_GUI_IDENTIFIER io.github.nikolas-chambers.shaman
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION} MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    if(EXISTS ${SHAMAN_ICNS})
      target_sources(shaman-desktop PRIVATE ${SHAMAN_ICNS})
      set_source_files_properties(${SHAMAN_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
      set_target_properties(shaman-desktop PROPERTIES MACOSX_BUNDLE_ICON_FILE Shaman.icns)
    endif()
  endif()
  if(NOT MSVC)
    target_compile_options(shaman-desktop PRIVATE -Wno-deprecated-declarations)  # webview headers use std::aligned_storage
  endif()
  install(TARGETS shaman-desktop RUNTIME DESTINATION bin BUNDLE DESTINATION /Applications)
endif()

# --- tests -------------------------------------------------------------------
if(SHAMAN_BUILD_TESTS)
  enable_testing()
  file(GLOB SHAMAN_TESTS CONFIGURE_DEPENDS tests/unit/*.cpp)
  add_executable(shaman_tests ${SHAMAN_TESTS})
  target_link_libraries(shaman_tests PRIVATE shaman_core)
  add_test(NAME unit COMMAND shaman_tests)

  # End-to-end: the real binary against tests/mock/mock_zen.py (POSIX + python3).
  find_program(BASH bash)
  if(PYTHON3 AND BASH AND NOT WIN32)
    add_test(NAME e2e COMMAND ${BASH} ${CMAKE_SOURCE_DIR}/tests/e2e/run.sh $<TARGET_FILE:shaman>)
  endif()
endif()

# --- static site -------------------------------------------------------------
# Dependency-free markdown/HTML generator for the GitHub Pages site.
# Output is committed so the zip ships a ready-to-publish site/ tree.
if(PYTHON3)
  add_custom_target(site
    COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tools/site.py
            --src ${CMAKE_SOURCE_DIR} --out ${CMAKE_SOURCE_DIR}/site
    COMMENT "Generating the static site into ${CMAKE_SOURCE_DIR}/site"
    VERBATIM)
endif()
