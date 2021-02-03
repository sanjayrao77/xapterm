import config
import vte
import misc

passwords={}
newpass=0
keyboard=0

class Dialog():
	def __init__(self,keyboard,row,col,title1,title2,cb,ccb):
		self.input=''
		self.title1=title1
		self.title2=title2
		self.titlelen=max(len(title1),len(title2))
		self.cb=cb
		self.ccb=ccb
		keyboard.register(self.onkey,self.oncontrolkey,None)

		col=int((config.columns-4-self.titlelen)/2)
		if row+6 > config.rows: row=config.rows-6

		self.row=row
		self.col=col

		vte.unsetcursor()

		vte.setcolors(0,15)
		vte.setunderline(0)
		bl=' '*(self.titlelen+4)
		for i in range(6):
			vte.moveto(row+i,col)
			vte.drawstring(bl)
		vte.moveto(row+1,col+2)
		vte.drawstring(self.title1)
		vte.moveto(row+2,col+2)
		vte.drawstring(self.title2)
		vte.setcolors(15,0)
		vte.moveto(row+4,col+2)
		vte.drawstring(' '*self.titlelen)
		vte.moveto(row+4,col+2)
		vte.setcursor()
	def oncontrolkey(self,code):
		if code==117:
			vte.unsetcursor()
			self.input=''
			vte.moveto(self.row+4,self.col+2)
			vte.drawstring(' '*self.titlelen)
			vte.moveto(self.row+4,self.col+2)
			vte.setcursor()
	def onkey(self,code):
		if code==8:
			if not len(self.input): return
			vte.unsetcursor()
			self.input=self.input[:-1]
			vte.moveto(self.row+4,self.col+2+len(self.input))
			vte.drawstring(' ')
			vte.moveto(self.row+4,self.col+2+len(self.input))
			vte.setcursor()
		elif code==27:
			keyboard.clear()
			self.ccb()
		elif code==13:
			keyboard.clear()
			self.cb()
		else:
			if len(self.input)<self.titlelen:
				vte.drawstring('*')
				vte.setcursor()
			self.input+=chr(code)

class Newpass():
	def __init__(self,login,row,col):
		vte.pause()
		vte.savetext()
		self.login=login
		self.dlg=Dialog(keyboard,row,col,"Enter password for",login,self.dialogdone,self.dialogcancel)
	def dialogdone(self):
		p=self.dlg.input
		passwords[self.login]=p
		vte.send(p)
		vte.send('\n')
		vte.restoretext()
		vte.unpause()
	def dialogcancel(self):
		vte.restoretext()
		vte.unpause()

def init(kb_in):
	global keyboard
	keyboard=kb_in

def isactivated():
	global newpass
	row,col = vte.fetchcharpos()
	if col>16:
		s = vte.fetchline(row,col-12,13)
		if s=="'s password: ":
			s=vte.fetchline(row,0,col-12)
			if s in passwords:
				vte.send(passwords[s])
				vte.send('\n')
			else:
				newpass=Newpass(s,row,col)
			return 1

