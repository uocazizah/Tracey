#ifndef TRACEY_CONFIG_H
#define TRACEY_CONFIG_H

/*
 * Generated build configuration for the Tracey bootstrap compiler.
 *
 * This header is the C-side entry point to the resolved build settings.
 * It includes the Kconfig-generated `kconfig_autoconf.h` and the
 * version header `config.h`, then exposes the values the compiler
 * consumes as typed constants and macros.
 */

#include "config.h"
#include "kconfig_autoconf.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Project version string from the build system. */
#ifndef TRACEY_VERSION_STRING
#ifdef TRACEY_VERSION
#define TRACEY_VERSION_STRING TRACEY_VERSION
#else
#define TRACEY_VERSION_STRING "unknown"
#endif
#endif

#define TRACEY_VERSION TRACEY_VERSION_STRING

/* Maximum accepted source-file size in bytes. */
#define TRACEY_MAX_SOURCE_SIZE ((size_t)(CONFIG_TRACEY_MAX_SOURCE_SIZE))

/* Internal stack size in bytes. */
#define TRACEY_STACK_SIZE ((size_t)(CONFIG_TRACEY_STACK_SIZE))

/* Maximum lexer errors reported per file. */
#define TRACEY_MAX_LEXER_ERRORS ((size_t)(CONFIG_TRACEY_MAX_LEXER_ERRORS))

/* Runtime debug tracing flag. */
#if defined(CONFIG_TRACEY_DEBUG_TRACE) && CONFIG_TRACEY_DEBUG_TRACE
#define TRACEY_DEBUG_TRACE 1
#else
#define TRACEY_DEBUG_TRACE 0
#endif

/* Optional feature flags. */
#if defined(CONFIG_TRACEY_USE_ZLIB) && CONFIG_TRACEY_USE_ZLIB
#define TRACEY_USE_ZLIB 1
#else
#define TRACEY_USE_ZLIB 0
#endif

#if defined(CONFIG_TRACEY_USE_THREADS) && CONFIG_TRACEY_USE_THREADS
#define TRACEY_USE_THREADS 1
#else
#define TRACEY_USE_THREADS 0
#endif

/* Compiler module flags. */
#if defined(CONFIG_TRACEY_MODULE_PARSER) && CONFIG_TRACEY_MODULE_PARSER
#define TRACEY_MODULE_PARSER 1
#else
#define TRACEY_MODULE_PARSER 0
#endif

#if defined(CONFIG_TRACEY_MODULE_SEMANTIC) && CONFIG_TRACEY_MODULE_SEMANTIC
#define TRACEY_MODULE_SEMANTIC 1
#else
#define TRACEY_MODULE_SEMANTIC 0
#endif

#if defined(CONFIG_TRACEY_MODULE_CODEGEN) && CONFIG_TRACEY_MODULE_CODEGEN
#define TRACEY_MODULE_CODEGEN 1
#else
#define TRACEY_MODULE_CODEGEN 0
#endif

#if defined(CONFIG_TRACEY_MODULE_ASSEMBLER) && CONFIG_TRACEY_MODULE_ASSEMBLER
#define TRACEY_MODULE_ASSEMBLER 1
#else
#define TRACEY_MODULE_ASSEMBLER 0
#endif

#if defined(CONFIG_TRACEY_MODULE_LINKER) && CONFIG_TRACEY_MODULE_LINKER
#define TRACEY_MODULE_LINKER 1
#else
#define TRACEY_MODULE_LINKER 0
#endif

/* Default target architecture name. */
#if defined(CONFIG_TRACEY_DEFAULT_TARGET_I686) && CONFIG_TRACEY_DEFAULT_TARGET_I686
#define TRACEY_DEFAULT_TARGET "i686"
#elif defined(CONFIG_TRACEY_DEFAULT_TARGET_X86_64) && CONFIG_TRACEY_DEFAULT_TARGET_X86_64
#define TRACEY_DEFAULT_TARGET "x86_64"
#else
#define TRACEY_DEFAULT_TARGET "native"
#endif

/* ABI width in bits. */
#define TRACEY_ABI_BITS ((int)(sizeof(void *) * 8))

/*
 * TRACEY_TRACE - compile-time-gated tracing.
 *
 * When debug tracing is enabled this forwards to fprintf on stderr.
 * Otherwise the arguments are not evaluated and there is no runtime cost.
 */
#if TRACEY_DEBUG_TRACE
#include <stdio.h>
#define TRACEY_TRACE(...) fprintf(stderr, __VA_ARGS__) /* flawfinder: ignore: format string controlled internally only */
#else
#define TRACEY_TRACE(...)                                                                          \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_CONFIG_H */
