# PSC_CHECK_ADIOS
# --------------------------------------
# checks for ADIOS and sets ADIOS_CFLAGS, ADIOS_LIBS

AC_DEFUN([PSC_CHECK_ADIOS],
  [
  AC_ARG_WITH(
    [adios],
    [AS_HELP_STRING([--with-adios=[ARG]],[use adios in directory ARG])],
    [AS_IF([test "x$with_adios" = xyes], [with_adios=$ADIOS_DIR])],
    [with_adios="no"]
   )


# Beginning of giant if, so we don't run checks if we don't want adios
if test "x$with_adios" != xno; then

AS_IF([test -z "$with_adios"], [AC_MSG_FAILURE([--with-adios does not give path, and ADIOS_DIR not set])])

AC_MSG_CHECKING([adios version])
# Fixme, this is the ugliest way ever to do a version check
adios_version=`${with_adios}/bin/adios_config -v`

AC_MSG_RESULT([${adios_version}])
AS_VERSION_COMPARE([${adios_version}], [$1], [AC_MSG_FAILURE([Minimum adios version required is $1])])


ADIOS_CFLAGS=`${with_adios}/bin/adios_config -c`
ADIOS_LIBS=`${with_adios}/bin/adios_config -l`

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $ADIOS_CFLAGS"
AC_CHECK_HEADER([adios.h],
  [],
  [AC_MSG_FAILURE([adios.h not found])]
)

CPPFLAGS="$save_CPPFLAGS"
have_adios="yes"
# end of giant if
else
have_adios="no"
ADIOS_CFLAGS=""
ADIOS_LIBS=""
fi

AC_SUBST([ADIOS_CFLAGS])
AC_SUBST([ADIOS_LIBS])
])
