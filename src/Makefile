CC		=	libtool --mode=compile gcc -s
LD		=	libtool --mode=link gcc -s
CFLAGS	=	-O2 -Wall -DNDEBUG

OBJS	=	memmgr.o firstfit-ao.o sysmem-mmap.o sysmem-sbrk.o sysmem-shm.o

all:	$(patsubst %.c,%.o,$(shell ls -1 *.c)) libmymalloc.la tst-random

libmymalloc.la:	$(OBJS)
	$(LD) -o $@ $(patsubst %.o,%.lo,$^) -rpath /usr/local/lib -lrt

tst-random:	tst-random.lo libmymalloc.la
	$(LD) -o $@ $^

clean:
	rm -f *~ *.o *.lo *.la
	rm -rf .libs tst-random
