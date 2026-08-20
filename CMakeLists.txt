cmake_minimum_required(VERSION 3.24)

project(WIPReviewProbe VERSION 1.1.0 LANGUAGES CXX)

include(FetchContent)

option(WIPREVIEW_BUILD_TESTS "Build the host-independent cumulative formatter tests" ON)
option(WIPREVIEW_BUILD_BENCHMARKS "Build the opt-in CPU performance benchmark" OFF)
option(WIPREVIEW_ADHOC_SIGN "Ad-hoc sign the macOS OFX bundle after building" ON)
set(WIPREVIEW_OPENFX_SDK_ROOT "" CACHE PATH
    "Existing OpenFX SDK checkout (optional; avoids fetching the pinned SDK)")

# The SDK is headers-only for this probe. It is kept out of the source tree and
# pinned to an immutable upstream revision so host-facing ABI changes cannot
# arrive accidentally. A local checkout can be supplied for offline builds.
if(WIPREVIEW_OPENFX_SDK_ROOT)
  set(openfx_sdk_SOURCE_DIR "${WIPREVIEW_OPENFX_SDK_ROOT}")
  if(NOT EXISTS "${openfx_sdk_SOURCE_DIR}/include/ofxImageEffect.h")
    message(FATAL_ERROR
      "WIPREVIEW_OPENFX_SDK_ROOT does not contain include/ofxImageEffect.h")
  endif()
else()
  FetchContent_Declare(
    openfx_sdk
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    GIT_TAG 3de640d6f645fe6e346acd57e568d8b0a5ae4574
    GIT_SHALLOW FALSE
    # There is intentionally no CMakeLists here. Fetch the SDK without adding
    # its examples, support library, tests, or third-party dependencies.
    SOURCE_SUBDIR wipreview_headers_only
  )
  FetchContent_MakeAvailable(openfx_sdk)
endif()

add_library(openfx_sdk_headers INTERFACE)
target_include_directories(openfx_sdk_headers SYSTEM INTERFACE
  "${openfx_sdk_SOURCE_DIR}/include")

add_library(wipreview_probe_core STATIC
  src/color_pipeline.cpp
  src/probe_core.cpp
  src/calculated_field_resolver.cpp
)
target_include_directories(wipreview_probe_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(wipreview_probe_core PUBLIC cxx_std_17)
set_target_properties(wipreview_probe_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

if(WIPREVIEW_BUILD_BENCHMARKS)
  add_executable(wipreview_cpu_benchmark benchmarks/cpu_renderer_benchmark.cpp)
  target_link_libraries(wipreview_cpu_benchmark PRIVATE wipreview_probe_core)
endif()

if(APPLE)
  find_library(WIPREVIEW_CORETEXT_FRAMEWORK CoreText REQUIRED)
  find_library(WIPREVIEW_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  find_library(WIPREVIEW_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  add_library(wipreview_text_rasterizer STATIC
    src/text_effects.cpp
    src/text_rasterizer_macos.cpp
  )
  target_link_libraries(wipreview_text_rasterizer PUBLIC
    wipreview_probe_core
    "${WIPREVIEW_CORETEXT_FRAMEWORK}"
    "${WIPREVIEW_COREGRAPHICS_FRAMEWORK}"
    "${WIPREVIEW_COREFOUNDATION_FRAMEWORK}"
  )
else()
  add_library(wipreview_text_rasterizer STATIC
    src/text_effects.cpp
    src/text_rasterizer_stub.cpp
  )
  target_link_libraries(wipreview_text_rasterizer PUBLIC wipreview_probe_core)
endif()

target_include_directories(wipreview_text_rasterizer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(wipreview_text_rasterizer PUBLIC cxx_std_17)
set_target_properties(wipreview_text_rasterizer PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(WIPReviewProbe MODULE
  src/probe.cpp
)
target_compile_features(WIPReviewProbe PRIVATE cxx_std_17)
target_link_libraries(WIPReviewProbe PRIVATE
  openfx_sdk_headers
  wipreview_text_rasterizer
)
target_compile_definitions(WIPReviewProbe PRIVATE
  WIPREVIEW_PROBE_VERSION="${PROJECT_VERSION}"
)
set_target_properties(WIPReviewProbe PROPERTIES
  PREFIX ""
  SUFFIX ".ofx"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

if(APPLE)
  set(WIPREVIEW_BUNDLE_DIR
      "${CMAKE_BINARY_DIR}/WIPReviewProbe.ofx.bundle")
  set(WIPREVIEW_BUNDLE_CONTENTS "${WIPREVIEW_BUNDLE_DIR}/Contents")
  set(WIPREVIEW_BUNDLE_BINARY_DIR "${WIPREVIEW_BUNDLE_CONTENTS}/MacOS")

  set_target_properties(WIPReviewProbe PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${WIPREVIEW_BUNDLE_BINARY_DIR}"
  )

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    "${WIPREVIEW_BUNDLE_CONTENTS}/Info.plist"
    @ONLY
  )

  if(WIPREVIEW_ADHOC_SIGN)
    add_custom_command(TARGET WIPReviewProbe POST_BUILD
      COMMAND /usr/bin/codesign --force --sign - "${WIPREVIEW_BUNDLE_DIR}"
      COMMENT "Ad-hoc signing WIPReviewProbe.ofx.bundle"
      VERBATIM
    )
  endif()

  install(DIRECTORY "${WIPREVIEW_BUNDLE_DIR}"
          DESTINATION "."
          USE_SOURCE_PERMISSIONS)
else()
  message(WARNING "The probe is currently packaged and documented for macOS only")
  install(TARGETS WIPReviewProbe LIBRARY DESTINATION WIPReviewProbe.ofx.bundle/Contents/Linux-x86-64)
endif()

if(WIPREVIEW_BUILD_TESTS)
  enable_testing()
  add_executable(wipreview_probe_tests tests/probe_core_tests.cpp)
  target_link_libraries(wipreview_probe_tests PRIVATE
    wipreview_text_rasterizer
  )
  add_test(NAME wipreview_probe_core COMMAND wipreview_probe_tests)

  add_executable(wipreview_v1_acceptance_tests tests/v1_acceptance_tests.cpp)
  target_link_libraries(wipreview_v1_acceptance_tests PRIVATE
    wipreview_probe_core
  )
  add_test(NAME wipreview_v1_acceptance COMMAND wipreview_v1_acceptance_tests)

  add_executable(wipreview_color_pipeline_tests tests/color_pipeline_tests.cpp)
  target_link_libraries(wipreview_color_pipeline_tests PRIVATE
    wipreview_probe_core
  )
  add_test(NAME wipreview_color_pipeline COMMAND wipreview_color_pipeline_tests)

  add_executable(wipreview_bundle_smoke tests/bundle_smoke.cpp)
  target_link_libraries(wipreview_bundle_smoke PRIVATE ${CMAKE_DL_LIBS})
  target_include_directories(wipreview_bundle_smoke SYSTEM PRIVATE
    "${openfx_sdk_SOURCE_DIR}/include")
  add_test(NAME wipreview_bundle_exports
    COMMAND wipreview_bundle_smoke "$<TARGET_FILE:WIPReviewProbe>")

  if(APPLE)
    add_custom_target(fusion_host_smoke
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_fusion_smoke.sh"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMENT "Running cumulative Fusion Standalone V1 OFX host smoke test"
      USES_TERMINAL
    )
  endif()
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "WIPReviewProbe")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "WIPReviewProbe-${PROJECT_VERSION}-macOS-universal")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
