import config
import vte
import misc
import menus
import alarms
import passwords

# backup of colors for palette commands
lights_global=0
darks_global=0

stopwatch_global=0
mainmenu_global=menus.MainMenu()
brightness_global=menus.BrightnessMenu(mainmenu_global)
fontmenu_global=menus.FontMenu(mainmenu_global)

def unhex(h): return int(h,base=16)

class KMInput():
	def noop(*k): pass
	def __init__(self):
		self.kcb=self.noop
		self.ckcb=self.noop
		self.pcb=self.noop
		self.ispcb=0
	def register(self,keycb,controkeycb,pointercb):
		if keycb: self.kcb=keycb
		if controkeycb: self.ckcb=controkeycb
		if pointercb:
			self.ispcb=1
			self.pcb=pointercb
			vte.grabpointer(1)
	def clear(self):
		vte.grabpointer()
		self.__init__()
	def onkey(self,code): self.kcb(code)
	def oncontrolkey(self,code): self.ckcb(code)
	def onpointer(self,etype,mods,button,row,col): self.pcb(etype,mods,button,row,col)
kminput_global=KMInput()

class Tap():
	def __init__(self):
		pass
	def OnLook(self,code):
		vte.stderr("tap.onlook: "+str(code))
tap=Tap()

def fastbegin():
	config.typeface="monospace-17"
	config.celldims=(14,24)
	config.cursorheight=9
	config.cursoryoff=15
	if config.isfullscreen:
		config.windims=(1920,1080)
		config.columns=137
		config.rows=45
	else:
		config.windims=(960,528)
		config.columns=68
		config.rows=22

def dfastbegin():
	fastbegin()
	config.typeface="OpenDyslexicMono-15"
	config.font0line=18 # the ascent/descent numbers for OpenDyslexicMono-15 are huge, overriding the ascent here


def parsehexcolor2(a,b): return unhex(a)*16+unhex(b)
def parsehexcolor(arg): return (parsehexcolor2(arg[0],arg[1]),parsehexcolor2(arg[2],arg[3]),parsehexcolor2(arg[4],arg[5]))
def parsecolor(arg):
	if arg[0]=='#':
		if len(arg)==7: return parsehexcolor(arg[1:])
		if len(arg)==4: return parsehexcolor(arg[1]+arg[1]+arg[2]+arg[2]+arg[3]+arg[3])
	if arg=="red": return (255,0,0)
	elif arg=="green": return (0,255,0)
	elif arg=="blue": return (0,0,255)
	elif arg=="black": return (0,0,0)
	elif arg=="white": return (255,255,255)
	elif len(arg)==3: return parsehexcolor(arg[0]+arg[0]+arg[1]+arg[1]+arg[2]+arg[2])
	return (255,255,255)

def findtypeface(typeface,fontsize,dims):
	# OpenDyslexicMono dimensions aren't auto-detected well, see dfastbegin() to set it manually
	if typeface!=None and fontsize!=None: return typeface+'-'+fontsize
	if typeface==None:
		typeface="monospace"
		for i in ("monospace","freemono","liberationmono","notomono","droidsansmono100"):
			(ismatch,ascent,descent,width)=config.queryfont(i)
			if ismatch:
				typeface=i
				break
	if fontsize==None:
		i=5
		while True:
			(ismatch,ascent,descent,width)=config.queryfont(typeface+'-'+str(i))
			if width>dims[0]: break
			if ascent+descent>dims[1]: break
			i+=1
		fontsize=str(i-1)
	return typeface+'-'+fontsize

