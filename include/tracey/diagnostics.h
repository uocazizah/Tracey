#ifndef TRACEY_DIAGNOSTICS_H
#define TRACEY_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tracey_report_diagnostic - report a compiler diagnostic message.
 *
 * msg points to a NUL-terminated message string. The current
 * implementation is a placeholder and does not emit output; callers
 * retain ownership of msg and the string must remain valid for the
 * duration of the call.
 */
void tracey_report_diagnostic(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* TRACEY_DIAGNOSTICS_H */
