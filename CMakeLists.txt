# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Dawid Zalewski
#
# pico_sens: a DHT22 (AM2302) temperature and humidity sensor library for the
# RP2040.
#
# Use it from your project's CMakeLists.txt, after pico_sdk_init():
#
#     add_subdirectory(pico_sens)
#     target_link_libraries(your_program pico_stdlib pico_sens)
#
# and in your C file: #include "pico_sens/dht22.h"
#
# Like every Pico SDK library (pico_stdlib, hardware_pwm, ...), pico_sens is an
# INTERFACE library: it is not compiled on its own. Instead, its sources,
# include directories and dependencies are added to every program that links
# it, so it is compiled with exactly the same settings as that program
# (see "Every Library is an INTERFACE Library" in the Pico C/C++ SDK book).

if (NOT TARGET hardware_pio)
    message(FATAL_ERROR "pico_sens: put add_subdirectory(pico_sens) after pico_sdk_init()")
endif()
if (PICO_SDK_VERSION_STRING VERSION_LESS 2.0.0)
    message(FATAL_ERROR "pico_sens: needs Pico SDK 2.0.0 or newer, found ${PICO_SDK_VERSION_STRING}")
endif()

add_library(pico_sens INTERFACE)

target_sources(pico_sens INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/dht22.c
)

# Programs that link pico_sens can #include "pico_sens/dht22.h": the header is
# in include/pico_sens/, so the library's name is part of the #include, like
# "pico/stdlib.h" or "hardware/pio.h" in the SDK.
target_include_directories(pico_sens INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Assemble the PIO program into dht22.pio.h during the build
pico_generate_pio_header(pico_sens ${CMAKE_CURRENT_LIST_DIR}/src/dht22.pio)

# What pico_sens itself uses from the SDK
target_link_libraries(pico_sens INTERFACE
    hardware_clocks
    hardware_gpio
    hardware_pio
    pico_time
)
