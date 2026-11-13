#include "tracey/argument.h"
#include "tracey/assembler.h"
#include "tracey/common.h"
#include "tracey/config.h"
#include "tracey/linker.h"
#include "tracey/pipeline.h"
#include "tracey/source.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * tracey_now_ms - read a monotonic timestamp in milliseconds.
 *
 * Used for the verbose assembler/linker stage timings only.
 */
static double tracey_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/*
 * tracey_make_temp_dir - create a private temporary directory.
 *
 * Returns a heap-allocated NUL-terminated path owned by the caller, or NULL
 * on failure. The directory is created with mode 0700 under $TMPDIR, or
 * /tmp when TMPDIR is unset or empty, and the caller is responsible for
 * removing the files it creates inside it.
 */
static char *tracey_make_temp_dir(void)
{
    const char *base = getenv("TMPDIR"); /* flawfinder: ignore: TMPDIR used only as mkdtemp base with 0700 perms */
    size_t len;
    char *tmpl;

    if (base == NULL || base[0] == '\0')
        base = "/tmp";

    len = strlen(base) + sizeof("/traceyc.XXXXXX");
    tmpl = (char *)malloc(len);
    if (tmpl == NULL)
        return NULL;

    snprintf(tmpl, len, "%s/traceyc.XXXXXX", base);
    if (mkdtemp(tmpl) == NULL)
    {
        free(tmpl);
        return NULL;
    }
    return tmpl;
}

/*
 * tracey_join_path - join a directory and a file name.
 *
 * Returns a heap-allocated NUL-terminated path owned by the caller, or NULL
 * when the result cannot be allocated.
 */
static char *tracey_join_path(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    char *out = (char *)malloc(dlen + nlen + 2);

    if (out == NULL)
        return NULL;
    memcpy(out, dir, dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1, name, nlen);
    out[dlen + 1 + nlen] = '\0';
    return out;
}

/*
 * tracey_is_directory - check whether path names an existing directory.
 */
static bool tracey_is_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode) ? true : false;
}

/*
 * tracey_collect_include_paths - build the final include search list.
 *
 * args supplies user -I paths, which are borrowed from args. The remaining
 * entries are either string literals with static storage or canonicalized
 * environment directories owned by the array returned through pp_owned.
 *
 * The returned array is heap-allocated and owned by the caller. pp_owned
 * receives a matching array whose entries must be freed individually
 * (entries that are not owned hold NULL), or NULL when nothing was allocated.
 */
static const char **tracey_collect_include_paths(const struct tracey_args *args, size_t *p_count,
                                                 char ***pp_owned)
{
    const char *env_vars[2] = {"TRACEY_LIB_PATH", "TRACEY_INCLUDE_PATH"};
    size_t cap = args->include_path_count + 6;
    const char **paths;
    char **owned;
    size_t count = 0;
    size_t i;

    paths = (const char **)calloc(cap, sizeof(*paths));
    owned = (char **)calloc(cap, sizeof(*owned));
    if (paths == NULL || owned == NULL)
    {
        free(paths);
        free(owned);
        *p_count = 0;
        *pp_owned = NULL;
        return NULL;
    }

    for (i = 0; i < args->include_path_count; i++)
        paths[count++] = args->include_paths[i];

    paths[count++] = "lib";
    paths[count++] = "include";
#ifdef TRACEY_LIBDIR
    paths[count++] = TRACEY_LIBDIR;
#endif
#ifdef TRACEY_INCLUDEDIR
    paths[count++] = TRACEY_INCLUDEDIR;
#endif

    /*
     * Environment variables are untrusted input. Only existing directories
     * are accepted, and their canonical form is used so that later include
     * resolution is not affected by relative components. Invalid values are
     * ignored with a warning instead of aborting the compilation.
     */
    for (i = 0; i < sizeof(env_vars) / sizeof(env_vars[0]); i++)
    {
        const char *value = getenv(env_vars[i]); /* flawfinder: ignore: validated before canonicalization with realpath */
        char *canonical;

        if (value == NULL || value[0] == '\0')
            continue;
        if (!tracey_is_directory(value))
        {
            fprintf(stderr,
                    "Warning: ignoring environment variable %s='%s' because it is not a directory\n",
                    env_vars[i], value);
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m set it to an existing directory or unset it\n");
            continue;
        }
        canonical = realpath(value, NULL); /* flawfinder: ignore: realpath used for canonicalization best practice */
        if (canonical == NULL)
        {
            fprintf(stderr, "Warning: ignoring environment variable %s='%s': %s\n", env_vars[i],
                    value, strerror(errno));
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m check directory permissions and canonical path\n");
            continue;
        }
        owned[count] = canonical;
        paths[count++] = canonical;
    }

    *p_count = count;
    *pp_owned = owned;
    return paths;
}

