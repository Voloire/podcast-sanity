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

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

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
  src/encode.cpp
)

add_library(sanity_core STATIC ${SANITY_CORE_SOURCES})
target_include_directories(sanity_core PUBLIC include)
if(SANITY_SANITIZE)
  target_compile_options(sanity_core PUBLIC -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sanity_core PUBLIC -fsanitize=address,undefined)
endif()

# ---- codec adapters (optional, detected) ----------------------------------
# Two discovery paths, because the same two libraries are packaged very
# differently on Linux (pkg-config) and on Windows (vcpkg, no pkg-config).
find_package(PkgConfig QUIET)
set(SANITY_HAVE_MP3 FALSE)
set(SANITY_HAVE_SNDFILE FALSE)

if(PkgConfig_FOUND)
  pkg_check_modules(MP3LAME QUIET lame)
  pkg_check_modules(SNDFILE QUIET sndfile)
  if(MP3LAME_FOUND)
    set(SANITY_HAVE_MP3 TRUE)
    set(MP3LAME_LIBRARY "${MP3LAME_LIBRARIES}")
    target_link_libraries(sanity_core PUBLIC ${MP3LAME_LIBRARIES})
    target_include_directories(sanity_core PUBLIC ${MP3LAME_INCLUDE_DIRS})
  endif()
  if(SNDFILE_FOUND)
    set(SANITY_HAVE_SNDFILE TRUE)
    set(SNDFILE_LIBRARY "${SNDFILE_LIBRARIES}")
    target_link_libraries(sanity_core PUBLIC ${SNDFILE_LIBRARIES})
    target_include_directories(sanity_core PUBLIC ${SNDFILE_INCLUDE_DIRS})
  endif()
endif()

# Fallback: find_path/find_library. This is the only path on MSVC + vcpkg,
# which installs lame/lame.h and sndfile.h but no pkg-config metadata.
if(NOT SANITY_HAVE_MP3)
  find_path(MP3LAME_INCLUDE_DIR NAMES lame/lame.h)
  find_library(MP3LAME_LIBRARY NAMES mp3lame lame libmp3lame)
  if(MP3LAME_INCLUDE_DIR AND MP3LAME_LIBRARY)
    set(SANITY_HAVE_MP3 TRUE)
    target_link_libraries(sanity_core PUBLIC ${MP3LAME_LIBRARY})
    target_include_directories(sanity_core PUBLIC ${MP3LAME_INCLUDE_DIR})
  endif()
endif()
if(NOT SANITY_HAVE_SNDFILE)
  find_path(SNDFILE_INCLUDE_DIR NAMES sndfile.h)
  find_library(SNDFILE_LIBRARY NAMES sndfile libsndfile)
  if(SNDFILE_INCLUDE_DIR AND SNDFILE_LIBRARY)
    set(SANITY_HAVE_SNDFILE TRUE)
    target_link_libraries(sanity_core PUBLIC ${SNDFILE_LIBRARY})
    target_include_directories(sanity_core PUBLIC ${SNDFILE_INCLUDE_DIR})
  endif()
endif()

if(SANITY_HAVE_MP3)
  message(STATUS "MP3 export enabled (lame at ${MP3LAME_LIBRARY})")
else()
  message(STATUS "MP3 export disabled: lame not found")
endif()
if(SANITY_HAVE_SNDFILE)
  message(STATUS "FLAC/Ogg export enabled (libsndfile at ${SNDFILE_LIBRARY})")
else()
  message(STATUS "FLAC/Ogg export disabled: libsndfile not found")
endif()

