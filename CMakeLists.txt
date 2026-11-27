cmake_minimum_required(VERSION 3.22)

project(Nightshift VERSION 0.4.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the exe runs without the VC++ redistributable
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(TE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/tracktion_engine)
add_subdirectory(${TE_DIR}/modules/juce juce)
add_subdirectory(${TE_DIR}/modules tracktion)

juce_add_gui_app(Nightshift
    PRODUCT_NAME "NightShift"       # what Windows shows (the exe, its details, the window); the target stays Nightshift
    COMPANY_NAME "NightWriter"
    COMPANY_COPYRIGHT "Copyright (C) 2026 NightWriter. Free software under the GNU GPL v3 or later."
    BUNDLE_ID "com.nightwriter.nightshift"
    VERSION ${PROJECT_VERSION}
    # Cut from the artwork in resources/icon_source.jpg by tools/make_icon.ps1: the whole tile
    # (Windows' 256 px icon) and the moon and wave alone (16-48 px, where the name can't be read)
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png
    ICON_SMALL ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_small.png
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    DOCUMENT_EXTENSIONS nsproj      # macOS: the bundle claims .nsproj, so Finder opens projects in NightShift
    # macOS: without this the audio interface's inputs are silent (the system asks the first time)
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NightShift records your guitar, bass and voice through your audio interface."
    # macOS asks before an app reads Downloads: the sidebar's Samples lists it (asked the first time Samples opens)
    PLIST_TO_MERGE "<plist><dict><key>NSDownloadsFolderUsageDescription</key><string>NightShift lists the sounds in your Downloads in the sidebar's Samples, to hear them and drag them into a song.</string></dict></plist>")

juce_generate_juce_header(Nightshift)

if (WIN32)
    # The exe's icon goes in through the .rc JUCE writes, which names icon.ico but stays the same when
    # the icon changes, so the resources were never compiled again (the new icon of 2026-11-06 didn't
    # reach the exe). Make them depend on the icon itself.
    get_target_property(nightshift_generated Nightshift JUCE_GENERATED_SOURCES_DIRECTORY)
    set_source_files_properties("${nightshift_generated}/Nightshift_resources.rc"
        PROPERTIES OBJECT_DEPENDS "${nightshift_generated}/icon.ico")

    # Likewise the .rc is written again only for a new icon, not a new name: renamed NightShift, the
    # exe's details still said Nightshift. Write it again when the app's details change too.
    get_target_property(nightshift_info Nightshift JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${nightshift_generated}/Nightshift_resources.rc" APPEND DEPENDS "${nightshift_info}")
endif()

file(GLOB_RECURSE NIGHTSHIFT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
target_sources(Nightshift PRIVATE ${NIGHTSHIFT_SOURCES})
target_include_directories(Nightshift PRIVATE src)

# Neural Amp Modeler's core (MIT, Steven Atkinson) for the NAM Amp effect, with what it ships with: Eigen (MPL 2.0),
# nlohmann/json (MIT) and AudioDSPTools' resampler (MIT, from iPlug2 and WDL). It's at the tag mac/build.sh and
# NEXT.md pin, in external/NeuralAmpModelerCore (cloned with --recurse-submodules)
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore)
if (NOT EXISTS ${NAM_DIR}/NAM/get_dsp.h)
    message(FATAL_ERROR "Neural Amp Modeler's core isn't in ${NAM_DIR}: git clone --branch v0.6.0 --recurse-submodules https://github.com/sdatkinson/NeuralAmpModelerCore.git there")
endif()
file(GLOB NAM_SOURCES CONFIGURE_DEPENDS ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/wavenet/*.cpp)
target_sources(Nightshift PRIVATE ${NAM_SOURCES})
target_include_directories(Nightshift SYSTEM PRIVATE
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann
    ${NAM_DIR}/Dependencies/AudioDSPTools)
target_compile_definitions(Nightshift PRIVATE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST
    NIGHTSHIFT_NAM_EXAMPLES="${NAM_DIR}/example_models")
if (MSVC)
    # NAM's own sources build as its CMake builds them: without Windows' min and max macros, and quietly
    set_source_files_properties(${NAM_SOURCES} PROPERTIES COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN" COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${NAM_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# LAME 3.100 (GNU LGPL 2 or later, lame.sourceforge.io), the MP3 encoder Export uses: built here from its
# source, no lame.exe. The release tarball is fetched once into external/ (from SourceForge, or Debian's
# copy of the same file) and checked against its SHA-256 every configure; only libmp3lame is compiled,
# with NightShift's config.h (cmake/lame), into a static library of its own
set(LAME_VERSION 3.100)
set(LAME_SHA256 ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e)
set(LAME_ARCHIVE ${CMAKE_CURRENT_SOURCE_DIR}/external/lame-${LAME_VERSION}.tar.gz)
set(LAME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lame-${LAME_VERSION})

if (NOT EXISTS ${LAME_ARCHIVE})
    foreach (url https://downloads.sourceforge.net/project/lame/lame/${LAME_VERSION}/lame-${LAME_VERSION}.tar.gz
                 https://deb.debian.org/debian/pool/main/l/lame/lame_${LAME_VERSION}.orig.tar.gz)
        message(STATUS "Getting LAME ${LAME_VERSION} from ${url}")
        file(DOWNLOAD ${url} ${LAME_ARCHIVE}.part STATUS got TLS_VERIFY ON)
        list(GET got 0 code)

        if (code EQUAL 0)
            file(SHA256 ${LAME_ARCHIVE}.part hash)

            if (hash STREQUAL LAME_SHA256)
                file(RENAME ${LAME_ARCHIVE}.part ${LAME_ARCHIVE})
                break()
            endif()

            message(STATUS "That wasn't LAME ${LAME_VERSION} as released (SHA-256 ${hash})")
        endif()

        file(REMOVE ${LAME_ARCHIVE}.part)
    endforeach()
endif()

if (NOT EXISTS ${LAME_ARCHIVE})
    message(FATAL_ERROR "Couldn't get LAME ${LAME_VERSION}: download lame-${LAME_VERSION}.tar.gz (SHA-256 ${LAME_SHA256}) from lame.sourceforge.io into ${CMAKE_CURRENT_SOURCE_DIR}/external")
endif()

file(SHA256 ${LAME_ARCHIVE} LAME_HASH)

if (NOT LAME_HASH STREQUAL LAME_SHA256)
    message(FATAL_ERROR "${LAME_ARCHIVE} isn't LAME ${LAME_VERSION} as released (its SHA-256 is ${LAME_HASH}, not ${LAME_SHA256}): delete it and configure again")
endif()

if (NOT EXISTS ${LAME_DIR}/include/lame.h)
    file(ARCHIVE_EXTRACT INPUT ${LAME_ARCHIVE} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/external)
endif()

add_library(nightshift_lame STATIC
    ${LAME_DIR}/libmp3lame/VbrTag.c ${LAME_DIR}/libmp3lame/bitstream.c ${LAME_DIR}/libmp3lame/encoder.c
    ${LAME_DIR}/libmp3lame/fft.c ${LAME_DIR}/libmp3lame/gain_analysis.c ${LAME_DIR}/libmp3lame/id3tag.c
    ${LAME_DIR}/libmp3lame/lame.c ${LAME_DIR}/libmp3lame/newmdct.c ${LAME_DIR}/libmp3lame/presets.c
    ${LAME_DIR}/libmp3lame/psymodel.c ${LAME_DIR}/libmp3lame/quantize.c ${LAME_DIR}/libmp3lame/quantize_pvt.c
    ${LAME_DIR}/libmp3lame/reservoir.c ${LAME_DIR}/libmp3lame/set_get.c ${LAME_DIR}/libmp3lame/tables.c
    ${LAME_DIR}/libmp3lame/takehiro.c ${LAME_DIR}/libmp3lame/util.c ${LAME_DIR}/libmp3lame/vbrquantize.c
    ${LAME_DIR}/libmp3lame/version.c)
target_include_directories(nightshift_lame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lame ${LAME_DIR}/include ${LAME_DIR}/libmp3lame)
target_compile_definitions(nightshift_lame PRIVATE HAVE_CONFIG_H)

if (MSVC)
    target_compile_definitions(nightshift_lame PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(nightshift_lame PRIVATE /W0)
else()
    target_compile_options(nightshift_lame PRIVATE -w)
endif()

# Only the encoder's own file sees lame.h (so every other file's build stays as it was)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Mp3Encoder.cpp PROPERTIES INCLUDE_DIRECTORIES ${LAME_DIR}/include)

target_compile_definitions(Nightshift PRIVATE
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>               # JUCE bundles the ASIO SDK under the GPLv3
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>       # a Mac's Audio Units, beside VST3
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Nightshift,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Nightshift,JUCE_VERSION>"
    # The engine's time-stretchers: SoundTouch (LGPL 2.1) and Signalsmith Stretch (MIT), both in its 3rd_party
    # (NOTICES.txt). NightShift renders stretched audio clips with Signalsmith Stretch itself (src/audio/Stretcher),
    # lined up to the sample; --stretchtest holds SoundTouch beside it
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1)

# Selawik (Microsoft, SIL Open Font License 1.1: resources/fonts/Selawik-OFL.txt): Segoe UI's stand-in where
# it isn't installed (a Mac), Regular and Semibold (NightShift's bold)
juce_add_binary_data(NightshiftData SOURCES resources/logo_64.png resources/fonts/selawk.ttf resources/fonts/selawksb.ttf)

target_link_libraries(Nightshift PRIVATE
    NightshiftData
    nightshift_lame
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    $<$<PLATFORM_ID:Windows>:crypt32>
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(Nightshift PRIVATE /bigobj /MP)

    # 16 MB of stack, not Windows' default 1 MB: the tests and --snapshot build the whole studio
    # (MainComponent, every panel in it) as a local, and the Master tab's second page took it past
    # 1 MB (a stack overflow, 0xC00000FD, before anything ran)
    target_link_options(Nightshift PRIVATE /STACK:16777216)
elseif (APPLE)
    # The same 16 MB for the main thread on a Mac (its default is 8 MB)
    target_link_options(Nightshift PRIVATE "-Wl,-stack_size,0x1000000")
endif()

# NightShift is GPLv3-or-later (LICENSE): its text goes beside the exe as LICENSE.txt, as install.cmd
# and the setup put it beside the installed one, for About's Licence... button
if (APPLE)
    # In a Mac app, inside the bundle's Resources (About's Licence... looks there)
    add_custom_command(TARGET Nightshift POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_BUNDLE_CONTENT_DIR:Nightshift>/Resources/LICENSE.txt")
else()
    add_custom_command(TARGET Nightshift POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:Nightshift>/LICENSE.txt")
endif()
