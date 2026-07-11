cmake_minimum_required(VERSION 3.22)

# Portable default: run on macOS 11+ (the host SDK would otherwise pin minos to the
# current OS, so the .app would refuse to launch on older macOS). Must precede project().
# For a portable binary also pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()

project(OrbitCaptureApp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.14. By default fetch the pinned public release; set -DOC_JUCE_SRC=<path>
# to reuse a local JUCE checkout (offline build / shared source tree).
set(OC_JUCE_SRC "" CACHE PATH "Optional path to a local JUCE source tree")

include(FetchContent)
if(OC_JUCE_SRC)
  FetchContent_Declare(juce SOURCE_DIR "${OC_JUCE_SRC}")
else()
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.14)
endif()
FetchContent_MakeAvailable(juce)

# felitronics-core (local sibling checkout): the oc:: capture headers are compat shims over it,
# and the spectrum display curves come straight from felitronics::analysis::offline.
set(OC_CORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../felitronics-core" CACHE PATH "felitronics-core source")
FetchContent_Declare(felitronics_core SOURCE_DIR "${OC_CORE_SRC}")
FetchContent_MakeAvailable(felitronics_core)

# felitronics-appkit — the family's shared APP-SIDE JUCE infrastructure (the update checker).
# Same sibling-checkout convention as felitronics-core; CI checks it out next to this repo.
set(OC_APPKIT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../felitronics-appkit" CACHE PATH "felitronics-appkit source")
FetchContent_Declare(felitronics_appkit SOURCE_DIR "${OC_APPKIT_SRC}")
FetchContent_MakeAvailable(felitronics_appkit)

# The version the update checker reports. Release CI passes the tag (-DOC_VERSION=vX.Y.Z);
# otherwise a configure-time `git describe` — a dev stamp counts OLDER than any release (the
# appkit dev rule), so between-releases builds show the badge once a release exists. Configure-time
# is fine here: CI always configures fresh, and a stale local stamp only mislabels a dev build.
set(OC_VERSION "" CACHE STRING "Version stamp for the update check (vX.Y.Z); empty = git describe")
if(NOT OC_VERSION)
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE OC_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  if(NOT OC_VERSION)
    set(OC_VERSION "unknown")
  endif()
endif()
message(STATUS "OrbitCapture update-check version stamp: ${OC_VERSION}")

# Library versions for the header's update/about window — read from the subprojects' own
# project() scopes, so they can never drift from what was actually built.
get_directory_property(OC_CORE_VERSION   DIRECTORY "${felitronics_core_SOURCE_DIR}"   DEFINITION PROJECT_VERSION)
get_directory_property(OC_APPKIT_VERSION DIRECTORY "${felitronics_appkit_SOURCE_DIR}" DEFINITION PROJECT_VERSION)

juce_add_gui_app(OrbitCaptureApp
    PRODUCT_NAME "OrbitCapture"
    COMPANY_NAME "Darwin's Cat"
    BUNDLE_ID "com.darwinscat.orbitcapture"
    # Bundle / executable icon — the product's converging-arrows orbit mark
    # (resources/icon/OrbitCapture.svg → …-1024.png; same pipeline as orbitcab). juceaide builds
    # the macOS AppIcon.icns and the Windows .ico from this one 1024² PNG; no ICON_SMALL on
    # purpose (JUCEUtils appends BIG-then-SMALL but juceaide reads them small-then-big, so
    # passing both can swap the slots). Linux launcher icons are a packaging concern.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/OrbitCapture-1024.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "OrbitCapture records the cabinet microphone to capture an impulse response.")
juce_add_binary_data(OrbitCaptureAssets SOURCES
    resources/catlogo.svg
    resources/Michroma-Regular.ttf
    resources/cats.wav
    resources/space.wav
    resources/eleven.wav)

target_sources(OrbitCaptureApp PRIVATE Main.cpp)
target_compile_features(OrbitCaptureApp PRIVATE cxx_std_20)
target_compile_definitions(OrbitCaptureApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="OrbitCapture"
    JUCE_APPLICATION_VERSION_STRING="0.1.0"
    OC_VERSION_STRING="${OC_VERSION}"
    OC_CORE_VERSION_STRING="${OC_CORE_VERSION}"
    OC_APPKIT_VERSION_STRING="${OC_APPKIT_VERSION}")
target_include_directories(OrbitCaptureApp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(OrbitCaptureApp PRIVATE
    OrbitCaptureAssets
    felitronics::appkit
    felitronics::core
    felitronics::measurement
    felitronics::analysis
    felitronics::blend
    felitronics::convolution
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
