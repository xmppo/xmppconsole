xmppconsole
===========

xmppconsole - a simple tool for XMPP hackers.

This tool sends raw XMPP stanzas over an XMPP connection and displays the XMPP
stream. Main purpose is to study XEPs and debug servers implementation.

The tool is under development, the finished version will support both GTK UI and
ncurses UI.

To try it out:
```
xmppconsole name@domain.com password
```
Where the first argument is your JID and the second is your password.

Build requirements
------------------

xmppconsole depends on 3 libraries:

* libstrophe
* gtk-3.0
* gtksourceview (either version 3.0 or 4)

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
./configure.sh
```
