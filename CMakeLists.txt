cmake_minimum_required(VERSION 3.16)
project(ScanJet4200C VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HP4200_USB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(HP4200_PNG REQUIRED IMPORTED_TARGET libpng)
option(HP4200_BUILD_GUI "Build the GTK scanning application" ON)
if(HP4200_BUILD_GUI)
    pkg_check_modules(HP4200_GTK REQUIRED IMPORTED_TARGET gtk4)
endif()

add_library(hp4200 STATIC
    driver/hp4200_bridge.c
    driver/hp4200_brightness.c
    driver/hp4200_calibration_setup.c
    driver/hp4200_engine.c
    driver/hp4200_final_calibration.c
    driver/hp4200_final_capture.c
    driver/hp4200_gain.c
    driver/hp4200_gamma.c
    driver/hp4200_home.c
    driver/hp4200_initialize.c
    driver/hp4200_io.c
    driver/hp4200_motor.c
    driver/hp4200_offset.c
    driver/hp4200_origin.c
    driver/hp4200_origin_scan.c
    driver/hp4200_pipeline.c
    driver/hp4200_pixels.c
    driver/hp4200_plan.c
    driver/hp4200_ram.c
    driver/hp4200_rate.c
    driver/hp4200_registers.c
    driver/hp4200_rows.c
    driver/hp4200_session.c
    driver/hp4200_square_pixels.c
    driver/hp4200_usb.c)
target_include_directories(hp4200 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/driver")
target_link_libraries(hp4200 PUBLIC PkgConfig::HP4200_USB)

add_executable(native_scan driver/native_scan.c driver/png_writer.c)
target_link_libraries(native_scan PRIVATE hp4200 PkgConfig::HP4200_PNG)
add_executable(raw_replay driver/raw_replay.c driver/png_writer.c)
target_link_libraries(raw_replay PRIVATE hp4200 PkgConfig::HP4200_PNG)
add_executable(probe driver/probe.c)
target_link_libraries(probe PRIVATE hp4200)

# The GTK app resolves native_scan beside itself in an installed layout.
set_target_properties(native_scan raw_replay probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(HP4200_BUILD_GUI)
    add_executable(scanjet4200c scanjet4200c.c driver/image_dpi.c)
    target_link_libraries(scanjet4200c PRIVATE PkgConfig::HP4200_GTK)
    if(NOT APPLE)
        target_link_libraries(scanjet4200c PRIVATE m)
    endif()
    set_target_properties(scanjet4200c PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(APPLE AND HP4200_BUILD_GUI)
    set_target_properties(scanjet4200c PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.scanjet4200c.Scanner"
        MACOSX_BUNDLE_BUNDLE_NAME "ScanJet 4200C"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    add_dependencies(scanjet4200c native_scan)
    add_custom_command(TARGET scanjet4200c POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:native_scan>"
            "$<TARGET_FILE_DIR:scanjet4200c>/native_scan"
        VERBATIM)
    install(TARGETS scanjet4200c BUNDLE DESTINATION .)
    install(TARGETS native_scan raw_replay probe RUNTIME DESTINATION bin)
elseif(HP4200_BUILD_GUI)
    install(TARGETS native_scan raw_replay probe scanjet4200c RUNTIME DESTINATION bin)
else()
    install(TARGETS native_scan raw_replay probe RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    file(GLOB HP4200_TEST_SOURCES CONFIGURE_DEPENDS "driver/test_*.c")
    foreach(test_source IN LISTS HP4200_TEST_SOURCES)
        get_filename_component(test_name "${test_source}" NAME_WE)
        add_executable("${test_name}" "${test_source}")
        target_compile_options("${test_name}" PRIVATE -UNDEBUG)
        target_link_libraries("${test_name}" PRIVATE hp4200)
        if(test_name STREQUAL "test_png_writer")
            target_sources("${test_name}" PRIVATE driver/png_writer.c)
            target_link_libraries("${test_name}" PRIVATE PkgConfig::HP4200_PNG)
        elseif(test_name STREQUAL "test_image_dpi")
            target_sources("${test_name}" PRIVATE driver/image_dpi.c)
        endif()
        add_test(NAME "${test_name}" COMMAND "${test_name}")
    endforeach()
endif()
