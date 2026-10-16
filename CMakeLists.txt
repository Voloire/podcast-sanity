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
option(SANITY_REQUIRE_CODECS "Fail if MP3 or FLAC/Ogg support is missing" OFF)

set(SANITY_CORE_SOURCES
  src/json.cpp
  src/io_wav.cpp
  src/io_sndfile.cpp
  src/distribution.cpp
  src/music_bed.cpp
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
find_package(PkgConfig QUIET)
set(SANITY_HAVE_MP3 FALSE)
set(SANITY_HAVE_SNDFILE FALSE)
find_package(mp3lame CONFIG QUIET)
find_package(SndFile CONFIG QUIET)
if(TARGET mp3lame::mp3lame)
  target_link_libraries(sanity_core PUBLIC mp3lame::mp3lame)
  set(SANITY_HAVE_MP3 TRUE)
endif()
if(TARGET SndFile::sndfile)
  target_link_libraries(sanity_core PUBLIC SndFile::sndfile)
  set(SANITY_HAVE_SNDFILE TRUE)
endif()
if(PkgConfig_FOUND)
  pkg_check_modules(MP3LAME QUIET lame)
  pkg_check_modules(SNDFILE QUIET sndfile)
  if(MP3LAME_FOUND AND NOT SANITY_HAVE_MP3)
    set(SANITY_HAVE_MP3 TRUE)
    target_link_libraries(sanity_core PUBLIC ${MP3LAME_LIBRARIES})
    target_include_directories(sanity_core PUBLIC ${MP3LAME_INCLUDE_DIRS})
  endif()
  if(SNDFILE_FOUND AND NOT SANITY_HAVE_SNDFILE)
    set(SANITY_HAVE_SNDFILE TRUE)
    target_link_libraries(sanity_core PUBLIC ${SNDFILE_LIBRARIES})
    target_include_directories(sanity_core PUBLIC ${SNDFILE_INCLUDE_DIRS})
  endif()
endif()
message(STATUS "Codecs: MP3=${SANITY_HAVE_MP3}, FLAC/Ogg=${SANITY_HAVE_SNDFILE}")
if(SANITY_REQUIRE_CODECS AND (NOT SANITY_HAVE_MP3 OR NOT SANITY_HAVE_SNDFILE))
  message(FATAL_ERROR "Release package requires LAME and libsndfile")
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

# CLI contract: a missing option value is an error, not a crash; --help lists
# the distribution switch added in Phase 3.
add_test(NAME cli_missing_value COMMAND podcast-sanity-render in.wav --out)
set_tests_properties(cli_missing_value PROPERTIES
  PASS_REGULAR_EXPRESSION "option --out needs a value")
add_test(NAME cli_help COMMAND podcast-sanity-render --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "--deliver")
add_test(NAME cli_list_profiles COMMAND podcast-sanity-render --list-profiles)
set_tests_properties(cli_list_profiles PROPERTIES
  PASS_REGULAR_EXPRESSION "podcast-voice .*\\(recommended\\)")

# ---- end-to-end harness ----------------------------------------------------
# Generates synthetic episodes with ground truth, runs the whole chain and
# judges the result. Slower than the unit tests, so it is a separate target.
add_executable(sanity_e2e_tests tests/e2e_main.cpp)
target_link_libraries(sanity_e2e_tests PRIVATE sanity_core)
target_include_directories(sanity_e2e_tests PRIVATE tests)
target_compile_definitions(sanity_e2e_tests PRIVATE SANITY_VERSION="${PROJECT_VERSION}")
add_test(NAME e2e COMMAND sanity_e2e_tests)
set_tests_properties(e2e PROPERTIES TIMEOUT 900)

# The detector is calibrated at 48 kHz; this run proves 44.1 kHz material gets
# the same precision and recall.
add_test(NAME e2e_44k COMMAND sanity_e2e_tests --rate 44100)
set_tests_properties(e2e_44k PROPERTIES TIMEOUT 900)

add_test(NAME e2e_30s COMMAND sanity_e2e_tests --seconds 30)
add_test(NAME e2e_45s COMMAND sanity_e2e_tests --seconds 45)
set_tests_properties(e2e_30s e2e_45s PROPERTIES TIMEOUT 900)

# ---- GUI ------------------------------------------------------------------
if(SANITY_WITH_GUI)
  # Multimedia is optional: without it the app still records from a file or the
  # synthetic corpus, so a Qt install that lacks the module must not disable the
  # whole GUI.
  find_package(Qt6 QUIET COMPONENTS Widgets OPTIONAL_COMPONENTS Multimedia)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(podcast-sanity WIN32
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
    if(WIN32)
      configure_file(packaging/windows.rc.in windows.rc @ONLY)
      target_sources(podcast-sanity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
    endif()
    qt_add_resources(podcast-sanity app_icon PREFIX "/" BASE data
      FILES data/podcast-sanity.ico)
    if(TARGET Qt6::Multimedia)
      target_link_libraries(podcast-sanity PRIVATE Qt6::Multimedia)
      target_compile_definitions(podcast-sanity PRIVATE SANITY_HAVE_QTMM=1)
      message(STATUS "Qt6 Multimedia found: live recording enabled")
    else()
      target_compile_definitions(podcast-sanity PRIVATE SANITY_HAVE_QTMM=0)
      message(STATUS "Qt6 Multimedia missing: recording works in file/corpus test mode only")
    endif()
    message(STATUS "Qt6 found: building podcast-sanity GUI")

    # Automated interface test: walks every page, runs the chain and exports an
    # MP3. Runs under a virtual display in CI.
    find_program(XVFB_RUN xvfb-run)
    if(WIN32)
      find_program(SANITY_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
      add_custom_command(TARGET podcast-sanity POST_BUILD
        COMMAND "${SANITY_WINDEPLOYQT}" --release --no-translations --no-compiler-runtime
                "$<TARGET_FILE:podcast-sanity>"
        VERBATIM)
      add_test(NAME gui COMMAND podcast-sanity --selftest --selftest-seconds 8)
    elseif(XVFB_RUN)
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
    message(FATAL_ERROR "SANITY_WITH_GUI=ON requires Qt6 Widgets")
  endif()
endif()

# ---- install rules --------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS podcast-sanity-render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET podcast-sanity)
  install(TARGETS podcast-sanity RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(WIN32)
    find_program(SANITY_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    # Deploy into the install tree: copying DLLs beside build outputs alone does
    # not include them in an MSI. The same payload is used by MSI and ZIP.
    configure_file(packaging/deploy-windows.cmake.in deploy-windows.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy-windows.cmake")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include(InstallRequiredSystemLibraries)
    install(FILES LICENSE packaging/THIRD-PARTY.md DESTINATION licenses)
    if(VCPKG_INSTALLED_DIR)
      foreach(codec mp3lame libsndfile libflac libogg libvorbis mpg123 opus)
        install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${codec}/copyright"
          DESTINATION "licenses/${codec}")
      endforeach()
    endif()
  else()
  install(FILES data/podcast-sanity.desktop
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES data/io.github.voloire.podcast-sanity.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
  install(FILES data/podcast-sanity.svg
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
           RENAME podcast-sanity.svg)
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
  configure_file(LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" COPYONLY)
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
  if(CPACK_WIX_ROOT)
    file(TO_CMAKE_PATH "${CPACK_WIX_ROOT}" CPACK_WIX_ROOT)
  endif()
  if(CMAKE_VERSION VERSION_LESS 4.3)
    message(FATAL_ERROR "Windows per-user MSI packaging requires CMake 4.3 or newer")
  endif()
  set(CPACK_GENERATOR "WIX;ZIP")
  set(CPACK_WIX_INSTALL_SCOPE "perUser")
  set(CPACK_PACKAGE_FILE_NAME "podcast-sanity-${PROJECT_VERSION}-win64")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Podcast Sanity")
  set(CPACK_CREATE_DESKTOP_LINKS "podcast-sanity")
else()
  set(CPACK_GENERATOR "DEB;TGZ")
endif()
include(CPack)
