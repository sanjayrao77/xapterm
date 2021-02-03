import config
import vte

def debug(s):
	vte.moveto(config.rows-1,0)
	vte.clearlines()
	vte.drawstring("Script output [ ")
	vte.drawstring(s)
	vte.drawstring(" ]")
def drawheaderright(row,text):
	vte.setcolors(0,15)
	vte.setunderline(1)
	col=int(config.columns-len(text))
	if col<0: col=0
	vte.moveto(0,col)
	vte.drawstring(text)
def drawheadercenter(row,text):
	vte.setcolors(0,15)
	vte.setunderline(1)
	col=int((config.columns-len(text))/2)
	if col<0: col=0
	vte.moveto(0,col)
	vte.drawstring(text)
def drawheaderleft(row,text):
	vte.setcolors(0,15)
	vte.setunderline(1)
	vte.moveto(0,0)
	vte.drawstring(text)
