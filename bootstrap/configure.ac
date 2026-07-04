AC_INIT([tracey], [1.0], [uoctamika.codeberg.org@gmail.com])

AC_CONFIG_SRCDIR([index.cxx])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign subdir-objects silent-rules])

AM_SILENT_RULES([yes])

: ${CFLAGS="-g -O0"}

AC_PROG_CXXCPP

AC_CHECK_HEADERS([
cstdio
cstdlib
cstring
cstdint
cstdbool
cstdnoreturn
])

AC_ARG_VAR(
[ARCH],
[Target architecture: native, i686, or x86_64]
)

AC_MSG_CHECKING([for architecture])

AS_CASE([$ARCH],
[i686], [
CFLAGS="$CFLAGS -m32"
LDFLAGS="$LDFLAGS -m32"
AC_MSG_RESULT([i686])
],
[x86_64], [
CFLAGS="$CFLAGS -m64"
LDFLAGS="$LDFLAGS -m64"
AC_MSG_RESULT([x86_64])
],
[""] | [native], [
AC_MSG_RESULT([native])
],
[
AC_MSG_ERROR([unsupported architecture '$ARCH'])
]
)

AC_CONFIG_FILES([
GNUmakefile
])

AC_OUTPUT
