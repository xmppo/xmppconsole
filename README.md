xmppconsole
===========

xmppconsole - a simple tool for XMPP hackers.

This tool sends raw XMPP stanzas over an XMPP connection and displays the XMPP
stream. Main purpose is to study XEPs and debug servers implementation.

xmppconsole supports multiple UI modules: GTK, ncurses, console.

The tool is under development.

To try it out:
```
xmppconsole name@domain.com password
```
Where the first argument is your JID and the second is your password.

Build requirements
------------------

xmppconsole has only 1 required dependency:

* [libstrophe](https://github.com/strophe/libstrophe)

You will need the following dependencies in order to build optional UI modules.

For GTK graphical interface:

* gtk-3.0
* gtksourceview (either version 3.0 or 4)

For ncurses-based text interface:

* ncurses
* readline

Supported systems: Unix-like systems (including Linux, MacOS, BSDs), Windows
with Cygwin.

Build instructions
------------------

There is no release tarballs at this point. You will have to build xmppconsole
from sources using autotools.

```
git clone https://github.com/pasis/xmppconsole.git
cd xmppconsole
./autogen.sh
./configure
make
```