int main(int argc, char **argv)
{
    struct tracey_args_config config;
    struct tracey_args args;
    enum tracey_args_error parse_error;
    const char *const *include_paths;
    char **owned_paths;
    size_t include_count;
    struct tracey_compile_options opts;
    struct tracey_compile_pipeline pipeline;
    struct tracey_source *sources;
    struct tracey_string generated;
    bool had_error = false;
    bool verbose;
    size_t i;

    memset(&config, 0, sizeof(config));
    config.program_name = "traceyc";
    config.version_string = TRACEY_VERSION_STRING;
    config.description = "Tracey compiler - compile .trc files";
    config.allow_unknown_options = false;
    config.max_args = 10000;

    tracey_args_init(&args);
    parse_error = tracey_args_parse(argc, argv, &config, &args);
    if (parse_error != TRACEY_ARGS_OK)
    {
        switch (parse_error)
        {
            case TRACEY_ARGS_NOMEM:
                fprintf(stderr, "Error: out of memory while parsing command-line arguments\n");
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m reduce the number of arguments or free memory\n");
                break;
            case TRACEY_ARGS_TOO_MANY:
                fprintf(stderr, "Error: too many command-line arguments (max %zu)\n",
                        config.max_args);
                fprintf(stderr,
                        "\033[1;34mnote:\033[0m split the invocation or reduce input files\n");
                break;
            case TRACEY_ARGS_MISSING_ARG:
                fprintf(stderr, "Error: missing required command-line argument\n");
                fprintf(stderr, "\033[1;34mnote:\033[0m provide the missing option value\n");
                break;
            case TRACEY_ARGS_EMPTY_ARG:
                fprintf(stderr, "Error: empty command-line argument is not allowed\n");
                fprintf(stderr, "\033[1;34mnote:\033[0m provide a non-empty value\n");
                break;
            case TRACEY_ARGS_INVALID:
            default:
                fprintf(stderr, "Error: %s\n", tracey_args_error_string(parse_error));
                fprintf(stderr, "\033[1;34mnote:\033[0m review the command-line syntax\n");
                tracey_args_print_help(&config, stderr);
                break;
        }
        tracey_args_free(&args);
        return 1;
    }

    verbose = args.verbose;

    if (args.help)
    {
        tracey_args_print_help(&config, stdout);
        tracey_args_free(&args);
        return 0;
    }
    if (args.version)
    {
        tracey_args_print_version(&config, stdout);
        tracey_args_free(&args);
        return 0;
    }

    if (args.input_count == 0)
    {
        fprintf(stderr, "traceyc: \033[1;31mfatal error:\033[0m no input files provided\n");
        fprintf(stderr, "traceyc: \033[1;34mnote:\033[0m pass at least one .trc source file\n");
        tracey_args_free(&args);
        return 2;
    }

    sources = (struct tracey_source *)calloc(args.input_count, sizeof(*sources));
    if (sources == NULL)
    {
        fprintf(stderr, "Error: out of memory while loading input files\n");
        tracey_args_free(&args);
        return 1;
    }

    for (i = 0; i < args.input_count; i++)
    {
        enum tracey_source_error err = tracey_source_read(args.inputs[i], &sources[i]);

        if (err != TRACEY_SOURCE_OK)
        {
            fprintf(stderr, "Error: failed to read input file '%s': %s\n", args.inputs[i],
                    tracey_source_error_string(err));
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m check file existence, permissions, and that it is a "
                    "regular file\n");
            while (i > 0)
                tracey_source_free(&sources[--i]);
            free(sources);
            tracey_args_free(&args);
            return 1;
        }
    }

    if (verbose)
    {
        tracey_args_print_debug(&args, stderr);
        for (i = 0; i < args.input_count; i++)
        {
            if (i > 0)
                fprintf(stderr, "\n");
            tracey_source_print_debug(&sources[i], stderr);
        }
    }

    include_paths = tracey_collect_include_paths(&args, &include_count, &owned_paths);
    if (include_paths == NULL)
    {
        fprintf(stderr, "Error: out of memory while building include paths\n");
        for (i = 0; i < args.input_count; i++)
            tracey_source_free(&sources[i]);
        free(sources);
        tracey_args_free(&args);
        return 1;
    }

    memset(&opts, 0, sizeof(opts));
    opts.arch = TRACEY_TARGET_AARCH64;
    opts.verbose = verbose;
    opts.include_paths = include_paths;
    opts.include_path_count = include_count;

    tracey_pipeline_init(&pipeline, &opts);
    tracey_string_init(&generated);

    for (i = 0; i < args.input_count; i++)
    {
        bool emit_runtime = (i + 1 == args.input_count);
        struct tracey_file_result result =
            tracey_pipeline_process_file(&pipeline, &sources[i], args.inputs[i], i, emit_runtime);

        if (result.had_error)
        {
            had_error = true;
            free(result.assembly);
            continue;
        }
        if (result.assembly != NULL)
        {
            tracey_string_append_cstr(&generated, result.assembly);
            free(result.assembly);
        }
    }

    for (i = 0; i < args.input_count; i++)
        tracey_source_free(&sources[i]);
    free(sources);

    for (i = 0; i < include_count; i++)
        free(owned_paths[i]);
    free(owned_paths);
    free((void *)include_paths);

    if (had_error)
    {
        tracey_string_free(&generated);
        tracey_args_free(&args);
        return 1;
    }

    /*
     * -S stops after code generation and emits assembly text either to the
     * requested output file or to standard output.
     */
    if (args.assembly)
    {
        FILE *out = stdout;

        if (args.output != NULL)
        {
            out = fopen(args.output, "w");
            if (out == NULL)
            {
                fprintf(stderr, "Error: cannot write output file\n");
                fprintf(stderr, "\033[1;34mnote:\033[0m check output path and permissions\n");
                tracey_string_free(&generated);
                tracey_args_free(&args);
                return 1;
            }
        }
        if (generated.data != NULL)
            fwrite(generated.data, 1, generated.len, out);
        if (out != stdout && fclose(out) != 0)
        {
            fprintf(stderr, "Error: cannot finalize output file\n");
            tracey_string_free(&generated);
            tracey_args_free(&args);
            return 1;
        }
        tracey_string_free(&generated);
        tracey_args_free(&args);
        return 0;
    }

    {
        const char *exe_path = args.output != NULL ? args.output : "a.out";
        char *tmp_dir = tracey_make_temp_dir();
        char *obj_path = NULL;
        struct tracey_asm_result asm_result;
        bool ok = true;

        if (tmp_dir == NULL)
        {
            fprintf(stderr, "Error: cannot create temporary directory\n");
            fprintf(stderr, "\033[1;34mnote:\033[0m set TMPDIR to a writable directory\n");
            tracey_string_free(&generated);
            tracey_args_free(&args);
            return 1;
        }

        obj_path = tracey_join_path(tmp_dir, "traceyc.o");
        if (obj_path == NULL)
        {
            fprintf(stderr, "Error: out of memory while preparing temporary paths\n");
            rmdir(tmp_dir);
            free(tmp_dir);
            tracey_string_free(&generated);
            tracey_args_free(&args);
            return 1;
        }

        if (verbose)
            fprintf(stderr, "[assemble] start\n");
        {
            double start = tracey_now_ms();
            size_t generated_len = generated.data != NULL ? generated.len : 0;

            asm_result = tracey_assemble_aarch64(generated.data != NULL ? generated.data : "",
                                                generated_len);
            if (verbose)
                fprintf(stderr, "[assemble] done (%.3f ms)\n", tracey_now_ms() - start);
        }

        if (!asm_result.ok)
        {
            fprintf(stderr, "Error: assembler failed: %s\n", asm_result.error);
            fprintf(stderr,
                    "\033[1;34mnote:\033[0m inspect generated assembly around the reported "
                    "instruction\n");
            ok = false;
        }
        else if (!tracey_write_elf_object(obj_path, &asm_result))
        {
            fprintf(stderr, "Error: cannot write object file\n");
            fprintf(stderr, "\033[1;34mnote:\033[0m check temporary directory permissions\n");
            ok = false;
        }
        tracey_asm_result_free(&asm_result);

        if (ok)
        {
            const char *objects[1];

            objects[0] = obj_path;
            if (verbose)
                fprintf(stderr, "[link] start\n");
            {
                double start = tracey_now_ms();

                if (!tracey_link_elf_aarch64(objects, 1, exe_path))
                {
                    fprintf(stderr, "Error: linking failed\n");
                    fprintf(stderr,
                            "\033[1;34mnote:\033[0m verify object files, entry point, and "
                            "unresolved symbols\n");
                    ok = false;
                }
                if (verbose)
                    fprintf(stderr, "[link] done (%.3f ms)\n", tracey_now_ms() - start);
            }
        }

        if (ok)
        {
            /*
             * Mark the produced executable as runnable for everyone. Failure
             * to change the mode is not fatal: the file itself is valid and
             * can still be copied or run through an explicit interpreter.
             */
            struct stat st;
            if (stat(exe_path, &st) == 0)
                (void)chmod(exe_path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH); /* flawfinder: ignore: chmod on newly created output file */
            printf("Output: %s\n", exe_path);
        }

        unlink(obj_path);
        rmdir(tmp_dir);
        free(obj_path);
        free(tmp_dir);

        tracey_string_free(&generated);
        tracey_args_free(&args);
        return ok ? 0 : 1;
    }
}
