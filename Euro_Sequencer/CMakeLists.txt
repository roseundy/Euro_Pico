# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/home/sru/pico/pico-sdk")

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(Euro_Sequencer C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Euro_Sequencer Euro_Sequencer.cpp )

pico_set_program_name(Euro_Sequencer "Euro_Sequencer")
pico_set_program_version(Euro_Sequencer "0.1")

# no_flash means the target is to run from RAM
#pico_set_binary_type(Euro_Sequencer no_flash)

pico_enable_stdio_uart(Euro_Sequencer 0)
pico_enable_stdio_usb(Euro_Sequencer 1)

# Add the standard library to the build
target_link_libraries(Euro_Sequencer
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Euro_Sequencer PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

add_subdirectory(pico-ssd1306)

# Add any user requested libraries
target_link_libraries(Euro_Sequencer
        pico_rand
        pico_multicore
        hardware_spi
        hardware_i2c
        hardware_timer
        hardware_clocks
        hardware_adc
        hardware_flash
        pico_ssd1306
        )

pico_add_extra_outputs(Euro_Sequencer)

