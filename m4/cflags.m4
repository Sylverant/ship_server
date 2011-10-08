AC_DEFUN([ADD_CFLAGS],
[
    AC_MSG_CHECKING([if $CC accepts $1])
    add_cflags_olds="$CFLAGS"
    CFLAGS="$CFLAGS $1"
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main() { return 0; }])],
                      AC_MSG_RESULT([yes]),
                      AC_MSG_RESULT([no])
                      CFLAGS="$add_cflags_old")
])
