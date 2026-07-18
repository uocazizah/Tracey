cmake_minimum_required(VERSION 3.10)
project(tracey VERSION 1.0 LANGUAGES CXX)

# ------------------------------------------------------------
# Jika dijalankan dari dalam sumber (in‑source), arahkan ke cmake_build/
# ------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(WARNING "In‑source build terdeteksi. Semua berkas build akan diletakkan di cmake_build/")
    file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/cmake_build)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -B cmake_build -H.
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    message(STATUS "Konfigurasi selesai di cmake_build/. Sekarang jalankan: cmake --build cmake_build")
    return()
endif()

# ------------------------------------------------------------
# Tentukan lokasi config.h.in di cmake/ dan output config.h di cmake_build/
# ------------------------------------------------------------
set(CONFIG_IN_DIR ${CMAKE_SOURCE_DIR}/cmake)
set(CONFIG_OUT_DIR ${CMAKE_BINARY_DIR})

# Pastikan direktori cmake/ ada
if(NOT EXISTS ${CONFIG_IN_DIR})
    message(FATAL_ERROR "Direktori cmake/ tidak ditemukan di ${CMAKE_SOURCE_DIR}")
endif()

# ------------------------------------------------------------
# Opsi arsitektur (ARCH)
# ------------------------------------------------------------
set(ARCH "native" CACHE STRING "Target architecture: native, i686, or x86_64")
if(ARCH STREQUAL "i686")
    add_compile_options(-m32)
    add_link_options(-m32)
elseif(ARCH STREQUAL "x86_64")
    add_compile_options(-m64)
    add_link_options(-m64)
elseif(NOT ARCH STREQUAL "native")
    message(FATAL_ERROR "Arsitektur '${ARCH}' tidak didukung")
endif()

# ------------------------------------------------------------
# Periksa header yang dibutuhkan (mirip AC_CHECK_HEADERS)
# ------------------------------------------------------------
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
    # Ubah nama header menjadi variabel yang valid
    string(REPLACE "/" "_" HEADER_VAR ${HEADER})
    string(REPLACE "." "_" HEADER_VAR ${HEADER_VAR})
    string(REPLACE "+" "PLUS" HEADER_VAR ${HEADER_VAR})
    string(TOUPPER ${HEADER_VAR} HEADER_VAR)
    check_include_file_cxx(${HEADER} HAVE_${HEADER_VAR})
endforeach()

# ------------------------------------------------------------
# Hasilkan config.h dari cmake/config.h.in ke cmake_build/config.h
# ------------------------------------------------------------
configure_file(
    ${CONFIG_IN_DIR}/config.h.in
    ${CONFIG_OUT_DIR}/config.h
    @ONLY
)

# ------------------------------------------------------------
# Sumber program
# ------------------------------------------------------------
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

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------
add_executable(traceyc ${SOURCES})

# Direktori include: tambahkan cmake_build/ untuk akses config.h
target_include_directories(traceyc PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}   # <-- agar bisa #include "config.h"
)

# Standar C++17
set_target_properties(traceyc PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Flag kompilasi (Wall, Wextra, -g -O0)
target_compile_options(traceyc PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall -Wextra -g -O0
    >
)

# Flag tambahan jika dibutuhkan (misal untuk Windows)
if(WIN32)
    target_compile_definitions(traceyc PRIVATE _WIN32_WINNT=0x0600)
endif()

# ------------------------------------------------------------
# Informasi build
# ------------------------------------------------------------
message(STATUS "Config.h.in dari: ${CONFIG_IN_DIR}/config.h.in")
message(STATUS "Config.h ke: ${CONFIG_OUT_DIR}/config.h")
