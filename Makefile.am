AM_CFLAGS = -I$(top_srcdir)/src

bin_PROGRAMS = xmppconsole

xmppconsole_SOURCES = \
	src/ui.c \
	src/ui_console.c \
	src/ui_gtk.c \
	src/xmppconsole.c

xmppconsole_SOURCES += \
	src/ui.h \
	src/ui_console.h \
	src/ui_gtk.h \
	src/xmpp.h

xmppconsole_CFLAGS = $(AM_CFLAGS)
xmppconsole_LDFLAGS =

EXTRA_DIST = \
	autogen.sh \
	LICENSE \
	README.md
