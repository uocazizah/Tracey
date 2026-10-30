#ifndef TRACEY_CONFIG_HXX
#define TRACEY_CONFIG_HXX

/**
 * Generated build configuration for the Tracey bootstrap compiler.
 *
 * Technical notes:
 * - Wraps Kconfig macros into typed constexpr accessors.
 * - TRACEY_TRACE is compiled out when debug tracing disabled.
 *
 *
 * This header is the single, C++-side entry point to the resolved build
 * settings. It pulls in the Kconfig-generated `kconfig_autoconf.h` (which
 * defines every CONFIG_TRACEY_* value as a macro) and re-exposes the ones the
 * compiler actually consumes as typed, constexpr accessors.
 *
 * Using the accessors instead of the raw CONFIG_* macros gives:
 *   * a real type (std::size_t / bool) rather than an untyped literal,
 *   * a stable, greppable name (tracey::config::...) in the sources,
 *   * a single place to apply defaults or validation.
 */

#include "kconfig_autoconf.h"
#include "config.h"

#include <cstddef>
#include <string_view>

namespace tracey::config {

/** Project version string (TRACEY_VERSION_STRING), forwarded from the CMake
 * project(VERSION ...) so there is a single source of truth for the release
 * number. */
inline constexpr std::string_view version = TRACEY_VERSION_STRING;

/** Maximum accepted source-file size, in bytes (CONFIG_TRACEY_MAX_SOURCE_SIZE).
 * Enforced on every platform by source::read(). */
inline constexpr std::size_t max_source_size =
    static_cast<std::size_t>(CONFIG_TRACEY_MAX_SOURCE_SIZE);

/** Internal stack size, in bytes (CONFIG_TRACEY_STACK_SIZE).
 * Reserved for the upcoming parser/codegen stages; exposed now so the value is
 * configured in one place and available to every module that needs it. */
inline constexpr std::size_t stack_size =
    static_cast<std::size_t>(CONFIG_TRACEY_STACK_SIZE);

/** Maximum lexer errors reported per file (CONFIG_TRACEY_MAX_LEXER_ERRORS). */
inline constexpr std::size_t max_lexer_errors =
    static_cast<std::size_t>(CONFIG_TRACEY_MAX_LEXER_ERRORS);

/** Runtime debug tracing (CONFIG_TRACEY_DEBUG_TRACE). When enabled, the
 * TRACEY_TRACE(...) macro below emits diagnostic output to stderr. */
inline constexpr bool debug_trace =
#if defined(CONFIG_TRACEY_DEBUG_TRACE) && CONFIG_TRACEY_DEBUG_TRACE
    true;
#else
    false;
#endif

/** Optional features (CONFIG_TRACEY_USE_ZLIB, CONFIG_TRACEY_USE_THREADS). */
inline constexpr bool use_zlib =
#if defined(CONFIG_TRACEY_USE_ZLIB) && CONFIG_TRACEY_USE_ZLIB
    true;
#else
    false;
#endif

inline constexpr bool use_threads =
#if defined(CONFIG_TRACEY_USE_THREADS) && CONFIG_TRACEY_USE_THREADS
    true;
#else
    false;
#endif

/** Compiler modules (CONFIG_TRACEY_MODULE_*). */
inline constexpr bool module_parser =
#if defined(CONFIG_TRACEY_MODULE_PARSER) && CONFIG_TRACEY_MODULE_PARSER
    true;
#else
    false;
#endif

inline constexpr bool module_semantic =
#if defined(CONFIG_TRACEY_MODULE_SEMANTIC) && CONFIG_TRACEY_MODULE_SEMANTIC
    true;
#else
    false;
#endif

inline constexpr bool module_codegen =
#if defined(CONFIG_TRACEY_MODULE_CODEGEN) && CONFIG_TRACEY_MODULE_CODEGEN
    true;
#else
    false;
#endif

inline constexpr bool module_assembler =
#if defined(CONFIG_TRACEY_MODULE_ASSEMBLER) && CONFIG_TRACEY_MODULE_ASSEMBLER
    true;
#else
    false;
#endif

inline constexpr bool module_linker =
#if defined(CONFIG_TRACEY_MODULE_LINKER) && CONFIG_TRACEY_MODULE_LINKER
    true;
#else
    false;
#endif

/** Default target architecture (from Kconfig choice). */
inline constexpr const char* default_target =
#if defined(CONFIG_TRACEY_DEFAULT_TARGET_I686) && CONFIG_TRACEY_DEFAULT_TARGET_I686
    "i686";
#elif defined(CONFIG_TRACEY_DEFAULT_TARGET_X86_64) && CONFIG_TRACEY_DEFAULT_TARGET_X86_64
    "x86_64";
#else
    "native";
#endif

/** ABI width in bits (host pointer size). */
inline constexpr int abi_bits = static_cast<int>(sizeof(void*) * 8);

} /** namespace tracey::config */

/** TRACEY_TRACE(...) - compile-time-gated tracing.
 *
 * When CONFIG_TRACEY_DEBUG_TRACE is enabled this forwards to std::print on
 * stderr; otherwise the arguments are not evaluated at all (the whole
 * expression is compiled out), so there is zero runtime cost in the default
 * build. */
#if defined(CONFIG_TRACEY_DEBUG_TRACE) && CONFIG_TRACEY_DEBUG_TRACE
    #include <print>
    #define TRACEY_TRACE(...) ::std::print(stderr, __VA_ARGS__)
#else
    #define TRACEY_TRACE(...) do { } while (false)
#endif

#endif /** TRACEY_CONFIG_HXX */