target_compile_definitions(sanity_core PUBLIC
  SANITY_HAVE_MP3=$<BOOL:${SANITY_HAVE_MP3}>
  SANITY_HAVE_SNDFILE=$<BOOL:${SANITY_HAVE_SNDFILE}>)

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
  find_package(Qt6 QUIET COMPONENTS Widgets Multimedia)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(podcast-sanity
      gui/main.cpp
      gui/mainwindow.h
      gui/mainwindow.cpp
      gui/app_state.h
      gui/audio_capture.h
      gui/widgets.h
      gui/theme.h
      gui/pages/pages.h
      gui/app_state.cpp
      gui/audio_capture.cpp
      gui/widgets.cpp
      gui/theme.cpp
      gui/pages/page_base.cpp
      gui/pages/record_page.cpp
      gui/pages/master_page.cpp
      gui/pages/standards_page.cpp
      gui/pages/library_page.cpp
      gui/pages/export_page.cpp
      gui/pages/settings_page.cpp
      gui/pages/diagnostics_page.cpp
    )
    target_link_libraries(podcast-sanity PRIVATE sanity_core Qt6::Widgets)
    target_include_directories(podcast-sanity PRIVATE gui)
    target_compile_definitions(podcast-sanity PRIVATE SANITY_VERSION="${PROJECT_VERSION}")
    if(TARGET Qt6::Multimedia)
      target_link_libraries(podcast-sanity PRIVATE Qt6::Multimedia)
      target_compile_definitions(podcast-sanity PRIVATE SANITY_HAVE_QTMM=1)
      message(STATUS "Qt6 Multimedia found: live recording enabled")
    else()
      target_compile_definitions(podcast-sanity PRIVATE SANITY_HAVE_QTMM=0)
      message(STATUS "Qt6 Multimedia missing: recording works in file/corpus test mode only")
    endif()
    message(STATUS "Qt6 found: building podcast-sanity GUI")

    # Deploy the Qt runtime next to the executable so the packaged tree is
    # self-contained. Without this the MSI/ZIP would ship an .exe that cannot
    # start, because CPack only knows about the targets we install by hand.
    if(WIN32)
      qt_generate_deploy_app_script(
        TARGET podcast-sanity
        OUTPUT_SCRIPT qt_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
      install(SCRIPT ${qt_deploy_script})
    endif()

    # Automated interface test: walks every page, runs the chain and exports an
    # MP3. Runs under a virtual display in CI.
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
      add_test(NAME gui
        COMMAND ${XVFB_RUN} -a "--server-args=-screen 0 1400x900x24"
                $<TARGET_FILE:podcast-sanity> --selftest --selftest-seconds 8)
    else()
      add_test(NAME gui
        COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:podcast-sanity> --selftest --selftest-seconds 8)
    endif()
    set_tests_properties(gui PROPERTIES TIMEOUT 300)
  else()
    message(STATUS "Qt6 not found: GUI skipped")
  endif()
endif()

# ---- install rules --------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS podcast-sanity-render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET podcast-sanity)
  install(TARGETS podcast-sanity RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Desktop integration is a freedesktop concept. Installing it on Windows would
# drop .desktop files and AppStream metadata into the MSI, which nothing there
# can read.
if(UNIX)
  install(FILES data/podcast-sanity.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES data/io.github.voloire.podcast-sanity.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
  install(FILES data/podcast-sanity.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
          RENAME podcast-sanity.svg)
endif()

# The codec libraries come from vcpkg as DLLs on Windows, so the package must
# carry their runtime. They are collected wholesale rather than one by one
# because libsndfile pulls in a dozen transitive dependencies of its own.
if(WIN32)
  set(SANITY_VCPKG_BIN "")
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    set(SANITY_VCPKG_BIN "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT} AND VCPKG_TARGET_TRIPLET)
    set(SANITY_VCPKG_BIN "$ENV{VCPKG_INSTALLATION_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(SANITY_VCPKG_BIN AND EXISTS "${SANITY_VCPKG_BIN}")
    file(GLOB SANITY_CODEC_RUNTIME "${SANITY_VCPKG_BIN}/*.dll")
    list(LENGTH SANITY_CODEC_RUNTIME SANITY_CODEC_RUNTIME_COUNT)
    if(SANITY_CODEC_RUNTIME_COUNT GREATER 0)
      install(FILES ${SANITY_CODEC_RUNTIME} DESTINATION ${CMAKE_INSTALL_BINDIR})
      message(STATUS "Shipping ${SANITY_CODEC_RUNTIME_COUNT} codec DLL(s) from ${SANITY_VCPKG_BIN}")
    endif()
  endif()
endif()

# ---- packaging -----------------------------------------------------------
set(CPACK_PACKAGE_NAME "podcast-sanity")
set(CPACK_PACKAGE_VENDOR "Voloire")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local podcast recording and automatic mastering")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Voloire <dev@voloire.local>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/voloire/podcast-sanity")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES "podcast-sanity" "podcast-sanity")
# Without this the WiX default is "<name> <version>", which puts every release
# in its own "podcast-sanity 0.1.0" folder under Program Files.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "podcast-sanity")

# --- Debian / Ubuntu -------------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Voloire")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6, libqt6multimedia6, libmp3lame0, libsndfile1")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-qpa-plugins, qt6-multimedia-plugins, ffmpeg")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# --- Windows (WiX MSI) -----------------------------------------------------
# A fixed Upgrade GUID means upgrades replace the previous install instead of
# stacking next to it.
set(CPACK_WIX_UPGRADE_GUID "9F2C6D4E-7B31-4A58-9C0E-2D5A6F1B8E30")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "podcast-sanity")
# Branding images are optional: only reference the ones that exist so the MSI
# still builds on a bare checkout.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/podcast-sanity.ico")
  set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/data/podcast-sanity.ico")
  set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/data/podcast-sanity.ico")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/wix-banner.bmp")
  set(CPACK_WIX_UI_BANNER "${CMAKE_CURRENT_SOURCE_DIR}/data/wix-banner.bmp")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/wix-dialog.bmp")
  set(CPACK_WIX_UI_DIALOG "${CMAKE_CURRENT_SOURCE_DIR}/data/wix-dialog.bmp")
endif()
# The Windows bundle must carry the Qt runtime and the codec DLLs (optional).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/wix_patch.xml")
  set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/wix_patch.xml")
endif()

if(WIN32)
  # CPack's WiX generator accepts only a .txt or .rtf license file, while the
  # project ships a plain extensionless LICENSE. Hand it a text copy instead of
  # keeping a second, drifting copy in the repository.
  set(SANITY_WIX_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${SANITY_WIX_LICENSE}" COPYONLY)
  set(CPACK_RESOURCE_FILE_LICENSE "${SANITY_WIX_LICENSE}")
  set(CPACK_GENERATOR "WIX;ZIP")
else()
  set(CPACK_GENERATOR "DEB;TGZ")
endif()
include(CPack)
