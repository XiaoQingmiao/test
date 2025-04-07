# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Final_Proj C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Final_Proj)

add_compile_options(-Ofast -Wno-write-strings -ftree-vectorize -funsafe-math-optimizations -fno-trapping-math -ffast-math -fno-signed-zeros -fno-rounding-math -fno-finite-math-only -fassociative-math -march=native)

pico_generate_pio_header(Final_Proj ${CMAKE_CURRENT_LIST_DIR}/can.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Final_Proj 0)
pico_enable_stdio_usb(Final_Proj 1)

# must match with executable name and source file names
target_sources(Final_Proj PRIVATE Final_Proj.cpp can.cpp)

# Add the standard library to the build
target_link_libraries(Final_Proj PRIVATE pico_stdlib pico_divider pico_multicore pico_bootsel_via_double_reset hardware_pio hardware_dma hardware_irq hardware_clocks hardware_pll)

# Add the standard include files to the build
target_include_directories(Final_Proj PRIVATE ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Final_Proj)

