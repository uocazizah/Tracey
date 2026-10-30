# general/CMakeLists.txt
#
# Per-module static libraries for the bootstrap compiler. Each module owns a
# slice of the compiler front end and exposes its interface via the matching
# header in include/tracey/*.hxx. The final `traceyc` executable links these
# together (see the top-level CMakeLists.txt).
#
# Sources are listed explicitly (no GLOB) so adding a file is a deliberate,
# reviewable change and incremental builds never miss a new translation unit.

# --- tracey_source: on-disk source loading (hardened POSIX + portable) ------
add_library(tracey_source STATIC
    source/source.cxx
    source/source_debug.cxx
)

# --- tracey_lexer: lexical analysis over an in-memory source view -----------
add_library(tracey_lexer STATIC
    lexer/lexer.cxx
    lexer/lexer_debug.cxx
)

# --- tracey_argument: command-line parsing ----------------------------------
add_library(tracey_argument STATIC
    argument/argument.cxx
    argument/argument_parse.cxx
    argument/argument_help.cxx
    argument/argument_debug.cxx
)

# Every module shares the same public headers, generated build config
# (tracey_config) and strict warning set.
foreach(_tracey_module tracey_source tracey_lexer tracey_argument)
    target_include_directories(${_tracey_module} PUBLIC
        "${TRACEY_INCLUDE_DIR}"
    )
    target_link_libraries(${_tracey_module} PUBLIC
        tracey_config
        tracey_warnings
    )
    # Every module includes <tracey/config.hxx>, which pulls in the generated
    # kconfig_autoconf.h. Make sure the header is produced before any module
    # object is compiled (matters for parallel builds).
    add_dependencies(${_tracey_module} tracey_kconfig_header)
endforeach()
