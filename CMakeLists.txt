# Generated Cmake Pico project file
# for "clean" use: cmake --build build --target clean from project directory

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
      FORCE)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picolo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-DPICO_RP2040)

# PicoDVI
# To change the default serial config for all apps, pass e.g.
#   cmake -DDVI_DEFAULT_SERIAL_CONFIG=pimoroni_demo_hdmi_cfg ..
# then rebuild. This is applied via target_compile_definitions on each app
# individually.
set(DVI_DEFAULT_SERIAL_CONFIG "pico_sock_cfg" CACHE STRING
"Select a default pin configuration from common_dvi_pin_configs.h")

#add_definitions(-DPICO_COPY_TO_RAM=1)
get_filename_component(PICO_FOLDER ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

# this is for the FatFS module
# Can leave these off for silent mode:
# add_compile_definitions(USE_PRINTF USE_DBG_PRINTF)
add_compile_definitions(USE_PRINTF)

# Add executable. Default name is the project name, version 0.1
add_executable(picolo
    main.c 
    zxe_driver.c
    zxe_driver.h
    platform.c
    platform.h
    besciifont.h
    display.c
    display.h
    conio.c
    conio.h
    #tusb_config.h
    #hid_app.c
    cstream.c
    cstream.h
    shell.c
    shell.h
    sdcard.c
    sdcard.h
    ed.c
    ed_edit.c
    ed_buffer.c
    ed_cursor.c
    ed_status.c
    ed_copypaste.c
    ed.h
    zxe.pio.h
    )

pico_set_program_name(picolo "picolo")
pico_set_program_version(picolo "1.0")

# Create C header file with the name <pio program>.pio.h
pico_generate_pio_header(${PROJECT_NAME}  
        ${CMAKE_CURRENT_LIST_DIR}/zxe.pio
)

# Modify the below lines to enable/disable output over UART/USB
# disable UART: we need every pin we can get
pico_enable_stdio_uart(picolo 1)
pico_enable_stdio_usb(picolo 0)

add_subdirectory(liblua)
add_subdirectory(${PICO_FOLDER}/PicoDVI/software/libdvi "${CMAKE_CURRENT_BINARY_DIR}/libdvi")
add_subdirectory(${PICO_FOLDER}/PicoDVI/software/libsprite "${CMAKE_CURRENT_BINARY_DIR}/libsprite")
add_subdirectory(${PICO_FOLDER}/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src "${CMAKE_CURRENT_BINARY_DIR}/FatFS")

target_compile_definitions(picolo PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    
    # tglib
    CONIO_DEBUG_PRINT=1
    # tinyusb
    #CFG_TUSB_MCU=OPT_MCU_RP2040

     # from colour terminal: not tested with graphics mode yet
	#PICO_CORE1_STACK_SIZE=0x200
    #DVI_VERTICAL_REPEAT=1  

	)

# Add all required libraries to the build
target_link_libraries(picolo
        pico_stdlib
        pico_multicore
        pico_util
        #tinyusb_host 
        #tinyusb_board       
        libdvi
        libsprite
        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
        #liblua
        )

# Add our include directories to the build
target_include_directories(picolo PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
  ${CMAKE_CURRENT_LIST_DIR}/liblua
  #${PICO_SDK_PATH}/lib/tinyusb/hw
  #${PICO_SDK_PATH}/lib/tinyusb/src
  #${PICO_SDK_PATH}/lib/tinyusb/src/class/hid
)

# PicoDVI include directories
include_directories(
	${PICO_FOLDER}/PicoDVI/software/assets
	${PICO_FOLDER}/PicoDVI/software/include
	${PICO_FOLDER}/PicoDVI/software/libsprite
	)


# make sure .uf2 gets created
pico_add_extra_outputs(picolo)

