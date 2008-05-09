CC		=	libtool --mode=compile gcc -ggdb -static -fms-extensions 
INCLUDE	=	-I./tests -I./tests/sysdeps/pthread -I./tests/sysdeps/generic 
DEFS	=	-D__USE_GNU -DPM_USE_SBRK -DPM_USE_MMAP -DPM_USE_SHM -DDEADMEMORY -DVERBOSE=1
CFLAGS	=	-march=i686 -O2 -Wall $(DEFS) $(INCLUDE)

LD		=	libtool --mode=link gcc -g 
LDFLAGS	=	-rpath /usr/local/lib -lnana -lrt -lm

OBJS	=	memmgr.lo eqsbmgr.lo blkmgr.lo blklst-ao.lo areamgr.lo mmapmgr.lo sysmem-mmap.lo sysmem-sbrk.lo sysmem-shm.lo

all:	cscope.out tags libmneme.la tst-random tests/t-test1 tests/t-test2

libmneme.la:	$(OBJS)
	$(LD) $(LDFLAGS) -static -o $@ $(patsubst %.o,%.lo,$^)

libmneme_posix.la: $(OBJS) ldwrapper.lo
	$(LD) $(LDFLAGS) -o $@ $(patsubst %.o,%.lo,$^) ldwrapper.lo

tst-random:	libmneme.la tst-random.lo 
	$(LD) $(LDFLAGS) -static -o $@ $^

tests/t-test1:		tests/t-test1.lo libmneme.la
	$(LD) $(LDFLAGS) -static -o $@ $^

tests/t-test2:		tests/t-test2.lo libmneme.la
	$(LD) $(LDFLAGS) -static -o $@ $^

%.lo: %.o
	@

areamgr.o:			areamgr.c areamgr.h common.h sysmem.h
blklst-ao.o: 		blklst-ao.c blklst-ao.h common.h areamgr.h sysmem.h
blkmgr.o: 			blkmgr.c blkmgr.h blklst-ao.h common.h areamgr.h sysmem.h
eqsbmgr.o:			eqsbmgr.c eqsbmgr.h common.h areamgr.h sysmem.h common-list0.c
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
	@rm -vf cscope.out tags

lines:
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | sed -r '/.*list0.*/d' | xargs cat | wc
	@find -maxdepth 1 -regextype posix-extended -regex "(.*\.(c|h)|Makefile)" | sed -r '/.*list0.*/d' | xargs cat | sed '/^$$/d' | wc

cscope.out: $(wildcard *.c *.h)
	cscope -b $^

tags: $(wildcard *.c *.h)
	ctags-exuberant --extra=+fq --fields=+afmikKlnsStz $^

.PHONY:	lines clean
