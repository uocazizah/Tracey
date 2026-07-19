cmake_minimum_required(VERSION 3.10)
project(tracey VERSION 1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(WARNING "In-source build detected. All build files will be placed in cmake_build/")
    file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/cmake_build)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -B cmake_build -H.
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    message(STATUS "Configuration completed in cmake_build/. Now run: cmake --build cmake_build")
    return()
endif()

set(CONFIG_IN_DIR ${CMAKE_SOURCE_DIR}/cmake)
set(CONFIG_OUT_DIR ${CMAKE_BINARY_DIR})

if(NOT EXISTS ${CONFIG_IN_DIR})
    message(FATAL_ERROR "Directory cmake/ not found in ${CMAKE_SOURCE_DIR}")
endif()

set(ARCH "native" CACHE STRING "Target architecture: native, i686, or x86_64")
if(ARCH STREQUAL "i686")
    add_compile_options(-m32)
    add_link_options(-m32)
elseif(ARCH STREQUAL "x86_64")
    add_compile_options(-m64)
    add_link_options(-m64)
elseif(NOT ARCH STREQUAL "native")
    message(FATAL_ERROR "Architecture '${ARCH}' not supported")
endif()

include(CheckIncludeFileCXX)

set(HEADER_LIST
    windows.h
    algorithm
    bits/stdc++.h
    cassert
    cctype
    cerrno
    cfloat
    climits
    cmath
    csetjmp
    csignal
    cstdarg
    cstddef
    cstdint
    cstdio
    cstdlib
    cstring
    ctime
    cwchar
    cwctype
    deque
    exception
    fstream
    functional
    iomanip
    ios
    iosfwd
    iostream
    istream
    iterator
    limits
    list
    locale
    map
    memory
    new
    numeric
    ostream
    queue
    set
    sstream
    stack
    stdexcept
    streambuf
    string
    typeinfo
    utility
    valarray
    vector
)

foreach(HEADER ${HEADER_LIST})
    string(REPLACE "/" "_" HEADER_VAR ${HEADER})
    string(REPLACE "." "_" HEADER_VAR ${HEADER_VAR})
    string(REPLACE "+" "PLUS" HEADER_VAR ${HEADER_VAR})
    string(TOUPPER ${HEADER_VAR} HEADER_VAR)
    check_include_file_cxx(${HEADER} HAVE_${HEADER_VAR})
endforeach()

configure_file(
    ${CONFIG_IN_DIR}/config.h.in
    ${CONFIG_OUT_DIR}/config.h
    @ONLY
)

set(SOURCES
    index.cxx
    general/argument/constructor.cxx
    general/argument/parse_argument.cxx
    general/argument/debug_print_argument.cxx
    general/argument/get_input_file.cxx
    general/argument/get_output_file.cxx
    general/argument/get_warning_all.cxx
    general/argument/get_warning_extra.cxx
    general/compile/compiler_pipeline.cxx
    general/error/normal_error.cxx
    general/file/close.cxx
    general/file/constructor.cxx
    general/file/debug.cxx
    general/file/destructor.cxx
    general/file/move_assignment.cxx
    general/file/move_constructor.cxx
    general/file/open.cxx
)

add_executable(traceyc ${SOURCES})

target_include_directories(traceyc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

set_target_properties(traceyc PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_compile_options(traceyc PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall -Wextra -g -O0
    >
)

if(WIN32)
    target_compile_definitions(traceyc PRIVATE _WIN32_WINNT=0x0600)
endif()

message(STATUS "Config.h.in from: ${CONFIG_IN_DIR}/config.h.in")
message(STATUS "Config.h to: ${CONFIG_OUT_DIR}/config.h")
