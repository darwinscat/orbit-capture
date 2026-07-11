# OrbitCapture headless test gates — JUCE-FREE console tests for the src/core engine layer.
# The whole point of the de-monolith: this logic is testable without JUCE / a widget tree.
cmake_minimum_required(VERSION 3.22)
project(orbitcapture_tests CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(OC_CORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../felitronics-core" CACHE PATH "felitronics-core source")
include(FetchContent)
FetchContent_Declare(felitronics_core SOURCE_DIR "${OC_CORE_SRC}")
FetchContent_MakeAvailable(felitronics_core)

enable_testing()

set(OC_TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/include
    ${felitronics_core_SOURCE_DIR}/test_support)
set(OC_TEST_LIBS felitronics::core felitronics::measurement felitronics::analysis felitronics::blend)

function(oc_add_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${OC_TEST_INCLUDES})
    target_link_libraries(${name} PRIVATE ${OC_TEST_LIBS})
    target_compile_options(${name} PRIVATE -O2)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

oc_add_test(blend_kernels_test   BlendKernelsTests.cpp)
oc_add_test(micset_model_test    MicSetModelTests.cpp)
oc_add_test(export_plan_test     ExportPlanTests.cpp)
oc_add_test(rt_streams_test      RtStreamsTests.cpp)
oc_add_test(ir_import_test       IrImportTests.cpp)
oc_add_test(auto_align_test      AutoAlignTests.cpp)
oc_add_test(blend_engine_test    BlendEngineTests.cpp)
oc_add_test(blend_null_test      BlendNullTest.cpp)
oc_add_test(capture_pipeline_test CapturePipelineTests.cpp)

# ---- juce_core tier: session/take persistence (de-monolith step 6) --------------------------------
# Everything above this line is JUCE-free by design (the whole point of the de-monolith). The
# session/take model needs juce::var/File/JSON to bridge to the app's take.json — mirrors how
# app/CMakeLists.txt fetches JUCE, so -DOC_JUCE_SRC=<path> reuses a local checkout offline.
set(OC_JUCE_SRC "" CACHE PATH "Optional path to a local JUCE source tree")
if(OC_JUCE_SRC)
    FetchContent_Declare(juce SOURCE_DIR "${OC_JUCE_SRC}")
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.14)
endif()
FetchContent_MakeAvailable(juce)

function(oc_add_juce_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${OC_TEST_INCLUDES})
    target_link_libraries(${name} PRIVATE ${OC_TEST_LIBS} juce::juce_core)
    target_compile_definitions(${name} PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    target_compile_options(${name} PRIVATE -O2)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

oc_add_juce_test(session_roundtrip_test SessionRoundtripTest.cpp)
oc_add_juce_test(report_writer_test     ReportWriterTests.cpp)
oc_add_juce_test(session_store_test     SessionStoreTest.cpp)
oc_add_juce_test(session_writer_parity_test SessionWriterParityTest.cpp)

oc_add_juce_test(session_fixture_test   SessionFixtureTest.cpp)
target_compile_definitions(session_fixture_test PRIVATE
    OC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/legacy_takes")
