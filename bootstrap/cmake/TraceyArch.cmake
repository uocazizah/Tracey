# TraceyArch.cmake
#
# Target-architecture selection for the bootstrap compiler. The `ARCH` cache
# variable accepts:
#
#   native  - build for the host (default)
#   i686    - force 32-bit (-m32)
#   x86_64  - force 64-bit (-m64)
#
# On the very first configure the cache default is seeded from the Kconfig
# "Default target architecture" choice (TRACEY_CFG_TRACEY_DEFAULT_TARGET_*) when
# one is selected, otherwise it falls back to "native". A later -DARCH=... on
# the command line always wins, and once the cache exists it is left untouched,
# so Kconfig only influences the initial default. This keeps Kconfig as the
# single source of truth without fighting an explicit override.

if(NOT DEFINED CACHE{ARCH})
    # First configure: derive the default from Kconfig, falling back to native.
    set(_tracey_arch_default "native")
    if(DEFINED TRACEY_CFG_TRACEY_DEFAULT_TARGET_I686 AND TRACEY_CFG_TRACEY_DEFAULT_TARGET_I686)
        set(_tracey_arch_default "i686")
    elseif(DEFINED TRACEY_CFG_TRACEY_DEFAULT_TARGET_X86_64 AND TRACEY_CFG_TRACEY_DEFAULT_TARGET_X86_64)
        set(_tracey_arch_default "x86_64")
    endif()

    set(ARCH "${_tracey_arch_default}" CACHE STRING "Target architecture: native, i686, or x86_64")
    unset(_tracey_arch_default)
endif()

# Keep the cache entry's suggested values in sync (the entry now exists).
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
