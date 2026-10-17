# Shared opt-in backend targets for the full build and standalone CPU checks.
get_filename_component(KYTY_DLSS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(KYTY_DLSS_SDK_ROOT "" CACHE PATH "Prepared NVIDIA DLSS SDK for local NGX prototype checks")

add_library(kyty_dlss_backend OBJECT EXCLUDE_FROM_ALL
    "${KYTY_DLSS_ROOT}/src/graphics/presentation/frameGeneration/dlssBackend.cpp")
target_include_directories(kyty_dlss_backend PUBLIC
    "${KYTY_DLSS_ROOT}/src" "${KYTY_DLSS_ROOT}/3rdparty/Vulkan-Headers/include")
target_compile_features(kyty_dlss_backend PUBLIC cxx_std_20)
add_executable(dlss_backend_tests EXCLUDE_FROM_ALL "${KYTY_DLSS_ROOT}/tests/DlssBackendTests.cpp")
target_link_libraries(dlss_backend_tests PRIVATE kyty_dlss_backend)
add_test(NAME dlss_backend COMMAND dlss_backend_tests)
set_tests_properties(dlss_backend PROPERTIES TIMEOUT 30)

if(KYTY_DLSS_SDK_ROOT)
    if(NOT WIN32 OR NOT EXISTS "${KYTY_DLSS_SDK_ROOT}/include/nvsdk_ngx_helpers_vk.h")
        message(FATAL_ERROR "NGX prototype checks require Windows and tools/prepare-dlss.py headers")
    endif()
    add_library(kyty_dlss_ngx_objects OBJECT EXCLUDE_FROM_ALL
        "${KYTY_DLSS_ROOT}/src/graphics/presentation/frameGeneration/dlssNgx.cpp")
    target_link_libraries(kyty_dlss_ngx_objects PRIVATE kyty_dlss_backend)
    target_include_directories(kyty_dlss_ngx_objects PRIVATE "${KYTY_DLSS_SDK_ROOT}/include")

    # Stub NGX symbols exercise the real adapter's marshaling and cleanup. This
    # target neither links the NVIDIA library nor loads an SDK DLL/GPU driver.
    add_executable(dlss_ngx_tests EXCLUDE_FROM_ALL "${KYTY_DLSS_ROOT}/tests/DlssNgxTests.cpp")
    target_link_libraries(dlss_ngx_tests PRIVATE kyty_dlss_backend kyty_dlss_ngx_objects)
    target_include_directories(dlss_ngx_tests PRIVATE "${KYTY_DLSS_SDK_ROOT}/include")
    add_test(NAME dlss_ngx_mapping COMMAND dlss_ngx_tests)
    set_tests_properties(dlss_ngx_mapping PROPERTIES TIMEOUT 30)

    # NVIDIA's Windows import/static library uses the MSVC ABI. Zig's standalone
    # MinGW tests still compile and test the adapter through the stubs above.
    if(MSVC)
        set(KYTY_DLSS_NGX_LIBRARY "${KYTY_DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
        if(NOT EXISTS "${KYTY_DLSS_NGX_LIBRARY}")
            message(FATAL_ERROR "Run tools/prepare-dlss.py for the pinned x64 NGX library")
        endif()
        add_executable(dlss_ngx_link_check EXCLUDE_FROM_ALL "${KYTY_DLSS_ROOT}/tests/DlssNgxLinkCheck.cpp")
        target_link_libraries(dlss_ngx_link_check PRIVATE kyty_dlss_backend kyty_dlss_ngx_objects
            "${KYTY_DLSS_NGX_LIBRARY}" version shlwapi)
        add_test(NAME dlss_ngx_link_no_gpu COMMAND dlss_ngx_link_check)
        set_tests_properties(dlss_ngx_link_no_gpu PROPERTIES TIMEOUT 30)

        # Hardware gate, not a CTest: real NGX on an NVIDIA GPU with NVIDIA's signed feature
        # runtime, synthetic frames and readback. Without both it exits 77 before any NGX call.
        add_executable(dlss_vulkan_probe EXCLUDE_FROM_ALL "${KYTY_DLSS_ROOT}/tests/DlssVulkanProbe.cpp")
        target_link_libraries(dlss_vulkan_probe PRIVATE kyty_dlss_backend kyty_dlss_ngx_objects
            "${KYTY_DLSS_NGX_LIBRARY}" version shlwapi bcrypt wintrust crypt32)
        target_include_directories(dlss_vulkan_probe PRIVATE "${KYTY_DLSS_SDK_ROOT}/include")
    endif()
endif()
