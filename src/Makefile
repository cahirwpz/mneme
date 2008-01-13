CC		=	libtool --mode=compile gcc -ggdb -static -fms-extensions
INCLUDE	=	-I./tests -I./tests/sysdeps/pthread -I./tests/sysdeps/generic 
DEFS	=	-DPM_USE_SBRK -DPM_USE_MMAP -DPM_USE_SHM -DDEADMEMORY -DVERBOSE=1
CFLAGS	=	-march=i686 -O0 -Wall $(DEFS) $(INCLUDE)

LD		=	libtool --mode=link gcc -g -static 
LDFLAGS	=	-rpath /usr/local/lib -lrt

OBJS	=	memmgr.lo blkmgr.lo blklst-ao.lo areamgr.lo mmapmgr.lo sysmem-mmap.lo sysmem-sbrk.lo sysmem-shm.lo

all:	cscope.out libmymalloc.la tst-random tests/t-test1 tests/t-test2

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

areamgr.o:			areamgr.c areamgr.h common.h sysmem.h
blklst-ao.o: 		blklst-ao.c blklst-ao.h common.h areamgr.h sysmem.h
blkmgr.o: 			blkmgr.c blkmgr.h blklst-ao.h common.h areamgr.h sysmem.h
ldwrapper.o: 		ldwrapper.c memmgr.h common.h areamgr.h sysmem.h
mmapmgr.o:			mmapmgr.c mmapmgr.h common.h areamgr.h sysmem.h
sysmem-mmap.o:		sysmem-mmap.c sysmem.h common.h
sysmem-sbrk.o:		sysmem-sbrk.c sysmem.h common.h
sysmem-shm.o:		sysmem-shm.c sysmem.h common.h
tst-random.o:		tst-random.c memmgr.h common.h areamgr.h sysmem.h
memmgr.o:			memmgr.c mmapmgr.h areamgr.h common.h sysmem.h memmgr.h

tests/t-test1.o:	tests/t-test1.c tests/lran2.h tests/t-test.h ldwrapper.h
tests/t-test2.o:	tests/t-test2.c tests/lran2.h tests/t-test.h ldwrapper.h

clean:
	@find -regextype posix-extended -regex ".*(~|\.(o|lo|a|la|loT|so))" | xargs rm -vf
	@rm -vrf .libs tests/.libs
	@rm -vf tst-random tests/{t-test1,t-test2}

lines:
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | xargs cat | wc
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | xargs cat | sed '/^$$/d' | wc

cscope.out: $(wildcard *.c *.h)
	cscope -b $^

.PHONY:	lines clean
