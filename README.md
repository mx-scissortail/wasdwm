wasdwm - wasd wm / was dwm
============================
wasdwm is an extremely fast, small, and dynamic window manager for X.


Requirements
------------
In order to build wasdwm you need the Xlib header files.


Installation
------------
Edit config.mk to match your local setup (wasdwm is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install wasdwm (if
necessary as root):

    make clean install

Running wasdwm
-----------
Add the following line to your .xinitrc to start wasdwm using startx:

    exec wasdwm

In order to connect wasdwm to a specific display, make sure that
the DISPLAY environment variable is set correctly, e.g.:

    DISPLAY=foo.bar:1 exec wasdwm

(This will start wasdwm on display :1 of the host foo.bar.)

In order to display status info in the bar, you can do something
like this in your .xinitrc:

    while xsetroot -name "`date` `uptime | sed 's/.*,//'`"
    do
    	sleep 1
    done &
    exec wasdwm


Configuration
-------------
The configuration of wasdwm is done by creating a custom config.h
and (re)compiling the source code.