def OnInitBegin(*args):
	typeface=None
	fontsize=None
	curheight=0
	nargs=len(args)
	i=0
	while i < nargs:
		a=args[i]
		if a=="-fast": return fastbegin()
		elif a=="-dyslexic": return dfastbegin()
		elif a=="-fs": config.isfullscreen=1
		elif a=="-dark": config.isdarkmode=1
		elif a=="-light": config.isdarkmode=0
		elif a=="-noblink": config.isblinkcursor=0
		elif a=="-dash": config.cmdline=["dash","-i"]
		elif a=="--": pass
		elif a=="-h":
			vte.stdout("Usage: xapterm [-nobc] [-nopy] [-h] [scriptname] [python args...]\n"\
			"-nobc   : disable python's __pycache__ litter\n"\
			"-nopy   : disable python support to save some memory\n"\
			"-stderr : send python's output to caller instead of terminal\n"\
			"-h      : this help, of sorts\n"\
			"scriptname  : filename containing python code for terminal\n"\
			"python args : one or more arguments to send to OnInitBegin() in script\n"\
			"\t-fast     :  quick start with set values\n"\
			"\t-fs       :  start full-screen\n"\
			"\t-dark     :  start in dark color scheme\n"\
			"\t-light    :  start in light color scheme\n"\
			"\t-noblink  :  don't blink the cursor\n"\
			"\t-dash     :  run dash instead of bash\n"\
			"\t-term X     : set TERM, \"ansi\" and \"linux\" work\n"\
			"\t-typeface X : set the font\n"\
			"\t-fontsize X : set the font size\n"\
			"\t-curheight X  : set the cursor's height\n"\
			"\t-curcolor X   : set the cursor's color (e.g., \"red\")\n")
			config.isnostart=1
		else:
			i+=1
			na=args[i] if i!=nargs else ""
			if a=="-term": config.exportterm=na
			elif a=="-typeface": typeface=na
			elif a=="-fontsize": fontsize=na
			elif a=="-curheight": curheight=int(na)
			elif a=="-curcolor": config.rgb_cursor=parsecolor(na)
			else:
				vte.stderr("Ignoring argument "+a)
				continue
		i+=1
	config.celldims=(round(config.screendims[0]/config.mm_screendims[0]*3.7),\
			round(config.screendims[1]/config.mm_screendims[1]*7)) # (width,height), (14,27) is default

	config.typeface=findtypeface(typeface,fontsize,config.celldims) # monospace-17 is a reasonable choice

	if not curheight: config.cursorheight=round(config.celldims[1]*.4) # height of fullest cursor: 11 is default
	else: config.cursorheight=min(curheight,config.celldims[1])
	config.cursoryoff=config.celldims[1]-config.cursorheight # where to place cursor, 15 is default
	# config.font0shift=0 # xoff to draw characters, -1 is default
	# config.font0line=int(.77*config.celldims[1]) # yoff: baseline to draw characters, -1 is default
	# config.fontulline=int(.85*config.celldims[1]) # where underline should start, -1 is default
	# config.fontullines=int(.08*config.celldims[1]) # thickness of underline, -1 is default

	if config.isfullscreen: config.windims=config.screendims
	else: config.windims=(int(config.screendims[0]/2),int(config.screendims[1]/2))

	config.columns=int(config.windims[0]/config.celldims[0])
	config.rows=int(config.windims[1]/config.celldims[1])

	config.charcache=200 # number of drawn characters to cache, 200 is default

def checkissynched():
	vte.stderr("config.issynched: "+str(config.issynched()))
	return 0

def increasewindow():
	config.typeface="monospace-18"
	config.celldims=(15,28)
	config.windims=(15*68,28*20)
	config.apply()
	return 0

def testsurface():
	config.columns=50
	config.rows=18
	config.windims=(8+14*config.columns,27*config.rows)
	config.apply()
	return 0

def testcopy():
	vte.copy("PRIMARY","Hello world, this is user.py\n")

def testpaste():
	t=vte.paste()
	vte.send(t)

def cleartap():
	global tap
	tap=Tap()
	return 0

def testselect():
	alarms.add(0,lambda: vte.select(0,0,0,20))

def OnInitEnd(texttap):
	global lights_global,darks_global
	lights_global=config.lightmode[:]
	darks_global=config.darkmode[:]

	mainmenu_global.setup(kminput_global,alarms)
	brightness_global.setup(kminput_global)
	fontmenu_global.setup(kminput_global)

	passwords.init(kminput_global)
	mainmenu_global.add('*',"brightness menu",brightness_global.draw)
	mainmenu_global.add('f',"font menu",fontmenu_global.draw)
#	mainmenu_global.add('x',"check issynched",checkissynched)
	mainmenu_global.add('z',"increase window",increasewindow)
	mainmenu_global.add('t',"clear text taps",cleartap)
#	mainmenu_global.add('v',"test surface resize",testsurface)
#	mainmenu_global.add('c',"copy text to clipboard",testcopy)
	mainmenu_global.add('p',"paste text from clipboard",testpaste)
#	mainmenu_global.add('s',"select text test",testselect)

	vte.movewindow(0,0)
#	alarms.add(1,sayhi)
#	texttap.addlook(tap,1,"burp")

def OnSuspend():
	if vte.ispaused():
		OnResume()
		return
	vte.pause()
	vte.savetext()
	misc.drawheaderright(0,"VTE Suspended, Press ^q or ^s to Resume")


def startmenu():
	vte.pause()
	vte.savetext()
	mainmenu_global.draw()

def OnResume():
	if vte.ispaused():
		vte.restoretext()
		vte.unpause()
		return
	if passwords.isactivated(): return
	startmenu()

def OnControlKey(key): kminput_global.oncontrolkey(key)
def OnKey(key): kminput_global.onkey(key)
def OnPointer(etype,mods,button,row,col):
# etype: 1=>press,2=>move,3=>click,4=>release
	if kminput_global.ispcb: return kminput_global.onpointer(etype,mods,button,row,col)
	if etype!=3: return
	mods=mods&15
	if not mods:
		if button==2: vte.send(vte.paste())
		elif button==3: OnResume()
		elif button==4: vte.scrollback(1) # mouse Up Button
		elif button==5: vte.scrollback(-1) # mouse Down Button
	if mods&1: # shift
		if button==4: vte.scrollback(10)
		elif button==5: vte.scrollback(-10)

