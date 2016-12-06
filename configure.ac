# Init
AC_INIT([daq-acquire], [0.0.1], [i.ucar86@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_GNU_SOURCE
CFLAGS="$CFLAGS -pedantic -Wall -std=gnu99"

# Checks for programs
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

# Checks for libraries
AC_CHECK_LIB([m], [cos],, [AC_MSG_ERROR([math required])])
AC_CHECK_LIB([comedi], [comedi_open], [LIBS="-lcomedi -lm "], [AC_MSG_ERROR([comedilib required])])

# Checks for header files
AC_CHECK_HEADERS([sched.h],, [AC_MSG_ERROR([<sched.h> required])])
AC_CHECK_HEADERS([sys/mman.h],, [AC_MSG_ERROR([<sys/mman.h> required])])
AC_CHECK_HEADERS([comedilib.h],, [AC_MSG_ERROR([<comedilib.h> required])])

# Checks for typedefs, structures, and compiler characteristics

# Checks for library functions
AC_CHECK_FUNCS([clock_gettime],, [AC_MSG_ERROR([clock_gettime required])])
AC_CHECK_FUNCS([memset],, [AC_MSG_ERROR([memset required])])
AC_CHECK_FUNCS([strtoul],, [AC_MSG_ERROR([strtoul required])])
AC_FUNC_MMAP
AC_FUNC_STRTOD

# Output files
AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([
  Makefile
  src/Makefile
])
AC_OUTPUT
