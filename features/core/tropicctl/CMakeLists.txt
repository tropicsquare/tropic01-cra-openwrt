cmake_minimum_required(VERSION 3.16)
project(tropicctl VERSION 0.1.0 DESCRIPTION "Command-line tool for the TROPIC01 secure element" LANGUAGES C)

# tropicctl is self-contained: nothing here refers to files outside this directory except the libtropic source tree,
# which is passed in (-DLIBTROPIC_SRC=...) when building for a real chip.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(TROPICCTL_CHIP "libtropic" CACHE STRING "chip implementation: libtropic (real chip) or fake (tests, no chip)")
set_property(CACHE TROPICCTL_CHIP PROPERTY STRINGS libtropic fake)
set(TROPICCTL_SPI_DEV "/dev/spidev2.0" CACHE STRING "default SPI device")
set(TROPICCTL_GPIO_DEV "/dev/gpiochip0" CACHE STRING "default GPIO chip")
set(TROPICCTL_CS_PIN "25" CACHE STRING "default chip-select GPIO line")
set(TROPICCTL_SPI_SPEED "1000000" CACHE STRING "default SPI speed in Hz")
set(LIBTROPIC_SRC "" CACHE PATH "libtropic source tree (needed for TROPICCTL_CHIP=libtropic)")

find_package(OpenSSL REQUIRED) # host-side cryptography (keyed hash, authenticated encryption); both variants

set(SRC
    src/main.c src/cmd_table.c src/legacy.c src/ctx.c src/keys.c src/lock.c src/output.c src/errors.c src/args.c
    src/files.c src/cmd_info.c src/cmd_rng.c src/cmd_mem.c src/cmd_pairing.c src/cmd_config.c src/cmd_session.c
    src/cmd_help.c src/crypto.c src/uap.c src/safety.c src/mnd.c src/pin.c src/cfgfile.c src/cmd_ecc.c src/cmd_mnd.c src/cmd_counter.c src/cmd_fw.c)

if(TROPICCTL_CHIP STREQUAL "fake")
    add_executable(tropicctl ${SRC} src/chip_fake.c src/ecc_host.c)
    target_link_libraries(tropicctl PRIVATE OpenSSL::Crypto)
else()
    if(NOT LIBTROPIC_SRC OR NOT EXISTS "${LIBTROPIC_SRC}/CMakeLists.txt")
        message(FATAL_ERROR "Set -DLIBTROPIC_SRC to a libtropic source tree, or use -DTROPICCTL_CHIP=fake")
    endif()
    set(LT_HELPERS ON CACHE BOOL "" FORCE)
    set(LT_LOG_LVL "None" CACHE STRING "" FORCE)
    add_subdirectory(${LIBTROPIC_SRC} libtropic)
    add_subdirectory(${LIBTROPIC_SRC}/cal/openssl cal_openssl)
    target_sources(tropic PRIVATE ${LT_CAL_SRCS})
    target_include_directories(tropic PUBLIC ${LT_CAL_INC_DIRS})
    target_link_libraries(tropic PUBLIC OpenSSL::Crypto)
    add_subdirectory(${LIBTROPIC_SRC}/hal/linux/spi linux_spi_hal)
    target_sources(tropic PRIVATE ${LT_HAL_SRCS})
    target_include_directories(tropic PUBLIC ${LT_HAL_INC_DIRS})
    add_executable(tropicctl ${SRC} src/chip_libtropic.c)
    target_link_libraries(tropicctl PRIVATE tropic OpenSSL::Crypto)
endif()

target_compile_options(tropicctl PRIVATE -Wall -Wextra)
target_compile_definitions(tropicctl PRIVATE
    TROPICCTL_VERSION="${PROJECT_VERSION}"
    TROPICCTL_SPI_DEV="${TROPICCTL_SPI_DEV}" TROPICCTL_GPIO_DEV="${TROPICCTL_GPIO_DEV}"
    TROPICCTL_CS_PIN=${TROPICCTL_CS_PIN} TROPICCTL_SPI_SPEED=${TROPICCTL_SPI_SPEED})
if(TROPICCTL_CHIP STREQUAL "fake")
    target_compile_definitions(tropicctl PRIVATE TROPICCTL_FAKE=1)
endif()

install(TARGETS tropicctl RUNTIME DESTINATION bin)
