# Makefile for Putty Mobile Proxy

# For gcc
CC= gcc
# For ANSI compilers
#CC= cc

#For Optimization
# CFLAGS= -O2 -Wall -fno-strict-aliasing -fPIC
#For debugging
CFLAGS= -g -Wall -fPIC
#LIBS= 
#INSTALLDIR=/usr/local
INSTALLDIR=$(DESTDIR)/usr

# lorder *.o | tsort

PROGS=tester

RM= /bin/rm -f

all: $(PROGS)

install: all
	install -D --mode=0644 libeasyv6.so.1.0 \
		$(INSTALLDIR)/lib/libeasyv6.so.1.0
	install -D --mode=0644 libeasyv6.a $(INSTALLDIR)/lib/libeasyv6.a
	install -D --mode=0644 easyv6.h $(INSTALLDIR)/include/easyv6.h
	install -D --mode=0644 libeasyv6.pc \
		$(INSTALLDIR)/lib/pkgconfig/libeasyv6.pc
	rm -f $(INSTALLDIR)/lib/libeasyv6.so.1
	rm -f $(INSTALLDIR)/lib/libeasyv6.so
	ln -s libeasyv6.so.1.0 $(INSTALLDIR)/lib/libeasyv6.so.1
	ln -s libeasyv6.so.1.0 $(INSTALLDIR)/lib/libeasyv6.so
	install -D --mode=0644 addrinfototext.3 \
		$(INSTALLDIR)/share/man/man3/addrinfototext.3
	gzip $(INSTALLDIR)/share/man/man3/addrinfototext.3
	install -D --mode=0644 connectbyaddrinfo.3 \
		$(INSTALLDIR)/share/man/man3/connectbyaddrinfo.3
	gzip $(INSTALLDIR)/share/man/man3/connectbyaddrinfo.3
	install -D --mode=0644 connectbyname.3 \
		$(INSTALLDIR)/share/man/man3/connectbyname.3
	gzip $(INSTALLDIR)/share/man/man3/connectbyname.3
	install -D --mode=0644 getpeernametext.3 \
		$(INSTALLDIR)/share/man/man3/getpeernametext.3
	gzip $(INSTALLDIR)/share/man/man3/getpeernametext.3
	install -D --mode=0644 listenbyname.3 \
		$(INSTALLDIR)/share/man/man3/listenbyname.3
	gzip $(INSTALLDIR)/share/man/man3/listenbyname.3
	install -D --mode=0644 timeoutgetaddrinfo.3 \
		$(INSTALLDIR)/share/man/man3/timeoutgetaddrinfo.3
	gzip $(INSTALLDIR)/share/man/man3/timeoutgetaddrinfo.3

.c.o:
	$(CC) -c $(CFLAGS) $<

libeasyv6.a:	easyv6.o
	ar -cvq libeasyv6.a easyv6.o
	gcc -shared -Wl,-soname,libeasyv6.so.1 -lrt -lanl \
		 -o libeasyv6.so.1.0 easyv6.o
	ln -sf libeasyv6.so.1.0 libeasyv6.so.1
#	ln -sf libeasyv6.so.1.0 libeasyv6.so

depend:
#	makedepend -I/usr/include/linux *.c
	gccmakedep *.c

tester: libeasyv6.a tester.o
	$(CC) tester.o -L. -leasyv6 -lrt -lanl -o $@

clean:
	rm -f *.a *.so *.so.* *.o $(PROGS)

