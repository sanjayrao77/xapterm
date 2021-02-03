# xapterm

## Overview
Have you ever wanted to write scripts for your linux terminal? Maybe you remember DOS
modem programs that let you do this?

This lets you write python scripts to interact with your terminal session.
Scripts can watch for certain input and then run automatically, run on a timer,
or run when requested by you.

It's also just a simple terminal that's easy to modify if you're tired of bloated,
cumbersome terminals with slow menus.

You can write your own menus, shortcuts and modify other aspects of the terminal's
behavior from python. If you want custom behavior, a lot of it is implemented in
python so it's easy to tweak.

I want to have my terminal cache my ssh passwords so I don't have to retype them
over and over. I hit a key (^Q) when I'm at an ssh prompt and the script could
handle it for me.

I'd also like to have the brightness change with the time of day. That'll be easy to
do with a script as well.

I've added a list of ideas (Script ideas) below but I'm sure I'm just scratching
the surface with this.

## Building
You'll want a unix/X11 system with a lot of libraries and headers to build this.

I should make a list for raspbian (TODO). For a start, you'll want python development,
X11, Xext, Xft (freetype2) and fontconfig. If you have a problem compiling,
shoot me a message.

```
make
```

## Quick start

If you just compiled it and just want to try it out:
```bash
./xapterm -nobc user
```

If you want to install it:
```bash
mkdir ~/.config/xapterm
cp *.py ~/.config/xapterm
./xapterm -- -h
./xapterm
```

## Command line arguments
Usage
```bash
xapterm [-nobc] [-nopy] [-stderr] [-h] [scriptname] [python args...]
```
### -nobc : no byte code
*-nobc* disables python's \_\_pycache\_\_ directory and compilation caching.

### -nopy : disable python support
*-nopy* disables python support and saves some memory. Some functions will still
work, using the alternate _cscript.c_ code.

### -stderr : send stderr to caller instead of terminal
*-stderr* preserves the default stderr function and sends python output to the caller.

The default behavior is to trap python's output and show it in the terminal window so
the user can see it. Otherwise the output might never reach the user.

### -h : help
This prints some basic command line help.

### [scriptname]
By default, _xapterm_ will look for _user.py_ in ~/.config/xapterm and /usr/share/xapterm.

An alternate script can be specified here.

### [python args...]
Optional arguments can be provided here to be read by the python script. The default script
supports a number of arguments.

## Optional command line arguments
These arguments are read by the default _user.py_ script. They can be overwritten and
modified by the user in ~/.config/xapterm/user.py.

### -- -h : help
Running _xapterm -- -h_ will ask _user.py_ for help instead of _main.c_.

### -fast : fast start
This will run _fastbegin()_ which bypasses a lot of startup code and uses explicit
settings.

### -dyslexic : dyslexic fast start
This will run _dfastbegin()_ which is like _-fast_.

### -fs : fullscreen
This will attempt to go fullscreen. X11 servers are picky about this so it might not
work perfectly everywhere.

### -dark : dark mode
Start in a dark color palette.

### -light : light mode
Start in a light color palette.

### -noblink : don't blink the cursor
By default the cursor blinks. This doesn't.

### -dash : use dash shell
By default, bash is used. This uses lighter-weight dash instead.

### -term X : use TERM value X
Sensible answers for X are _ansi_ and _linux_. Other values will work too.

### -typeface X : use X for the font
For me, _monospace_, _freemono_, _liberationmono_, _notomono_, _droidsansmono100_ and _OpenDyslexicMono_ all
seem to work for X.

### -fontsize X : use X for the font size
Different fonts fit differently. You can try slightly larger or smaller numbers to get a fit you like more.
I've used Monospace with size 17 and been happy with it. I've also liked OpenDyslexicMono with size 15.

### -curheight X : use X for the height of the cursor
You can specify how tall you want the cursor. Note there are some escape sequences that will change it.

