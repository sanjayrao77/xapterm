import config
import vte
import misc

def listsort(v): return v[0]

class Menu():
	def __init__(self):
		self.list=[]
		self.isactive=0
		self.keyboard=None
		self.title=""
		self.pointercode=0
		self.issorted=0
	def add(self,letter,text,fn): self.list.append([letter,text,fn,0,0,0])
	def setpointer(self,c):
		if self.pointercode==c: return
		self.pointercode=c
		vte.setpointer(c)
	def exit(self):
		self.isactive=1
		if self.keyboard!=None: self.keyboard.clear()
		vte.restoretext()
		self.setpointer(0)
		vte.unpause()
	def onkey(self,code):
		for it in self.list:
			if ord(it[0])==code:
				r=it[2]()
				if r==None: return self.exit()
				return
		if code==27: return self.exit()
		misc.debug("Unhandled key: "+str(code))
	def onpointer(self,etype,mods,button,row,col):
		if etype==2:
			for it in self.list:
				if row!=it[3]: continue
				if col<it[4]: continue
				if col>it[5]: continue
				return self.setpointer(60)
			self.setpointer(0)
		elif etype==3: # Click
			if button==3: return self.exit()
			if button==1:
				for it in self.list:
					if row!=it[3]: continue
					if col<it[4]: continue
					if col>it[5]: continue
					r=it[2]()
					if r==None: return self.exit()
					return
	def sort(self):
		self.list.sort(key=listsort)
		self.issorted=1
	def draw(self):
		self.isactive=1
		if not self.issorted: self.sort()
		self.keyboard.register(self.onkey,None,self.onpointer)
		r=2
		vte.unsetcursor()
		vte.clear()
		misc.drawheaderleft(0,self.title)
		for it in self.list:
			it[3]=r
			it[4]=1
			it[5]=1+2+len(it[1])
			vte.moveto(r,1); r+=1
			vte.setcolors(15,0)
			vte.setunderline(0)
			vte.drawstring(it[0])
			vte.drawstring(": ")
			vte.setcolors(4,0)
			vte.setunderline(1)
			vte.drawstring(it[1])
		return 0

class MainMenu(Menu):
	def __init__(self):
		super().__init__()
		self.add('b',"blue cursor",self.bluecursor)
		self.add('g',"green cursor",self.greencursor)
		self.add('r',"red cursor",self.redcursor)
		self.add('P',"pointer test",self.pointertest)
		self.add('q',"unpause",self.unpause)
		self.title=" Main Menu (q to exit) "
	def setup(self,keyboard,alarms):
		self.keyboard=keyboard
		self.alarms=alarms
	def unpause(self): pass
	def setcursorcolor(r,g,b):
		config.rgb_cursor=(r,g,b)
		config.apply()
	def bluecursor(self): setcursorcolor(0,0,255)
	def redcursor(self): setcursorcolor(255,0,0)
	def greencursor(self): setcursorcolor(0,255,0)
	def pointertest(self):
		if not self.isactive: return
		self.pointercode=(self.pointercode+2)%154
		misc.debug("Pointer code: "+str(self.pointercode))
		vte.setpointer(self.pointercode)
		self.alarms.add(2,self.pointertest)
		return 0

class BrightnessMenu(Menu):
	def __init__(self,mainmenu):
		super().__init__()
		self.add('q',"Back",mainmenu.draw)
		self.add('-',"switch darkmode",self.switchdarkmode)
		self.add('0',"100% brightness",lambda:self.setbrightness(100))
		self.add('9',"90% brightness",lambda:self.setbrightness(90))
		self.add('8',"80% brightness",lambda:self.setbrightness(80))
		self.add('7',"70% brightness",lambda:self.setbrightness(70))
		self.add('6',"60% brightness",lambda:self.setbrightness(60))
		self.add('5',"50% brightness",lambda:self.setbrightness(50))
		self.add('4',"40% brightness",lambda:self.setbrightness(40))
		self.add('3',"30% brightness",lambda:self.setbrightness(30))
		self.add('2',"20% brightness",lambda:self.setbrightness(20))
		self.add('1',"10% brightness",lambda:self.setbrightness(10))
		self.title=" Brightness Menu "
		self.lastpct=100
	def setup(self,keyboard):
		self.lightmode=config.lightmode[:]
		self.darkmode=config.darkmode[:]
		self.keyboard=keyboard
	def switchdarkmode(self):
		config.isdarkmode^=1
		config.apply()
	def setbrightness(self,pct):
		self.lastpct=pct
		for i in range(16):
			config.lightmode[i]=(int(self.lightmode[i][0]*pct/100),int(self.lightmode[i][1]*pct/100),int(self.lightmode[i][2]*pct/100))
			config.darkmode[i]=(int(self.darkmode[i][0]*pct/100),int(self.darkmode[i][1]*pct/100),int(self.darkmode[i][2]*pct/100))
	#		vte.stderr(str(i)+": "+("%x"%config.darkmode[i][0])+("%x"%config.darkmode[i][1])+("%x"%config.darkmode[i][2]))
		config.apply()
	def step(self,pct): self.setbrightness(min(max(self.lastpct+pct,0),100))

class FontMenu(Menu):
	def __init__(self,mainmenu):
		super().__init__()
		self.add('q',"Back",mainmenu.draw)
		self.add('-',"font size down",self.fontdown)
		self.add('+',"font size up",self.fontup)
		self.add('1',"Liberation Mono",lambda:self.setfont("liberationmono"))
		self.add('2',"FreeMono",lambda:self.setfont("freemono"))
		self.add('3',"Noto Mono",lambda:self.setfont("notomono"))
		self.add('4',"Droid Sans Mono",lambda:self.setfont("droidsansmono100"))
		self.add('5',"Open Dyslexic Mono",lambda:self.setfont("OpenDyslexicMono"))
		self.title=" Font Menu "
	def setup(self,keyboard): self.keyboard=keyboard
	def setboth(self,a):
		config.typeface='-'.join(a)
		config.apply()
		misc.debug("new typeface: "+config.typeface)
		return 0
	def setfont(self,name):
		a=config.typeface.split('-')
		a[0]=name
		return self.setboth(a)
	def fontup(self):
		a=config.typeface.split('-')
		a[1]=str(int(a[1])+1)
		return self.setboth(a)
	def fontdown(self):
		a=config.typeface.split('-')
		s=int(a[1])
		if s: a[1]=str(s-1)
		return self.setboth(a)
