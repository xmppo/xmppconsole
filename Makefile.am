AM_CFLAGS = -I$(top_srcdir)/src

bin_PROGRAMS = xmppconsole

xmppconsole_SOURCES = \
	src/list.c \
	src/ui.c \
	src/ui_console.c \
	src/ui_gtk.c \
	src/ui_ncurses.c \
	src/xmppconsole.c

xmppconsole_SOURCES += \
	src/list.h \
	src/misc.h \
	src/ui.h \
	src/ui_console.h \
	src/ui_gtk.h \
	src/ui_ncurses.h \
	src/xmpp.h

xmppconsole_CFLAGS = $(AM_CFLAGS)
xmppconsole_LDFLAGS =

man_MANS = docs/xmppconsole.1

EXTRA_DIST = \
	$(man_MANS) \
	autogen.sh \
	LICENSE \
	README.md
