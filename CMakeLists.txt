cmake_minimum_required(VERSION 3.20)
project(podcast-sanity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

option(SANITY_WITH_GUI "Build the Qt6 desktop application" ON)
option(SANITY_WITH_EBUR128 "Cross-check loudness against libebur128 in tests" ON)
option(SANITY_SANITIZE "Build the core with ASan/UBSan" OFF)

set(SANITY_CORE_SOURCES
  src/json.cpp
  src/io_wav.cpp
  src/dsp.cpp
  src/loudness.cpp
  src/clean.cpp
  src/dynamics.cpp
  src/filler.cpp
  src/analyze.cpp
  src/pipeline.cpp
  src/corpus.cpp
  src/metrics.cpp
)

add_library(sanity_core STATIC ${SANITY_CORE_SOURCES})
target_include_directories(sanity_core PUBLIC include)
if(SANITY_SANITIZE)
  target_compile_options(sanity_core PUBLIC -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sanity_core PUBLIC -fsanitize=address,undefined)
endif()

# ---- codec adapters (optional, detected) ----------------------------------
find_package(PkgConfig QUIET)
set(SANITY_HAVE_MP3 FALSE)
set(SANITY_HAVE_SNDFILE FALSE)
if(PkgConfig_FOUND)
  pkg_check_modules(MP3LAME QUIET lame)
  pkg_check_modules(SNDFILE QUIET sndfile)
  if(MP3LAME_FOUND)
    set(SANITY_HAVE_MP3 TRUE)
  endif()
  if(SNDFILE_FOUND)
    set(SANITY_HAVE_SNDFILE TRUE)
  endif()
endif()

# ---- CLI ------------------------------------------------------------------
add_executable(podcast-sanity-render cli/render_main.cpp)
target_link_libraries(podcast-sanity-render PRIVATE sanity_core)
target_compile_definitions(podcast-sanity-render PRIVATE
  SANITY_VERSION="${PROJECT_VERSION}"
  SANITY_HAVE_MP3=$<BOOL:${SANITY_HAVE_MP3}>)

# ---- tests ----------------------------------------------------------------
enable_testing()

add_executable(sanity_tests tests/test_main.cpp)
target_link_libraries(sanity_tests PRIVATE sanity_core)
target_include_directories(sanity_tests PRIVATE tests)

if(SANITY_WITH_EBUR128)
  find_library(EBUR128_LIBRARY NAMES ebur128)
  find_path(EBUR128_INCLUDE_DIR NAMES ebur128.h)
  if(EBUR128_LIBRARY AND EBUR128_INCLUDE_DIR)
    target_link_libraries(sanity_tests PRIVATE ${EBUR128_LIBRARY})
    target_include_directories(sanity_tests PRIVATE ${EBUR128_INCLUDE_DIR})
    target_compile_definitions(sanity_tests PRIVATE SANITY_HAVE_EBUR128=1)
    message(STATUS "libebur128 found: loudness cross-check enabled (${EBUR128_LIBRARY})")
  else()
    message(STATUS "libebur128 not found: loudness cross-check disabled")
  endif()
endif()

add_test(NAME unit COMMAND sanity_tests)

# ---- end-to-end harness ----------------------------------------------------
# Generates synthetic episodes with ground truth, runs the whole chain and
# judges the result. Slower than the unit tests, so it is a separate target.
add_executable(sanity_e2e_tests tests/e2e_main.cpp)
target_link_libraries(sanity_e2e_tests PRIVATE sanity_core)
target_include_directories(sanity_e2e_tests PRIVATE tests)
target_compile_definitions(sanity_e2e_tests PRIVATE SANITY_VERSION="${PROJECT_VERSION}")
add_test(NAME e2e COMMAND sanity_e2e_tests)
set_tests_properties(e2e PROPERTIES TIMEOUT 900)

# ---- GUI ------------------------------------------------------------------
if(SANITY_WITH_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(podcast-sanity
      gui/main.cpp
      gui/mainwindow.cpp
      gui/pages/record_page.cpp
      gui/pages/master_page.cpp
      gui/pages/standards_page.cpp
      gui/pages/library_page.cpp
      gui/pages/export_page.cpp
      gui/pages/settings_page.cpp
      gui/pages/diagnostics_page.cpp
      gui/widgets.cpp
      gui/theme.cpp
    )
    target_link_libraries(podcast-sanity PRIVATE sanity_core Qt6::Widgets)
    target_include_directories(podcast-sanity PRIVATE gui)
    target_compile_definitions(podcast-sanity PRIVATE SANITY_VERSION="${PROJECT_VERSION}")
    message(STATUS "Qt6 found: building podcast-sanity GUI")
  else()
    message(STATUS "Qt6 not found: GUI skipped")
  endif()
endif()

# ---- packaging -----------------------------------------------------------
set(CPACK_PACKAGE_NAME "podcast-sanity")
set(CPACK_PACKAGE_VENDOR "Voloire")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local podcast recording and automatic mastering")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Voloire")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6 | libgtk-3-0")
set(CPACK_WIX_UPGRADE_GUID "9F2C6D4E-7B31-4A58-9C0E-2D5A6F1B8E30")
include(CPack)
