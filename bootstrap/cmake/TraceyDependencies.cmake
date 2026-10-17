# TraceyDependencies.cmake
#
# Optional third-party dependencies for the bootstrap compiler. Each feature
# is gated by a Kconfig toggle (see Kconfig, "Optional features") so the
# Kconfig file remains the single source of truth:
#
#     CONFIG_TRACEY_USE_ZLIB=y     -> require and link zlib
#     CONFIG_TRACEY_USE_THREADS=y  -> require and link a threading library
#
# If a feature is enabled in `.config` but the corresponding library cannot be
# found, configuration fails loudly rather than silently degrading — an
# explicitly requested feature should never vanish without a diagnostic.
#
# Resolved values are published on the `tracey_optional_deps` INTERFACE target
# so downstream code can simply link against it and test the TRACEY_HAVE_*
# definitions.

add_library(tracey_optional_deps INTERFACE)

# --- zlib -------------------------------------------------------------------
if(TRACEY_CFG_TRACEY_USE_ZLIB)
    find_package(ZLIB)
    if(ZLIB_FOUND)
        message(STATUS "zlib found - enabling zlib support (CONFIG_TRACEY_USE_ZLIB=y)")
        target_link_libraries(tracey_optional_deps INTERFACE ZLIB::ZLIB)
        target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_ZLIB=1)
    else()
        message(FATAL_ERROR
            "CONFIG_TRACEY_USE_ZLIB=y but zlib was not found.\n"
            "Install zlib (e.g. zlib1g-dev / zlib-devel) or disable the option "
            "via `cmake --build . --target menuconfig`.")
    endif()
else()
    message(STATUS "zlib support disabled (CONFIG_TRACEY_USE_ZLIB is not set)")
endif()

# --- threads ----------------------------------------------------------------
if(TRACEY_CFG_TRACEY_USE_THREADS)
    find_package(Threads)
    if(Threads_FOUND)
        message(STATUS "Threads found - enabling threading support (CONFIG_TRACEY_USE_THREADS=y)")
        target_link_libraries(tracey_optional_deps INTERFACE Threads::Threads)
        target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_THREADS=1)
    else()
        message(FATAL_ERROR
            "CONFIG_TRACEY_USE_THREADS=y but a threading library was not found.\n"
            "Install pthread support or disable the option via "
            "`cmake --build . --target menuconfig`.")
    endif()
else()
    message(STATUS "threading support disabled (CONFIG_TRACEY_USE_THREADS is not set)")
endif()