### -curcolor X : specify the cursor's color
I like _red_ for X. I find that works but you can change the color here.


## Script ideas
Here are a few ideas of what can be done to extend the functionality with scripts. Back in the day
I had scripts to play games for me (Tradewars!).

### Password manager
Included is _passwords.py_, a primitive password manager for ssh. This could be improved greatly.

### Color manager
It wouldn't be hard to calculate the appropriate brightness for the time of day and switch automatically
from light mode to dark more. The colors can be modified from python so this isn't hard.

### Typo highlighter
Incoming text can be watched from python. Common misspellings could be highlighted by a script.

### URL detector
Text that looks like URLs can be identified from scripts. They could be automatically copied to
the clipboard from a script.

## user.py

Most of the customization is done in user.py.

```
def OnInitBegin(*args)
```

*OnInitBegin* is the first procedure called. It can interact with the *config* module.
This can **not** interact with the *vte* module as nothing has been created yet. You can use *OnInitEnd* for *vte*
interaction.

*OnInitEnd* is the second procedure called, after data structures have been created. At this point,
the python code can start interacting with the *vte* and *texttap* modules.

There are a number of callbacks here:

### OnSuspend
This is called when the user suspends the session, for example with ctrl-s.

### OnResume
This is called with the user requests to resume the session, for example with ctrl-q.

### OnControlKey(key)
This is called when the session is paused and the user presses a control key combination.

This allows the script to get input from the user.

### OnKey(key)
This is called when the session is paused and the user presses a normal key.

This allows the script to get input from the user.

### OnPointer(etype,mods,button,row,col)

*etype*: 1 => button press, 2 => pointer movement, 3 => click, 4 => button release
*mods* (bitmask): &1 => shift is down, &2 => control, &4 => alt, &8 => numlock, &16 => appmode, &32 => scrollback mode, &64 => mouse is grabbed
*button*: 1 => left, 2 => middle, 3=> right, ...

This is called when the mouse is used but the use wasn't caught by the main code. There will be more events when the session is
paused.

### OnBell
This is called when the BEL (0x07) character is received. Older terminals used to beep. You could have python play a sound
but the current code flashes the screen. The flash can be customized here.

### OnAlarm(t)
*t*: The time requested
This is called when a user-specified timer expires. This allows the code to request a callback in the future.

### OnMessage(msg)
*msg*: text
This is called when an in-line message is received, e.g. a DCS like ESC+P+...+ESC+\\ will send the P+... to OnMessage().

### OnKeySymRelease(key,mods)
When a special key is released, see *OnKeySym*

### OnKeySym(key,mods)
*key*: of the form 0xff51, which is left cursor
*mods* (bitmask): &1 => shift is down, &2 => control, &4 => alt, &8 => numlock, &16 => appmode, &32 => scrollback mode, &64 => mouse is grabbed

