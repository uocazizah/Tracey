# TraceySanitizers.cmake
#
# Optional sanitizer support. These are ordinary CMake options so they can be
# toggled per-configuration:
#
#     cmake -DTRACEY_ENABLE_ASAN=ON -DTRACEY_ENABLE_UBSAN=ON ..
#
# The flags are applied globally (add_compile_options / add_link_options) so
# that every target in the build — including the runtime and any future
# tools — is instrumented consistently.

option(TRACEY_ENABLE_ASAN  "Enable AddressSanitizer"      OFF)
option(TRACEY_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(TRACEY_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(TRACEY_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()
