# TraceyWarnings.cmake
#
# Provides the `tracey_warnings` INTERFACE target carrying the project's
# warning flags. Link it into any target that should be built with the full
# warning set:
#
#     target_link_libraries(<target> PRIVATE tracey_warnings)
#
# The optional `TRACEY_WARNINGS_AS_ERRORS` switch promotes every warning to a
# hard error (useful in CI).

add_library(tracey_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Common flags for both GCC and Clang
    target_compile_options(tracey_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wmisleading-indentation
        -Wcast-qual
        -Wundef
        -Wfloat-equal
        -Wmissing-declarations
        -Wmissing-include-dirs
        -Wuninitialized
        -Wswitch-default
        -Wswitch-enum
        -Wmissing-field-initializers
        -Wredundant-decls
        -Wdeprecated
        -Wsign-compare
        -Wvariadic-macros
        -Wsuggest-override
        -Wnon-virtual-dtor
        -Woverloaded-virtual
    )

    # GCC-specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tracey_warnings INTERFACE
            -Wduplicated-cond
            -Wmaybe-uninitialized
            -Wstrict-aliasing=3
            -Wstrict-overflow=5
            -Wvla
            -Wduplicated-branches
            -Wlogical-op
            -Wcast-function-type
            -Wtrampolines
            -Wvector-operation-performance
            -Wclobbered
            -Wempty-body
            -Wignored-qualifiers
            -Walloc-zero
            -Walloca
            -Warray-bounds=2
            -Wattribute-alias=2
            -Wbidi-chars=any
            -Wbuiltin-declaration-mismatch
            -Wformat-signedness
            -Wformat-truncation
            -Wformat-overflow
            -Wrestrict
            -Wstringop-overflow
            -Wstringop-truncation
            -Wshift-overflow
            -Wpacked
            -Wreturn-local-addr
            -Wsequence-point
            -Wsizeof-pointer-memaccess
            -Wtrigraphs
            -Wunused-macros
            -Wvolatile-register-var
            -Wzero-length-bounds
            -Wdisabled-optimization
        )
    endif()

    # Clang-specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(tracey_warnings INTERFACE
            -Wextra-semi
            -Wtautological-compare
            -Wshadow-all
            -Wmissing-variable-declarations
            -Wconditional-uninitialized
            -Wdocumentation
            -Wcovered-switch-default
            -Wdeprecated-implementations
            -Wmissing-noreturn
            -Wused-but-marked-unused
            -Wunused-macros
            -Wunused-parameter
            -Wunused-result
            -Wunused-value
            -Wnewline-eof
            -Wcomma
            -Wjump-misses-init
            -Wfloat-overflow-conversion
            -Wfloat-zero-conversion
        )
    endif()

    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    if(TRACEY_WARNINGS_AS_ERRORS)
        target_compile_options(tracey_warnings INTERFACE -Werror)
    endif()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(tracey_warnings INTERFACE
        /Wall
        /wd4100
        /wd4127
        /wd4514
        /wd4710
        /wd4820
        /wd5045
        /w4477
        /w4490
        /w4711
        /w4917
        /w5039
    )
    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    if(TRACEY_WARNINGS_AS_ERRORS)
        target_compile_options(tracey_warnings INTERFACE /WX)
    endif()
endif()
