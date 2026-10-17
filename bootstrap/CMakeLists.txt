cmake_minimum_required(VERSION 3.15...3.30)

project(tracey
    VERSION 1.0.0
    DESCRIPTION "Tracey compiler"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Project layout ---------------------------------------------------------
set(TRACEY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# --- Build-system modules ---------------------------------------------------
# Order matters: TraceyKconfig resolves the Kconfig symbols (TRACEY_CFG_*) that
# TraceyArch and TraceyDependencies consume.
include(cmake/TraceyKconfig.cmake)
include(cmake/TraceyArch.cmake)
include(cmake/TraceyWarnings.cmake)
include(cmake/TraceySanitizers.cmake)
include(cmake/TraceyDependencies.cmake)

# --- Generated build configuration ------------------------------------------
# config.h (CMake metadata: version) is produced at configure time;
# kconfig_autoconf.h (Kconfig feature toggles) is produced at build time by
# the tracey_kconfig_header target. Both are made available to every module
# through the tracey_config INTERFACE target.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

add_library(tracey_config INTERFACE)
target_include_directories(tracey_config INTERFACE
    "${TRACEY_INCLUDE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_features(tracey_config INTERFACE cxx_std_26)
target_link_libraries(tracey_config INTERFACE tracey_optional_deps)

# --- Compiler front-end modules ---------------------------------------------
add_subdirectory(general)

# --- traceyc executable -----------------------------------------------------
add_executable(traceyc index.cxx)

# Enforce the C++26 feature level explicitly (belt-and-braces beside
# CMAKE_CXX_STANDARD, so a stray override can't silently drop us to an older
# dialect).
target_compile_features(traceyc PRIVATE cxx_std_26)

# The generated kconfig_autoconf.h must exist before any source that includes
# <tracey/config.hxx> is compiled.
add_dependencies(traceyc tracey_kconfig_header)

target_link_libraries(traceyc PRIVATE
    tracey_source
    tracey_lexer
    tracey_argument
    tracey_config
    tracey_warnings
)

install(TARGETS traceyc RUNTIME DESTINATION bin)
