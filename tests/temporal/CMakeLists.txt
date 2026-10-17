cmake_minimum_required(VERSION 3.21...4.3)
project(KytyTemporalTests LANGUAGES CXX)

# Standalone CPU checks: no Qt, shader compiler, SDK DLL, GPU or game required.
# Keep this separate from the emulator's PGO build and delivery staging.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
enable_testing()
get_filename_component(KYTY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${KYTY_ROOT}/cmake/DlssPrototype.cmake")
# These targets are explicit-only in the full emulator build; include them in
# this small CPU developer build, including the optional SDK mapping checks.
add_custom_target(dlss_developer_checks ALL DEPENDS dlss_backend_tests)
if(TARGET dlss_ngx_tests)
    add_dependencies(dlss_developer_checks dlss_ngx_tests)
endif()
if(TARGET dlss_ngx_link_check)
    add_dependencies(dlss_developer_checks dlss_ngx_link_check)
endif()

foreach(test IN ITEMS TemporalInput TemporalCamera PresentationTiming SimulationClockProfile)
    add_executable(${test}Tests "${KYTY_ROOT}/tests/${test}Tests.cpp")
    target_include_directories(${test}Tests PRIVATE
        "${KYTY_ROOT}/src"
        "${KYTY_ROOT}/3rdparty/Vulkan-Headers/include"
        "${KYTY_ROOT}/3rdparty/nlohmann_json/single_include")
    if(MSVC)
        target_compile_options(${test}Tests PRIVATE /W4)
    else()
        target_compile_options(${test}Tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${test} COMMAND ${test}Tests)
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()

# Optional compile check only; loading the SDK and dispatching on a GPU belongs
# to the separate probe/home-machine tests. Nothing is downloaded by CMake.
set(KYTY_FSR3_SDK_ROOT "" CACHE PATH "Prepared FidelityFX 1.1.4 SDK package")
if(KYTY_FSR3_SDK_ROOT)
    if(NOT WIN32 OR NOT EXISTS "${KYTY_FSR3_SDK_ROOT}/ffx-api/include/ffx_api/ffx_api.h")
        message(FATAL_ERROR "FSR compile check needs Windows and the prepared 1.1.4 SDK headers")
    endif()
    add_library(kyty_fsr3_compile_check OBJECT
        "${KYTY_ROOT}/src/graphics/presentation/frameGeneration/fsr3Backend.cpp"
        "${KYTY_ROOT}/src/graphics/presentation/frameGeneration/fsrPresentation.cpp")
    target_include_directories(kyty_fsr3_compile_check PRIVATE
        "${KYTY_ROOT}/src" "${KYTY_ROOT}/3rdparty/Vulkan-Headers/include"
        "${KYTY_FSR3_SDK_ROOT}/ffx-api/include")
    add_executable(FsrBackendGuardTests "${KYTY_ROOT}/tests/FsrBackendGuardTests.cpp")
    target_include_directories(FsrBackendGuardTests PRIVATE
        "${KYTY_ROOT}/src" "${KYTY_ROOT}/3rdparty/Vulkan-Headers/include")
    target_link_libraries(FsrBackendGuardTests PRIVATE kyty_fsr3_compile_check)
    add_test(NAME FsrBackendGuards COMMAND FsrBackendGuardTests)
    set_tests_properties(FsrBackendGuards PROPERTIES TIMEOUT 30)
endif()