def OnBell():
	row,col=vte.fetchcharpos()
	yoff= 0 if row > config.rows/2 else int(config.celldims[1]*(config.rows-1)) 
	vte.fillrect(0,yoff,config.windims[0],config.celldims[1],5,50) # blink first or last line
	vte.restorerect(0,yoff,config.windims[0],config.celldims[1])

#	vte.fillpadding(5,0) # blink window margin, if any
#	vte.fillpadding()

#	vte.visualbell(5,50) # flash text area

#	vte.xbell(100) # ring X11 beeper, if any


def OnAlarm(t):
	alarms.call(t)

def palette(cmd):
	if len(cmd)!=7: return
	p=config.darkmode if config.isdarkmode else config.lightmode
	p[unhex(cmd[0])]=(unhex(cmd[1:3]), unhex(cmd[3:5]), unhex(cmd[5:7]))
	config.apply()
def reset_palette():
	config.lightmode=lights_global[:]
	config.darkmode=darks_global[:]
	config.apply()

def csiquestion(tail):
	if tail[-1]=='h':
		if tail=="25h": vte.cursorheight() # unhide cursor
	elif tail[-1]=='l':
		if tail=="25l": vte.cursorheight(0) # hide cursor
	elif tail[-1]=='c':
		code=tail[0]
		ordcode=ord(code)
		if code=='0': vte.cursorheight() # reset cursor
		elif code=='1': vte.cursorheight(0) # hide cursor
		elif ordcode>49 and ordcode<=57: vte.cursorheight(int(((ordcode-49)/8) * config.celldims[1]))

def OnMessage(msg):
	global stopwatch_global
	if msg[0]=='_':
		if msg == "__startwatch": stopwatch_global=vte.milliseconds()
		elif msg == "__stopwatch": vte.stderr("Elapsed ms: "+str(vte.milliseconds()-stopwatch_global))
		else: vte.settitle(msg[1:])
	elif msg[0]==']':
		if msg[1:2] == '0;': vte.settitle(msg[3:])
		elif msg[1] == 'P': palette(msg[2:]) # linux term has ESC]Pxrrggbb command to set a color
		elif msg[1] == 'R': reset_palette() # linux term has ESC]R to reset colors
	elif msg[0:2] == '[?': csiquestion(msg[2:]) # linux term has ESC[?Xc, for X in 0..9, to control cursor
	else: vte.stderr("Message received: "+msg)

def OnKeySymRelease(key,mods):
#	vte.stderr("Release { key:"+hex(key)+", mods:"+hex(mods)+" }")
	if mods==4:
		if key==0xffea: vte.drawtoggle(1) # Right Alt: release to resume drawing input

def OnKeySym(key,mods):
# mods&1=>shift, mods&2=>control, mods&4=>alt, mods&8=>numlock, mods&16=>appmode mods&32=>scrollback mods&64=>grabbed
#	vte.stderr('key: '+hex(key)+', mods: '+str(mods))
	if not mods:
		if key==0xff51: vte.send('[D') # Left
		elif key==0xff52: vte.send('[A') # Up
		elif key==0xff53: vte.send('[C') # Right
		elif key==0xff54: vte.send('[B') # Down
		elif key==0xff50: vte.send('[H') # Home
		elif key==0xff57: vte.send('[F') # End
		elif key==0xffea: vte.drawtoggle() # Right Alt: stop drawing input, release to resume
	elif mods==16:
		if key==0xff51: vte.send('OD') # Left
		elif key==0xff52: vte.send('OA') # Up
		elif key==0xff53: vte.send('OC') # Right
		elif key==0xff54: vte.send('OB') # Down
		elif key==0xff50: vte.send('OH') # Home
		elif key==0xff57: vte.send('OF') # End
	elif mods&3==3: # Shift+Control
		if key==0x56: vte.send(vte.paste()) # ctrl-V
	elif mods&2: # Control
		if key==0xff52: vte.scrollback(int(config.rows*.8)) # Ctrl-Up
		elif key==0xff54: vte.scrollback(-int(config.rows*.8)) # Ctrl-Down
	elif mods&1: # Shift
		if key==0xff52: vte.scrollback(1) # Shift-Up
		elif key==0xff54: vte.scrollback(-1) # Shift-Down
		elif key==0xffbe: brightness_global.step(-5)
		elif key==0xffbf: brightness_global.step(5)
	elif mods&32: # scrollback
		if key==0xff52: vte.scrollback(1) # Up
		elif key==0xff54: vte.scrollback(-1) # Down

def OnResize():
	# can choose to change font size and/or change rows/cols
#	vte.stderr("OnResize: "+str(config.windims))
	config.columns=int(config.windims[0]/config.celldims[0])
	config.rows=int(config.windims[1]/config.celldims[1])
	config.apply()
