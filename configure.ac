AC_INIT([xmppconsole], [dev], [pasis.ua@gmail.com])
AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_CONFIG_FILES([Makefile])

AC_PROG_CC
AM_PROG_CC_C_O

PKG_CHECK_MODULES([libstrophe], [libstrophe >= 0.9.0],
    [
        LIBS="$libstrophe_LIBS $LIBS"
        CFLAGS="$CFLAGS $libstrophe_CFLAGS"
    ],
    [AC_MSG_ERROR([libstrophe is required])])

PKG_CHECK_MODULES([ncursesw], [ncursesw],
    [
        LIBS="$ncursesw_LIBS $LIBS"
        CFLAGS="$CFLAGS $ncursesw_CFLAGS"
    ],
    [AC_MSG_ERROR([ncursesw is required])])

PKG_CHECK_MODULES([readline], [readline],
    [
        LIBS="$readline_LIBS $LIBS"
        CFLAGS="$CFLAGS $readline_CFLAGS"
    ],
    [
        AC_MSG_CHECKING([for -lreadline])
        LIBS="-lreadline $LIBS"
        AC_LINK_IFELSE([AC_LANG_SOURCE([[
            #include <stdio.h>
            #include <readline/history.h>
            #include <readline/readline.h>
            int main()
            {
                rl_callback_handler_install("", NULL);
                return 0;
            }
            ]])],
            [AC_MSG_RESULT([yes])],
            [AC_MSG_ERROR([readline is required])])
    ])

PKG_CHECK_MODULES([gtk], [gtk+-3.0],
    [
        LIBS="$gtk_LIBS $LIBS"
        CFLAGS="$CFLAGS $gtk_CFLAGS"
    ],
    [AC_MSG_ERROR([gtk+-3.0 is required])])

PKG_CHECK_MODULES([gtksourceview], [gtksourceview-4],
    [],
    [
        PKG_CHECK_MODULES([gtksourceview], [gtksourceview-3.0],
            [],
            [AC_MSG_ERROR([gtksourceview is required (either 3.0 or 4)])])
    ])
LIBS="$gtksourceview_LIBS $LIBS"
CFLAGS="$CFLAGS $gtksourceview_CFLAGS"

# Debug cflags
CFLAGS="$CFLAGS -O0 -g3 -Wall"

AC_OUTPUT
