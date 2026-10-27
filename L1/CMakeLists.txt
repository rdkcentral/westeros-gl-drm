cmake_minimum_required(VERSION 3.14)
project(westeros_gl_drm_l1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_COVERAGE "Instrument the measured Westeros GL DRM source scope" OFF)

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
include(CTest)

set(WESTEROS_GL_DRM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(WESTEROS_GL_DRM_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/westeros-gl")

add_executable(test_westeros_gl_drm_l1
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/L1/westeros_gl_console_helper_test.cpp"
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks/display_server_mock.cpp"
    "${WESTEROS_GL_DRM_ROOT}/westeros-gl/westeros-gl-console-helper.c")

target_include_directories(test_westeros_gl_drm_l1 PRIVATE
    "${WESTEROS_GL_DRM_ROOT}/westeros-gl"
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks")
target_compile_definitions(test_westeros_gl_drm_l1 PRIVATE _GNU_SOURCE UNIT_TEST)
target_link_libraries(test_westeros_gl_drm_l1 PRIVATE GTest::gtest_main Threads::Threads)

# The C harness includes the original renderer to access opaque state/static
# helpers without modifying production sources or compiling them as C++.
add_executable(test_westeros_gl_renderer_l1
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/L1/westeros_gl_renderer_test.cpp"
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks/renderer_test_support.c")
set_target_properties(test_westeros_gl_renderer_l1 PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_include_directories(test_westeros_gl_renderer_l1 PRIVATE
    "${WESTEROS_GL_DRM_ROOT}/westeros-gl"
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks"
    "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks/compat")
target_compile_definitions(test_westeros_gl_renderer_l1 PRIVATE
    _GNU_SOURCE UNIT_TEST NO_GENERIC_AVSYNC
    DRM_NO_UEVENT_HOTPLUG DRM_NO_REFRESH_LOCK DRM_NO_OUT_FENCE DRM_NO_NATIVE_FENCE)
target_link_libraries(test_westeros_gl_renderer_l1 PRIVATE
    GTest::gtest_main Threads::Threads PkgConfig::DRM PkgConfig::GBM
    PkgConfig::EGL ${CMAKE_DL_LIBS})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_westeros_gl_drm_l1 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC or Clang gcov-compatible coverage flags")
    endif()
    target_compile_options(test_westeros_gl_drm_l1 PRIVATE --coverage -O0 -g)
    target_link_options(test_westeros_gl_drm_l1 PRIVATE --coverage)
    # Instrument the C harness, including every compiled renderer function.
    # The renderer's own filename/lines are retained by gcov; no code is copied.
    set_source_files_properties(
        "${WESTEROS_GL_DRM_TEST_ROOT}/tests/mocks/renderer_test_support.c"
        PROPERTIES COMPILE_OPTIONS "--coverage;-O0;-g")
    target_link_options(test_westeros_gl_renderer_l1 PRIVATE --coverage)

    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)
    if(NOT LCOV_EXECUTABLE OR NOT GENHTML_EXECUTABLE)
        message(FATAL_ERROR "ENABLE_COVERAGE requires lcov and genhtml")
    endif()
    set(WESTEROS_GL_DRM_COVERAGE_INFO "${CMAKE_BINARY_DIR}/coverage_westeros_gl_drm.info")
    set(WESTEROS_GL_DRM_COVERAGE_HTML "${CMAKE_BINARY_DIR}/coverage_html_westeros_gl_drm")

    add_custom_target(coverage-westeros-gl-drm
        COMMAND "${LCOV_EXECUTABLE}" --directory "${CMAKE_BINARY_DIR}" --zerocounters
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -L WesterosGlDrm
        # The measured source lives outside the build directory. Use the explicit
        # include filter rather than --no-external, which would discard it.
        COMMAND "${LCOV_EXECUTABLE}" --directory "${CMAKE_BINARY_DIR}"
            --base-directory "${WESTEROS_GL_DRM_ROOT}"
            --capture
            --include "*/westeros-gl/westeros-gl-console-helper.c"
            --include "*/westeros-gl/westeros-gl.c"
            --output-file "${WESTEROS_GL_DRM_COVERAGE_INFO}"
            --rc branch_coverage=1
        COMMAND "${GENHTML_EXECUTABLE}" "${WESTEROS_GL_DRM_COVERAGE_INFO}"
            --output-directory "${WESTEROS_GL_DRM_COVERAGE_HTML}"
            --title "Westeros GL DRM L1 Coverage"
            --legend --show-details --branch-coverage
        COMMAND "${LCOV_EXECUTABLE}" --list "${WESTEROS_GL_DRM_COVERAGE_INFO}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS test_westeros_gl_drm_l1 test_westeros_gl_renderer_l1
        VERBATIM
        COMMENT "Testing and reporting console helper and renderer coverage (no minimum)")
endif()

add_test(NAME WesterosGlDrmL1Tests COMMAND test_westeros_gl_drm_l1)
add_test(NAME WesterosGlRendererL1Tests COMMAND test_westeros_gl_renderer_l1)
set_tests_properties(WesterosGlDrmL1Tests WesterosGlRendererL1Tests PROPERTIES
    LABELS "L1;WesterosGlDrm;Component"
    TIMEOUT 60)