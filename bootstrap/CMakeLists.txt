cmake_minimum_required(VERSION 3.16)

project(tracey
    VERSION 1.0
    DESCRIPTION "Tracey compiler"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# Basic project setup
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Mirrors the autoconf default of `-g -O0` for debug builds.
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Emit compile_commands.json for clang-tidy / editors / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# ARCH option — replaces the AC_ARG_VAR([ARCH], ...) case in configure.ac
# -----------------------------------------------------------------------------

set(ARCH "native" CACHE STRING "Target architecture: native, i686, or x86_64")
set_property(CACHE ARCH PROPERTY STRINGS native i686 x86_64)

if(ARCH STREQUAL "i686")
    message(STATUS "Target architecture: i686")
    add_compile_options(-m32)
    add_link_options(-m32)
elseif(ARCH STREQUAL "x86_64")
    message(STATUS "Target architecture: x86_64")
    add_compile_options(-m64)
    add_link_options(-m64)
elseif(ARCH STREQUAL "native" OR ARCH STREQUAL "")
    message(STATUS "Target architecture: native")
else()
    message(FATAL_ERROR "unsupported architecture '${ARCH}'")
endif()

# -----------------------------------------------------------------------------
# Warning flags — more aggressive than the old -Wall -Wextra
# -----------------------------------------------------------------------------

add_library(tracey_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tracey_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wuseless-cast
    )

    # -Wduplicated-branches and -Wlogical-op are GCC-only; Clang does not
    # recognize them.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tracey_warnings INTERFACE
            -Wduplicated-branches
            -Wlogical-op
            -Wuseless-cast
        )
    endif()

    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    if(TRACEY_WARNINGS_AS_ERRORS)
        target_compile_options(tracey_warnings INTERFACE -Werror)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(tracey_warnings INTERFACE /W4)
    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    if(TRACEY_WARNINGS_AS_ERRORS)
        target_compile_options(tracey_warnings INTERFACE /WX)
    endif()
endif()

# -----------------------------------------------------------------------------
# Optional sanitizer / static-analysis toggles (handy alongside UBSan/ASan)
# -----------------------------------------------------------------------------

option(TRACEY_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TRACEY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(TRACEY_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(TRACEY_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# -----------------------------------------------------------------------------
# Optional dependencies
# -----------------------------------------------------------------------------
# Add real find_package() calls here as Tracey grows. Pattern shown below —
# every optional library sets a TRACEY_HAVE_<NAME> compile definition so the
# code can #ifdef around it, exactly like AC_CHECK_LIB / AC_CHECK_HEADERS did.

add_library(tracey_optional_deps INTERFACE)

# Example: optional zlib support (replace/extend with whatever Tracey needs)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    message(STATUS "zlib found — enabling optional zlib support")
    target_link_libraries(tracey_optional_deps INTERFACE ZLIB::ZLIB)
    target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_ZLIB=1)
else()
    message(STATUS "zlib not found — building without optional zlib support")
endif()

# Example: optional Threads support
find_package(Threads QUIET)
if(Threads_FOUND)
    message(STATUS "Threads found — enabling optional threading support")
    target_link_libraries(tracey_optional_deps INTERFACE Threads::Threads)
    target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_THREADS=1)
else()
    message(STATUS "Threads not found — building without optional threading support")
endif()

# -----------------------------------------------------------------------------
# Sources — simple glob, matching the "keep CMake simple" request
# -----------------------------------------------------------------------------
# NOTE: GLOB does not automatically pick up newly added files on reconfigure.
# Re-run `cmake ..` (or touch CMakeLists.txt) after adding new source files,
# or switch to CONFIGURE_DEPENDS below (works fine in modern CMake/Ninja/Make).

file(GLOB_RECURSE TRACEY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/index.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/general/*.cxx"
)

add_executable(traceyc ${TRACEY_SOURCES})

target_include_directories(traceyc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(traceyc PRIVATE
    tracey_warnings
    tracey_optional_deps
)

# -----------------------------------------------------------------------------
# config.h generation — replaces AC_CONFIG_HEADERS([config.h])
# -----------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)
target_include_directories(traceyc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# -----------------------------------------------------------------------------
# Install rule (optional, replaces automake's implicit `make install`)
# -----------------------------------------------------------------------------

install(TARGETS traceyc RUNTIME DESTINATION bin)
