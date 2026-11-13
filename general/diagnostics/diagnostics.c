#include "tracey/diagnostics.h"

/*
 * tracey_report_diagnostic - report a compiler diagnostic message.
 *
 * msg points to a NUL-terminated message string. The placeholder
 * implementation intentionally ignores msg until a structured
 * diagnostic sink is available.
 */
void tracey_report_diagnostic(const char *msg)
{
    (void)msg;
}
