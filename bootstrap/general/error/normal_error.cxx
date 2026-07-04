#include <cstdarg>
/* #include <cstdbool> */
#include <cstdio>
#include <cstdlib>

#define TRACEYC_NAME   "\033[1mtraceyc\033[0m"

#define COLOR_FATAL    "\033[1;31m"
#define COLOR_ERROR    "\033[31m"
#define COLOR_WARNING "\033[35m"
#define COLOR_RESET    "\033[0m"

static void print_message(
    const char *color,
    const char *type,
    const char *fmt,
    va_list args)
{
    fprintf(stderr,
            "%s: %s%s%s: ",
            TRACEYC_NAME,
            color,
            type,
            COLOR_RESET);

    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    print_message(COLOR_ERROR, "error", fmt, args);
    va_end(args);
}

void warning(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    print_message(COLOR_WARNING, "warning", fmt, args);
    va_end(args);
}

[[noreturn]] void fatal(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    print_message(COLOR_FATAL, "fatal", fmt, args);
    va_end(args);

    exit(EXIT_FAILURE);
}
