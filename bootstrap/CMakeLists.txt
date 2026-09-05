cmake_minimum_required(VERSION 3.15...3.30)  
  
project(tracey  
    VERSION 1.0  
    DESCRIPTION "Tracey compiler"  
    LANGUAGES C  
)  
  
set(CMAKE_C_STANDARD 17)  
set(CMAKE_C_STANDARD_REQUIRED ON)  
set(CMAKE_C_EXTENSIONS OFF)  
  
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)  
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)  
endif()  
  
set(CMAKE_C_FLAGS_DEBUG "-g -O0")  
  
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  
  
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
  
add_library(tracey_warnings INTERFACE)  
  
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")  
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
        -Wduplicated-cond  
        -Wcast-qual  
        -Wundef  
        -Wfloat-equal  
        -Wmissing-declarations  
        -Wmissing-include-dirs  
        -Wuninitialized  
        -Wmaybe-uninitialized  
        -Wstrict-aliasing=3  
        -Wstrict-overflow=5  
        -Wswitch-default  
        -Wswitch-enum  
        -Wmissing-field-initializers  
        -Wredundant-decls  
        -Wdeprecated  
        -Wsign-compare  
        -Wmissing-prototypes  
        -Wstrict-prototypes  
        -Wold-style-definition  
        -Wmissing-parameter-type  
        -Wpointer-arith  
        -Wwrite-strings  
        -Waggregate-return  
        -Wbad-function-cast  
        -Wnested-externs  
        -Wjump-misses-init  
        -Wdeclaration-after-statement  
        -Wvla  
        -Wvariadic-macros  
        $<$<C_COMPILER_ID:Clang>:  
            -Wextra-semi  
            -Wtautological-compare  
            -Wshadow-all  
            -Wmissing-variable-declarations  
            -Wconditional-uninitialized  
            -Wc99-c11-compat  
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
            -Wc17-compat  
            -Wcomma  
            -Wempty-translation-unit  
            -Wfloat-overflow-conversion  
            -Wfloat-zero-conversion  
            -Werror=implicit-function-declaration  
            -Werror=incompatible-pointer-types  
        >  
    )  
  
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")  
        target_compile_options(tracey_warnings INTERFACE  
            -Wduplicated-branches  
            -Wlogical-op  
            -Wcast-function-type  
            -Wtrampolines  
            -Wvector-operation-performance  
            -Wclobbered  
            -Wempty-body  
            -Wignored-qualifiers  
            -Wmaybe-uninitialized  
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
            -Wpadded  
            -Wpointer-sign  
            -Wreturn-local-addr  
            -Wsequence-point  
            -Wsizeof-array-argument  
            -Wsizeof-pointer-memaccess  
            -Wtrigraphs  
            -Wunused-macros  
            -Wvolatile-register-var  
            -Wzero-length-bounds  
            -Wdisabled-optimization  
            -Werror=implicit-function-declaration  
            -Werror=incompatible-pointer-types  
            -Werror=int-conversion  
        )  
    endif()  
  
    option(TRACEY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)  
    if(TRACEY_WARNINGS_AS_ERRORS)  
        target_compile_options(tracey_warnings INTERFACE -Werror)  
    endif()  
  
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")  
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
  
  
add_library(tracey_optional_deps INTERFACE)  
  
include(CheckIncludeFile)  
  
# Essential C standard headers (all mandatory in hosted C17, but still useful to verify)  
check_include_file("stdio.h"     HAVE_STDIO_H)  
check_include_file("stdlib.h"    HAVE_STDLIB_H)  
check_include_file("string.h"    HAVE_STRING_H)  
check_include_file("stddef.h"    HAVE_STDDEF_H)  
check_include_file("stdint.h"    HAVE_STDINT_H)     # C99/C11  
check_include_file("inttypes.h"  HAVE_INTTYPES_H)   # C99/C11  
check_include_file("limits.h"    HAVE_LIMITS_H)  
check_include_file("math.h"      HAVE_MATH_H)  
  
# Optional POSIX / system headers (you may want these)  
check_include_file("unistd.h"    HAVE_UNISTD_H)  
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)  
check_include_file("fcntl.h"     HAVE_FCNTL_H)  
  
find_package(ZLIB QUIET)  
if(ZLIB_FOUND)  
    message(STATUS "zlib found — enabling optional zlib support")  
    target_link_libraries(tracey_optional_deps INTERFACE ZLIB::ZLIB)  
    target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_ZLIB=1)  
else()  
    message(STATUS "zlib not found — building without optional zlib support")  
endif()  
  
find_package(Threads QUIET)  
if(Threads_FOUND)  
    message(STATUS "Threads found — enabling optional threading support")  
    target_link_libraries(tracey_optional_deps INTERFACE Threads::Threads)  
    target_compile_definitions(tracey_optional_deps INTERFACE TRACEY_HAVE_THREADS=1)  
else()  
    message(STATUS "Threads not found — building without optional threading support")  
endif()  
  
file(GLOB_RECURSE TRACEY_SOURCES CONFIGURE_DEPENDS  
    "${CMAKE_CURRENT_SOURCE_DIR}/index.c"  
    "${CMAKE_CURRENT_SOURCE_DIR}/general/*.c"  
)  
  
add_executable(traceyc ${TRACEY_SOURCES})  
  
target_include_directories(traceyc PRIVATE  
    "${CMAKE_CURRENT_SOURCE_DIR}/include"  
)  
  
target_link_libraries(traceyc PRIVATE  
    tracey_warnings  
    tracey_optional_deps  
)  
  
configure_file(  
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"  
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"  
)  
target_include_directories(traceyc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")  
  
install(TARGETS traceyc RUNTIME DESTINATION bin)  
