import vte
import misc

list=[]

def add(seconds,fn):
	t=seconds+vte.time()
	list.append((t,fn))
	list.sort()
	vte.setalarm(list[0][0]-t)
def call(t):
	while 1:
		if not len(list): return
		if list[0][0] > t: break
		x=list.pop(0)
		x[1]()
	if len(list): vte.setalarm(list[0][0]-t)
