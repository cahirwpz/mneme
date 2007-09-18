CC		=	libtool --mode=compile gcc -g -static -fms-extensions
INCLUDE	=	-I./tests -I./tests/sysdeps/pthread -I./tests/sysdeps/generic 
DEFS	=	-DPM_USE_SBRK -DPM_USE_MMAP -DPM_USE_SHM -DVERBOSE=1 -DDEADMEMORY
CFLAGS	=	-O -Wall $(DEFS) $(INCLUDE)

LD		=	libtool --mode=link gcc -g -static 
LDFLAGS	=	-rpath /usr/local/lib -lrt

OBJS	=	memman-ao.lo areaman.lo blkman-ao.lo sysmem-mmap.lo sysmem-sbrk.lo sysmem-shm.lo

all:	libmymalloc.la tst-random tests/t-test1 tests/t-test2

libmymalloc.la:	$(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(patsubst %.o,%.lo,$^)

tst-random:	tst-random.lo libmymalloc.la
	$(LD) $(LDFLAGS) -o $@ $^

tests/t-test1:		tests/t-test1.lo ldwrapper.lo libmymalloc.la
	$(LD) $(LDFLAGS) -o $@ $^

tests/t-test2:		tests/t-test2.lo ldwrapper.lo libmymalloc.la
	$(LD) $(LDFLAGS) -o $@ $^

%.lo: %.o
	@

areaman.o:			areaman.c areaman.h common.h sysmem.h
blkman-ao.o: 		blkman-ao.c blkman-ao.h common.h areaman.h sysmem.h
ldwrapper.o: 		ldwrapper.c memman-ao.h common.h areaman.h sysmem.h
memman-ao.o: 		memman-ao.c memman-ao.h common.h areaman.h sysmem.h blkman-ao.h
sysmem-mmap.o:		sysmem-mmap.c sysmem.h common.h
sysmem-sbrk.o:		sysmem-sbrk.c sysmem.h common.h
sysmem-shm.o:		sysmem-shm.c sysmem.h common.h
tst-random.o:		tst-random.c memman-ao.h common.h areaman.h sysmem.h

tests/t-test1.o:	tests/t-test1.c tests/lran2.h tests/t-test.h ldwrapper.h
tests/t-test2.o:	tests/t-test2.c tests/lran2.h tests/t-test.h ldwrapper.h

clean:
	@find -regextype posix-extended -regex ".*(~|\.(o|lo|a|la|loT|so))" | xargs rm -vf
	@rm -vrf .libs tests/.libs
	@rm -vf tst-random tests/{t-test1,t-test2}

lines:
	@find -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | xargs cat | wc
	@find -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | xargs cat | sed '/^$$/d' | wc

.PHONY:	lines clean