This lets user.py send the control sequences for special characters. This allows full customization without recompiling.
For example, if 0xff51 is pressed (with no modifiers), OnKeySym() sends ESC+[+D, an escape sequence for left. If we're
in "app mode", we can see that in the *mods* variable and send ESC+[+OD instead.

### OnResize()
The user code is notified when the window changes size. This allows the user to
choose between a differently sized grid or differently sized fonts, or a combination.

## modules

There are three python modules for interacting with the terminal: *config*, *vte*, and *texttap*.

*config* lets python read and set basic configuration variables such as font size.

*vte* lets python interact with the terminal.

*texttap* lets python read characters or be notified when specified strings are received. This
is an enormous security concern so it is separate from *vte*. The only way (without hacking python)
for user.py to access texttap is by receiving it in *OnInitEnd* and passing it to code that uses it.

See below for a list of the modules.

## config module

### config variables
*config.cmdline* holds the command line

*config.darkmode* holds the dark mode colors

*config.lightmode* holds the light mode colors

*config.exportterm* holds the TERM value

*config.typeface* holds the font typeface

*config.windims* holds the window dimensions

*config.celldims* holds the size of a single character's cell size

*config.columns* holds the number of columns

*config.rows* holds the number of rows

*config.cursorheight* holds the height of the cursor

*config.cursoryoff* holds the y-offset for the cursor

*config.font0shift* holds the x-offset for drawing fonts

*config.font0line* holds the y-offset for drawing fonts

*config.fontulline* holds the y-offset for underlining fonts

*config.fontullines* holds the thickness of underlines

*config.charcache* holds the number of drawn characters to cache so they don't have to be drawn each time

*config.depth* holds the X11 color depth

*config.rgb\_cursor* holds the (r,g,b) tuple for the cursor color

*config.isfullscreen* holds the boolean value of whether fullscreen was requested

*config.isdarkmode* holds the boolean value if we're in dark mode (false => light mode)

*config.isblinkcursor* holds the boolean value if cursor is blinking

*config.isnostart* holds the boolean value of true if we're not going to start the terminal (e.g. just show help)

*config.screendims* holds the dimensions of the x11 screen in pixels

*config.mm_screendims* holds the dimensions of the x11 screen in millimeters

### config.apply()
Set the current configuration. Changes to config variables won't be used until this is called.

### config.queryfont(fontname)
This returns (ismatch,ascent,descent,width) as the information found about fontname.

You can try config.queryfont("monospace-17") as an example.

### config.issynched()
This is safe to ignore. It always returns true currently.

## vte module

### vte.clear()
Clears the screen

### vte.clearlines
*vte.clearlines()* Clear the current line

*vte.clearlines(row)* Clear the line at *row*

*vte.clearlines(row,count)* Clear lines starting at *row* and *count* lines from there

### vte.copy

*vte.copy(x)* copy *x* to the (PRIMARY) clipboard

*vte.copy(x,clipboard)* copy *x* to the (*clipboard*) clipboard

### vte.cursorheight

*vte.cursorheight()* reset cursor height

*vte.cursorheight(h)* set cursor height to *h* pixels

### vte.drawtoggle
*vte.drawtoggle()* toggles drawing state

*vte.drawtoggle(1)* enabled drawing

*vte.drawtoggle(0)* disables drawing

This is useful if you want the terminal to quickly process incoming text without slowing down to draw
to the screen. You can disable drawing and then re-enable it a few seconds later.

### vte.drawstring(string)

This draws *string* to the screen at the current cursor position.

### vte.fillpadding

*vte.fillpadding(color)*

*vte.fillpadding(color,ms)*

Draws *color* index in the window padding and waits *ms* milliseconds afterward.

There's often a small gap between the edge of the text screen and the x11 window. This happens because the window
isn't usually an integral size of the font size. We can draw in this gap for visual bells and similar effects.

### vte.fillrect

*vte.fillrect(x,y,width,height)*

*vte.fillrect(x,y,width,height,color)*

*vte.fillrect(x,y,width,height,color,ms)*

This fills the rectangle at *x*,*y* with *width* and *height* with an optional *color* index (or current color)
and optional sleep of *ms* milliseconds.


### vte.fetchline(row,col,count)

This returns a line from the screen at *row*, *col* and *count* characters. If the span exceeds the screen,
this will return an error code.


### vte.fetchcharpos()

This returns the position of the last character drawn by the terminal as a *(row,col)* pair.

### vte.grabpointer

*vte.grabpointer(1)* This grabs the pointer

*vte.grabpointer()*, *vte.grabpointer(0)* This releases the pointer

If the pointer is grabbed, more mouse events are sent to the python script. When it isn't grabbed, a lot
of events are either handled or not sent.

This allows the script to get user input from the mouse. Note that the *mods* variable in python callbacks will have the
grabbed bit set.

### vte.ispaused()

This returns true if the session is currently paused.

### vte.milliseconds()

This returns a count of milliseconds. It grows monotonically and lets the script measure elapsed time.

### vte.moveto

*vte.moveto()* This moves the cursor to the top left.

*vte.moveto(row)* This moves the cursor to *row*

*vte.moveto(row,col)* This moves the cursor to *row* and *col*


### vte.movewindow

*vte.movewindow()* This moves the window to the top left.

*vte.movewindow(x)* This moves the window to (x,0)

*vte.movewindow(x,y)* This moves the window to (x,y)

### vte.paste

*vte.paste()* This pastes text from the (PRIMARY) clipboard.

*vte.paste(clipboard)* This pastes text from the *clipboard* clipboard.

### vte.pause()

This pauses the terminal. See vte.unpause().

### vte.restoretext()

This restores the drawn screen from a backup. See vte.savetext().

### vte.restorerect(x,y,width,height)

This forces a redraw of the screen *(x,y,width,height)*

I'm not sure what this is useful for but it's here.

### vte.savetext()

This saves the drawn screen to a backup. See vte.restoretext().

This lets you overdraw the screen and then undo your changes.

### vte.scrollback(n)

This scrolls back *n* lines.

### vte.select

*vte.select(row_start,col_start,row_stop,col_stop)* This copies a portion of the screen to the (PRIMARY) clipboard.

*vte.select(row_start,col_start,row_stop,col_stop,clipboard)* As above but to *clipboard* clipboard.

### vte.send(chars)

Sends *chars* as if they were typed.

### vte.setalarm(seconds)

Sets an alarm for *seconds* seconds. *OnAlarm* will be called in user.py.

### vte.setcolors

*vte.setcolors()* This sets the foreground and background colors to defaults.

*vte.setcolors(fore)* This sets the foreground color to *fore* color index.

*vte.setcolors(fore,back)* This sets the foreground and background colors.

### vte.setcursor()

This enables the cursor.

### vte.setpointer

*vte.setpointer()* Sets the mouse pointer to default

*vte.setpointer(index)* Sets the mouse pointer to X11 number *index*.

You can easily write a script to cycle through the possible index values.
I find 60 is a nice value.

### vte.settitle(text)

Sets the window title to *text*.

### vte.setunderline

*vte.setunderline(0)* Disables

*vte.setunderline(1)* Enables underlining in the current style

### vte.stderr(text)

This sends *text* to the terminal's stderr output.

### vte.stdout(text)

This sends *text* to the terminal's stdout output.

### vte.time()

This returns the seconds since the epoch.

### vte.unpause()

See vte.pause(), this unpauses the session.

### vte.unsetcursor()

This hides the cursor. See vte.setcursor().

These are useful when interacting with the user.

### vte.visualbell

*vte.visualbell()* Uses color 0 and 150 ms.

*vte.visualbell(color)* Uses color *color* and 150 ms

*vte.visualbell(color,ms)* Uses color *color* and *ms* milliseconds

This flashes the screen. It's obnoxious. You're better off writing your own
with the functions available.

### vte.xbell(percent)

Without a parameter, it uses 0 percent.

This calls XBell() in the X11 server but on modern systems, this doesn't do anything.

If you actually want to make sound, you might be better off playing a wave file (or similar)
via python.

## texttap module
### texttap.addlook(object,id,match)
Set a callback for a specified input.

*object.OnLook(id)* will be called when *match* is found.

If *match* is empty, everything will match.

If *object* is destroyed, the tap will be deleted.

### texttap.rmlook(object,id)
This removes the tap created by addlook().


## python script files

### user.py

This is the main python script. All callbacks go through here.

### alarms.py

This is a small manager for alarms. The terminal only supports one
alarm at a time, so this code sequences multiple alarms and does
the work to support multiple alarms.

### menus.py

This has a *Menu* class as well as *MainMenu*, *BrightnessMenu*
and *FontMenu*. They can all be edited by the user.

### misc.py

This has some helper functions for drawing and debugging.

### passwords.py

This is a work-in-progress password manager. It can be
easily disabled in user.py if it's not wanted.
