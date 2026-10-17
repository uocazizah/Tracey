# TraceyKconfig.cmake
#
# Kconfiglib integration for the Tracey bootstrap compiler. This module:
#   * verifies Python3 + kconfiglib are available,
#   * materialises a default `.config` if one does not exist yet,
#   * exposes every Kconfig symbol to CMake as `TRACEY_CFG_<NAME>` by
#     generating and including `kconfig_vars.cmake` at configure time,
#   * generates `kconfig_autoconf.h` at build time for the C++ sources,
#   * provides the `menuconfig` / `olddefconfig` convenience targets.
#
# The Kconfig file is the single source of truth for feature toggles: CMake
# reads the resolved values instead of declaring its own duplicate options.
#
# NOTE: this module relies on `CMAKE_CURRENT_SOURCE_DIR` pointing at the
# bootstrap root (it is meant to be `include()`d from bootstrap/CMakeLists.txt
# only).

find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import kconfiglib"
    RESULT_VARIABLE _TRACEY_KCONFIGLIB_MISSING
    OUTPUT_QUIET ERROR_QUIET
)
if(_TRACEY_KCONFIGLIB_MISSING)
    message(FATAL_ERROR
        "Kconfiglib not found for Python3 (${Python3_EXECUTABLE}).\n"
        "Install it with: ${Python3_EXECUTABLE} -m pip install kconfiglib")
endif()

set(TRACEY_KCONFIG_FILE   "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig")
set(TRACEY_DOT_CONFIG     "${CMAKE_CURRENT_SOURCE_DIR}/.config")
set(TRACEY_KCONFIG_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/kconfig_tool.py")
set(TRACEY_KCONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kconfig_autoconf.h")
set(TRACEY_KCONFIG_VARS   "${CMAKE_CURRENT_BINARY_DIR}/kconfig_vars.cmake")

# Re-run CMake automatically whenever the Kconfig inputs change, so the
# TRACEY_CFG_* values and the generated header stay in sync with `.config`.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TRACEY_DOT_CONFIG}"
    "${TRACEY_KCONFIG_FILE}"
)

# Create a default `.config` if none exists yet (olddefconfig fills in the
# defaults for any missing symbol).
if(NOT EXISTS "${TRACEY_DOT_CONFIG}")
    message(STATUS "Kconfig: .config not found, generating defaults")
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${TRACEY_KCONFIG_SCRIPT} olddefconfig
                --kconfig ${TRACEY_KCONFIG_FILE}
                --config  ${TRACEY_DOT_CONFIG}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE _TRACEY_KCONFIG_INIT_RESULT
    )
    if(NOT _TRACEY_KCONFIG_INIT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to initialise .config")
    endif()
endif()

# Resolve every Kconfig symbol into `TRACEY_CFG_<NAME>` CMake variables and
# include the result. This runs at configure time so the values can drive
# conditional logic below (e.g. optional dependency lookup).
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${TRACEY_KCONFIG_SCRIPT} gen-cmake
            --kconfig ${TRACEY_KCONFIG_FILE}
            --config  ${TRACEY_DOT_CONFIG}
            --cmake   ${TRACEY_KCONFIG_VARS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE _TRACEY_KCONFIG_CMAKE_RESULT
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT _TRACEY_KCONFIG_CMAKE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate kconfig_vars.cmake from .config")
endif()
include("${TRACEY_KCONFIG_VARS}")

# Build target: `cmake --build <dir> --target menuconfig`
add_custom_target(menuconfig
    COMMAND ${Python3_EXECUTABLE} ${TRACEY_KCONFIG_SCRIPT} menuconfig
            --kconfig ${TRACEY_KCONFIG_FILE}
            --config  ${TRACEY_DOT_CONFIG}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running menuconfig..."
    USES_TERMINAL
    VERBATIM
)

# Build target: normalise `.config` against the defaults (olddefconfig).
add_custom_target(olddefconfig
    COMMAND ${Python3_EXECUTABLE} ${TRACEY_KCONFIG_SCRIPT} olddefconfig
            --kconfig ${TRACEY_KCONFIG_FILE}
            --config  ${TRACEY_DOT_CONFIG}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Normalising .config..."
    USES_TERMINAL
    VERBATIM
)

# Generate `kconfig_autoconf.h` from `.config` at build time. The C++ sources
# include this header (transitively, via config.h) to read CONFIG_* values.
add_custom_command(
    OUTPUT  ${TRACEY_KCONFIG_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${TRACEY_KCONFIG_SCRIPT} gen-header
            --kconfig ${TRACEY_KCONFIG_FILE}
            --config  ${TRACEY_DOT_CONFIG}
            --header  ${TRACEY_KCONFIG_HEADER}
    DEPENDS ${TRACEY_KCONFIG_FILE}
            ${TRACEY_KCONFIG_SCRIPT}
            ${TRACEY_DOT_CONFIG}
    COMMENT "Generating kconfig_autoconf.h from .config"
    VERBATIM
)

add_custom_target(tracey_kconfig_header DEPENDS ${TRACEY_KCONFIG_HEADER})
