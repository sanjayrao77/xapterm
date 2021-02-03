CFLAGS=-Wall -g -I/usr/include/freetype2
# CFLAGS=-Wall -O3 -I/usr/include/freetype2
# CFLAGS=-Wall -O2 -g -I/usr/include/freetype2 -DUSE_SAFEMEM
all: xapterm
xapterm: main.o config.o x11info.o xftchar.o charcache.o pty.o event.o xclient.o surface.o vte.o cursor.o script.o cscript.o keysym.o xclipboard.o common/blockmem.o common/texttap.o
	${CC} -o $@ $^ -lX11 -lXext -lfontconfig -lXft -lutil $(shell python3-config --libs) # -lpython3.7m
test: main-test.o config.o x11info.o xftchar.o charcache.o pty.o event.o xclient.o surface.o vte.o cursor.o script.o cscript.o keysym.o xclipboard.o common/blockmem.o common/texttap.o
	${CC} -o $@ $^ -lX11 -lXext -lfontconfig -lXft -lutil $(shell python3-config --libs) # -lpython3.7m
script.o: script.c
	${CC} -o $@ -c $^ ${CFLAGS} $(shell python3-config --includes) # -I/usr/include/python3.7m
main-test.o: main.c
	${CC} -o $@ -c $^ ${CFLAGS} -DTEST
upload: clean
	scp -pr * monitor:src/terminal/
upload2: clean
	scp -pr * tvroom:src/terminal/
backup: clean
	tar -jcf - . | jbackup src.terminal.tar.bz2
clean:
	rm -f xapterm test *.o common/*.o core __pycache__/*.pyc
.PHONY: clean backup
