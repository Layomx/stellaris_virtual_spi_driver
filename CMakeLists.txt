cmake_minimum_required(VERSION 3.20)

project(stellaris_spi C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MCU_FLAGS
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
)

add_executable(${PROJECT_NAME}
    src/log.c
    src/main.c
    src/spi.c
    src/startup.c
    src/sd_demo.c
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
        ${MCU_FLAGS}
        -ffreestanding
        -fdata-sections
        -ffunction-sections
        -Wall
        -Wextra
        -Wpedantic
)

target_link_options(${PROJECT_NAME}
    PRIVATE
        ${MCU_FLAGS}
        -T${CMAKE_CURRENT_SOURCE_DIR}/ld/stellaris.ld
        -nostartfiles
        -nostdlib
        -nodefaultlibs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
)

target_link_libraries(${PROJECT_NAME} PRIVATE gcc)

set(OBJCOPY ${CMAKE_OBJCOPY})
if(NOT OBJCOPY)
    find_program(OBJCOPY arm-none-eabi-objcopy REQUIRED)
endif()

add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${OBJCOPY} -O binary
            $<TARGET_FILE:${PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Creating ${PROJECT_NAME}.bin"
)
