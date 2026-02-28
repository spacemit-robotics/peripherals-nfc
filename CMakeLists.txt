cmake_minimum_required(VERSION 3.10)
project(nfc LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers
)

# Core source files
set(NFC_CORE_SOURCES
    src/nfc_core.c
)

set(NFC_DRIVER_SOURCES "")
foreach(_drv IN LISTS SROBOTIS_PERIPHERALS_NFC_ENABLED_DRIVERS)
    # Support both flat drivers (src/drivers/<name>.c) and directory drivers (src/drivers/<name>/<name>.c).
    # This lets configs use "drv_i2c_SI512" instead of "drv_i2c_SI512/drv_i2c_SI512".
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/${_drv}.c")
        list(APPEND NFC_DRIVER_SOURCES "src/drivers/${_drv}.c")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/${_drv}/${_drv}.c")
        list(APPEND NFC_DRIVER_SOURCES "src/drivers/${_drv}/${_drv}.c")
    else()
        message(FATAL_ERROR
            "nfc: enabled driver '${_drv}' not found. Tried:\n"
            "  - src/drivers/${_drv}.c\n"
            "  - src/drivers/${_drv}/${_drv}.c"
        )
    endif()
endforeach()

# All source files
set(NFC_SOURCES
    ${NFC_CORE_SOURCES}
    ${NFC_DRIVER_SOURCES}
)

# Create shared library (drivers auto-register via constructor)
add_library(nfc SHARED ${NFC_SOURCES})

# Set compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nfc PRIVATE
        -Wall
        -Wextra
    )
endif()

# Install rules
install(TARGETS nfc
    EXPORT ${SROBOTIS_EXPORT_SET}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES include/nfc.h
    DESTINATION include
)

# Optional: Create test executables
option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    add_executable(test_nfc_i2c test/test_nfc_i2c.c)
    target_link_libraries(test_nfc_i2c nfc)
    set_target_properties(test_nfc_i2c PROPERTIES BUILD_RPATH "$ORIGIN/../lib")
    install(TARGETS test_nfc_i2c RUNTIME DESTINATION bin)

    # Tests should rely on the normal driver selection mechanism:
    # enable drivers via SROBOTIS_PERIPHERALS_NFC_ENABLED_DRIVERS.
    add_executable(test_nfc_spi test/test_nfc_spi.c)
    target_link_libraries(test_nfc_spi nfc)
    set_target_properties(test_nfc_spi PROPERTIES BUILD_RPATH "$ORIGIN/../lib")
    install(TARGETS test_nfc_spi RUNTIME DESTINATION bin)

    add_executable(test_nfc_uart test/test_nfc_uart.c)
    target_link_libraries(test_nfc_uart nfc)
    set_target_properties(test_nfc_uart PROPERTIES BUILD_RPATH "$ORIGIN/../lib")
    install(TARGETS test_nfc_uart RUNTIME DESTINATION bin)
endif()

