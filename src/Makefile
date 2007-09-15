CC		=	libtool --mode=compile gcc -g -static -fms-extensions
LD		=	libtool --mode=link gcc -g -static 
CFLAGS	=	-O -Wall -DPM_USE_SBRK -DPM_USE_MMAP -DPM_USE_SHM -DVERBOSE=0 -DDEADMEMORY

OBJS	=	memman-ao.o areaman.o blkman-ao.o sysmem-mmap.o sysmem-sbrk.o sysmem-shm.o

all:	$(patsubst %.c,%.o,$(shell ls -1 *.c)) libmymalloc.la tst-random

libmymalloc.la:	$(OBJS)
	$(LD) -o $@ $(patsubst %.o,%.lo,$^) -rpath /usr/local/lib -lrt

tst-random:	tst-random.lo libmymalloc.la
	$(LD) -o $@ $^

areaman.o:		areaman.c areaman.h common.h sysmem.h
blkman-ao.o: 	blkman-ao.c blkman-ao.h common.h areaman.h sysmem.h
ldwrapper.o: 	ldwrapper.c memman-ao.h common.h areaman.h sysmem.h
memman-ao.o: 	memman-ao.c memman-ao.h common.h areaman.h sysmem.h blkman-ao.h
sysmem-mmap.o:	sysmem-mmap.c sysmem.h common.h
sysmem-sbrk.o:	sysmem-sbrk.c sysmem.h common.h
sysmem-shm.o:	sysmem-shm.c sysmem.h common.h
tst-random.o:	tst-random.c memman-ao.h common.h areaman.h sysmem.h

clean:
	rm -f *~ *.o *.lo *.la *.loT
	rm -rf .libs tst-random

lines:
	@cat *.c *.h Makefile | sed '/^$$/d' | wc -l

.PHONY:	lines clean
