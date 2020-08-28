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

#
# Ncurses UI module
#

AC_ARG_ENABLE([ncurses],
    [AS_HELP_STRING([--enable-ncurses], [Enable ncurses UI module])],
    [],
    [enable_ncurses=auto])

AS_IF([test "$enable_ncurses" != "no"],
    [PKG_CHECK_MODULES([ncursesw], [ncursesw],
        [
            LIBS="$ncursesw_LIBS $LIBS"
            CFLAGS="$CFLAGS $ncursesw_CFLAGS"
        ],
        [AS_IF([test "$enable_ncurses" = "yes"],
            [AC_MSG_ERROR([ncursesw is required for ncurses module])],
            [enable_ncurses=no])])
    ])

AS_IF([test "$enable_ncurses" != "no"],
    [PKG_CHECK_MODULES([readline], [readline],
        [
            LIBS="$readline_LIBS $LIBS"
            CFLAGS="$CFLAGS $readline_CFLAGS"
        ],
        [
            AC_MSG_CHECKING([for -lreadline])
            LIBS_OLD="$LIBS"
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
                [AS_IF([test "$enable_ncurses" = "yes"],
                    [AC_MSG_ERROR([readline is required ncurses module])],
                    [
                        enable_ncurses=no
                        LIBS="$LIBS_OLD"
                    ])
                ])
        ])
    ])

AS_IF([test "$enable_ncurses" != "no"],
    [enable_ncurses=yes])

#
# GTK UI module
#

AC_ARG_ENABLE([gtk],
  [AS_HELP_STRING([--enable-gtk], [Enable GTK UI module])],
  [],
  [enable_gtk=auto])

AS_IF([test "$enable_gtk" != "no"],
    [PKG_CHECK_MODULES([gtk], [gtk+-3.0],
        [
            LIBS="$gtk_LIBS $LIBS"
            CFLAGS="$CFLAGS $gtk_CFLAGS"
        ],
        [AS_IF([test "$enable_gtk" = "yes"],
            [AC_MSG_ERROR([gtk+-3.0 is required for GTK module])],
            [enable_gtk=no])])
    ])

AS_IF([test "$enable_gtk" != "no"],
    [PKG_CHECK_MODULES([gtksourceview], [gtksourceview-4],
        [],
        [
            PKG_CHECK_MODULES([gtksourceview], [gtksourceview-3.0],
                [],
                [AS_IF([test "$enable_gtk" = "yes"],
                    [AC_MSG_ERROR([gtksourceview (either 3.0 or 4) is required for GTK module])],
                    [enable_gtk=no])])
        ])
    ])
AS_IF([test "$enable_gtk" != "no"],
    [
        LIBS="$gtksourceview_LIBS $LIBS"
        CFLAGS="$CFLAGS $gtksourceview_CFLAGS"
    ])

AS_IF([test "$enable_gtk" != "no"],
    [enable_gtk=yes])

#
# Check for enabled modules
#

AS_IF([test "$enable_ncurses" = "yes"], [AC_DEFINE([BUILD_UI_NCURSES], [1], [Enable ncurses UI module])])
AS_IF([test "$enable_gtk" = "yes"], [AC_DEFINE([BUILD_UI_GTK], [1], [Enable GTK UI module])])

# Debug cflags
CFLAGS="$CFLAGS -O0 -g3 -Wall"

AC_OUTPUT

echo
echo "Modules support"
echo "  Ncurses UI module: $enable_ncurses"
echo "  GTK UI module: $enable_gtk"
