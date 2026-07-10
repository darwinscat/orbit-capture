cmake_minimum_required(VERSION 3.20)
project(oc_dsp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# felitronics-core (local sibling checkout): the oc:: headers are now compat shims over it.
set(OC_CORE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../felitronics-core" CACHE PATH "felitronics-core source")
include(FetchContent)
FetchContent_Declare(felitronics_core SOURCE_DIR "${OC_CORE_SRC}")
FetchContent_MakeAvailable(felitronics_core)

add_library(oc_dsp INTERFACE)
target_include_directories(oc_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(oc_dsp INTERFACE felitronics::core felitronics::measurement felitronics::analysis)

enable_testing()

add_executable(test_deconv tests/test_deconv.cpp)
target_link_libraries(test_deconv PRIVATE oc_dsp)
target_compile_options(test_deconv PRIVATE -O2)
add_test(NAME deconv COMMAND test_deconv)

add_executable(test_post tests/test_post.cpp)
target_link_libraries(test_post PRIVATE oc_dsp)
target_compile_options(test_post PRIVATE -O2)
add_test(NAME post COMMAND test_post)

add_executable(test_wav tests/test_wav.cpp)
target_link_libraries(test_wav PRIVATE oc_dsp)
target_compile_options(test_wav PRIVATE -O2)
add_test(NAME wav COMMAND test_wav)

# end-to-end CLI harness (not a test): raw sweep recording -> IR wav
add_executable(oc_deconv tools/oc_deconv.cpp)
target_link_libraries(oc_deconv PRIVATE oc_dsp)
target_compile_options(oc_deconv PRIVATE -O2)

# write our ESS sweep to a WAV (play it through the rig, record the mic)
add_executable(oc_gen_sweep tools/oc_gen_sweep.cpp)
target_link_libraries(oc_gen_sweep PRIVATE oc_dsp)
target_compile_options(oc_gen_sweep PRIVATE -O2)

add_executable(oc_abnull tools/oc_abnull.cpp)
target_link_libraries(oc_abnull PRIVATE oc_dsp)
target_compile_options(oc_abnull PRIVATE -O2)
