# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(input_common STATIC
    drivers/camera.cpp
    drivers/camera.h
    drivers/keyboard.cpp
    drivers/keyboard.h
    drivers/mouse.cpp
    drivers/mouse.h
    drivers/tas_input.cpp
    drivers/tas_input.h
    drivers/touch_screen.cpp
    drivers/touch_screen.h
    drivers/udp_client.cpp
    drivers/udp_client.h
    drivers/virtual_amiibo.cpp
    drivers/virtual_amiibo.h
    drivers/virtual_gamepad.cpp
    drivers/virtual_gamepad.h
    helpers/stick_from_buttons.cpp
    helpers/stick_from_buttons.h
    helpers/touch_from_buttons.cpp
    helpers/touch_from_buttons.h
    helpers/udp_protocol.cpp
    helpers/udp_protocol.h
    input_engine.cpp
    input_engine.h
    input_mapping.cpp
    input_mapping.h
    input_poller.cpp
    input_poller.h
    main.cpp
    main.h
)

if (MSVC)
    target_compile_options(input_common PRIVATE
        /we4242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
        /we4254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    )
else()
    target_compile_options(input_common PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-Wconversion>)
endif()

if (ANDROID)
    target_sources(input_common PRIVATE
        drivers/android.cpp
        drivers/android.h)

    target_link_libraries(input_common PRIVATE android)
else()
    target_sources(input_common PRIVATE
        drivers/joycon.cpp
        drivers/joycon.h
        drivers/renhdrum_pad.cpp
        drivers/renhdrum_pad.h
        drivers/sdl_driver.cpp
        drivers/sdl_driver.h
        helpers/joycon_driver.cpp
        helpers/joycon_driver.h
        helpers/joycon_protocol/calibration.cpp
        helpers/joycon_protocol/calibration.h
        helpers/joycon_protocol/common_protocol.cpp
        helpers/joycon_protocol/common_protocol.h
        helpers/joycon_protocol/generic_functions.cpp
        helpers/joycon_protocol/generic_functions.h
        helpers/joycon_protocol/joycon_types.h
        helpers/joycon_protocol/irs.cpp
        helpers/joycon_protocol/irs.h
        helpers/joycon_protocol/nfc.cpp
        helpers/joycon_protocol/nfc.h
        helpers/joycon_protocol/poller.cpp
        helpers/joycon_protocol/poller.h
        helpers/joycon_protocol/ringcon.cpp
        helpers/joycon_protocol/ringcon.h
        helpers/joycon_protocol/rumble.cpp
        helpers/joycon_protocol/rumble.h)

    target_link_libraries(input_common PRIVATE SDL3::SDL3)
    target_compile_definitions(input_common PRIVATE HAVE_SDL3)
endif()

if (ENABLE_LIBUSB)
    target_sources(input_common PRIVATE
        drivers/gc_adapter.cpp
        drivers/gc_adapter.h)

    target_link_libraries(input_common PRIVATE libusb::usb)
    target_compile_definitions(input_common PRIVATE ENABLE_LIBUSB)
endif()

create_target_directory_groups(input_common)
target_link_libraries(input_common PUBLIC hid_core PRIVATE common Boost::headers)

if (ENABLE_RENHDRUM AND NOT ANDROID)
    target_compile_definitions(input_common PRIVATE ENABLE_RENHDRUM)
    target_include_directories(input_common PRIVATE ${PROJECT_SOURCE_DIR}/externals/renhdrum)
    if (RENHDRUM_SDL_PATCHES)
        target_compile_definitions(input_common PRIVATE RENHDRUM_SDL_HDRUMBLE)
    endif()
    if (RENHDRUM_LIBRARY_PATH)
        # A generated header rather than -DRENHDRUM_LIBRARY_PATH="...": CMake mangles the quotes
        # of a compile definition that carries a path, and the loader then looks for a truncated
        # filename. Fail the build here if the packager named something that is not there.
        if (NOT EXISTS "${RENHDRUM_LIBRARY_PATH}")
            message(FATAL_ERROR
                "RENHDRUM_LIBRARY_PATH does not exist: ${RENHDRUM_LIBRARY_PATH}")
        endif()
        # The path lands inside a C++ string literal: escape what a literal cannot carry raw.
        string(REPLACE "\\" "\\\\" RENHDRUM_LIBRARY_PATH_ESCAPED "${RENHDRUM_LIBRARY_PATH}")
        string(REPLACE "\"" "\\\"" RENHDRUM_LIBRARY_PATH_ESCAPED "${RENHDRUM_LIBRARY_PATH_ESCAPED}")
        string(REPLACE "\r" "" RENHDRUM_LIBRARY_PATH_ESCAPED "${RENHDRUM_LIBRARY_PATH_ESCAPED}")
        string(REPLACE "\n" "" RENHDRUM_LIBRARY_PATH_ESCAPED "${RENHDRUM_LIBRARY_PATH_ESCAPED}")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/renhdrum_library_path.h.in"
            "${CMAKE_CURRENT_BINARY_DIR}/renhdrum_library_path.h"
            @ONLY)
        target_include_directories(input_common PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(input_common PRIVATE RENHDRUM_HAVE_PACKAGED_PATH)
    endif()
endif()