# DO NOT DELETE
easyv6.o: easyv6.c /usr/include/netdb.h /usr/include/features.h \
 /usr/include/bits/predefs.h /usr/include/sys/cdefs.h \
 /usr/include/bits/wordsize.h /usr/include/gnu/stubs.h \
 /usr/include/gnu/stubs-32.h /usr/include/netinet/in.h \
 /usr/include/stdint.h /usr/include/bits/wchar.h \
 /usr/include/sys/socket.h /usr/include/sys/uio.h \
 /usr/include/sys/types.h /usr/include/bits/types.h \
 /usr/include/bits/typesizes.h /usr/include/time.h \
 /usr/lib/gcc/i486-linux-gnu/4.4.5/include/stddef.h /usr/include/endian.h \
 /usr/include/bits/endian.h /usr/include/bits/byteswap.h \
 /usr/include/sys/select.h /usr/include/bits/select.h \
 /usr/include/bits/sigset.h /usr/include/bits/time.h \
 /usr/include/sys/sysmacros.h /usr/include/bits/pthreadtypes.h \
 /usr/include/bits/uio.h /usr/include/bits/socket.h \
 /usr/include/bits/sockaddr.h /usr/include/asm/socket.h \
 /usr/include/asm-generic/socket.h /usr/include/asm/sockios.h \
 /usr/include/asm-generic/sockios.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/siginfo.h \
 /usr/include/bits/netdb.h easyv6.h /usr/include/errno.h \
 /usr/include/bits/errno.h /usr/include/linux/errno.h \
 /usr/include/asm/errno.h /usr/include/asm-generic/errno.h \
 /usr/include/asm-generic/errno-base.h /usr/include/xlocale.h \
 /usr/include/stdlib.h /usr/include/bits/waitflags.h \
 /usr/include/bits/waitstatus.h /usr/include/alloca.h \
 /usr/include/string.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/environments.h \
 /usr/include/bits/confname.h /usr/include/getopt.h /usr/include/fcntl.h \
 /usr/include/bits/fcntl.h /usr/include/sys/stat.h \
 /usr/include/bits/stat.h /usr/include/sys/time.h /usr/include/stdio.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/i486-linux-gnu/4.4.5/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/arpa/inet.h /usr/include/pthread.h /usr/include/sched.h \
 /usr/include/bits/sched.h /usr/include/signal.h \
 /usr/include/bits/setjmp.h
tester.o: tester.c easyv6.h /usr/include/sys/types.h \
 /usr/include/features.h /usr/include/bits/predefs.h \
 /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \
 /usr/include/gnu/stubs.h /usr/include/gnu/stubs-32.h \
 /usr/include/bits/types.h /usr/include/bits/typesizes.h \
 /usr/include/time.h /usr/lib/gcc/i486-linux-gnu/4.4.5/include/stddef.h \
 /usr/include/endian.h /usr/include/bits/endian.h \
 /usr/include/bits/byteswap.h /usr/include/sys/select.h \
 /usr/include/bits/select.h /usr/include/bits/sigset.h \
 /usr/include/bits/time.h /usr/include/sys/sysmacros.h \
 /usr/include/bits/pthreadtypes.h /usr/include/sys/socket.h \
 /usr/include/sys/uio.h /usr/include/bits/uio.h \
 /usr/include/bits/socket.h /usr/include/bits/sockaddr.h \
 /usr/include/asm/socket.h /usr/include/asm-generic/socket.h \
 /usr/include/asm/sockios.h /usr/include/asm-generic/sockios.h \
 /usr/include/netdb.h /usr/include/netinet/in.h /usr/include/stdint.h \
 /usr/include/bits/wchar.h /usr/include/bits/in.h \
 /usr/include/rpc/netdb.h /usr/include/bits/netdb.h /usr/include/stdio.h \
 /usr/include/libio.h /usr/include/_G_config.h /usr/include/wchar.h \
 /usr/lib/gcc/i486-linux-gnu/4.4.5/include/stdarg.h \
 /usr/include/bits/stdio_lim.h /usr/include/bits/sys_errlist.h \
 /usr/include/errno.h /usr/include/bits/errno.h \
 /usr/include/linux/errno.h /usr/include/asm/errno.h \
 /usr/include/asm-generic/errno.h /usr/include/asm-generic/errno-base.h \
 /usr/include/stdlib.h /usr/include/alloca.h /usr/include/unistd.h \
 /usr/include/bits/posix_opt.h /usr/include/bits/confname.h \
 /usr/include/getopt.h /usr/include/fcntl.h /usr/include/bits/fcntl.h \
 /usr/include/string.h /usr/include/xlocale.h
