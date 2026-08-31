cmake_minimum_required(VERSION 3.16)

project(tracey
    VERSION 1.0
    DESCRIPTION "Tracey compiler"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# Basic project setup
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
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
        # ---- Core set (already there) ----
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

        # ---- Newly added (safe & generally useful) ----
        -Wcast-qual
        -Wzero-as-null-pointer-constant
        -Wundef
        -Wreorder
        -Wsign-promo
        -Wfloat-equal
        -Wmissing-declarations
        -Wmissing-include-dirs

        # ---- Additional “bread‑and‑butter” warnings ----
        -Wuninitialized               # catches uninitialised variables (already in -Wall, but explicit)
        -Wmaybe-uninitialized         # GCC‑only? Actually also in Clang with -Wuninitialized, but we keep for completeness
        -Wstrict-aliasing=3           # strict aliasing rules (level 3 is moderate)
        -Wstrict-overflow=5           # warn about signed overflow (level 5 is more conservative)
        -Wswitch-default              # warn when switch lacks a default case
        -Wswitch-enum                 # warn if enum switch misses a case (can be noisy, but good for exhaustive handling)
        -Wmissing-field-initializers  # warn if struct/class init misses a field (C++ supports it)
        -Wredundant-decls             # warn about redundant declarations in the same scope
        -Wctor-dtor-privacy           # warn when class has private constructor/destructor and no friends
        -Wdeprecated                  # warn about deprecated features (e.g., use of deprecated declarations)
        -Wnoexcept                    # warn about missing noexcept in move/swap
        -Wnoexcept-type               # warn about incompatible noexcept specifications

        # ---- C++11/14/17 modernisation hints (low noise, high payoff) ----
        -Wsuggest-override            # suggest adding `override` where appropriate
        -Wsuggest-final-types         # suggest final for classes never derived from (GCC only? Clang also has -Wfinal-...)
        -Wsuggest-final-methods       # suggest final for methods that shouldn't be overridden

        # ---- Clang‑specific extras (GCC ignores these gracefully) ----
        $<$<CXX_COMPILER_ID:Clang>:
            -Wextra-semi               # warn about extra semicolons (C++11)
            -Winconsistent-missing-destructor-override
            -Winconsistent-missing-override
            -Wtautological-compare     # compare always true/false
            -Wshadow-all               # even stricter shadowing
            -Wmissing-variable-declarations  # global vars without extern
            -Wconditional-uninitialized
        >
    )

    # ---- GCC‑only extras (Clang doesn't recognise all of them) ----
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tracey_warnings INTERFACE
            -Wduplicated-branches
            -Wlogical-op
            -Wcast-function-type      # warn about casting function pointers to incompatible types
            -Wtrampolines             # warn about trampolines (e.g., nested functions)
            -Wvector-operation-performance
            # -Weffc++                # VERY noisy; I recommend NOT enabling it by default
        )
    endif()

    # ---- Optional: treat warnings as errors (still OFF by default) ----
    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    if(TRACEY_WARNINGS_AS_ERRORS)
        target_compile_options(tracey_warnings INTERFACE -Werror)
    endif()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC: /W4 is a good baseline; add a few more specific warnings
    target_compile_options(tracey_warnings INTERFACE
        /W4
        # Additional warnings (with /w<number> syntax)
        /w14265  # class with virtual functions but non‑virtual destructor
        /w14296  # non‑virtual operator= in class with virtual functions
        /w14640  # enable /W4 warning about using a negative number in unsigned context
        /w14826  # conversion from 'type1' to 'type2' is sign‑extended
        /w14928  # inconsistent annotation for 'new': __declspec(allocator) mismatch
    )
    # /WX remains optional via the same option
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
