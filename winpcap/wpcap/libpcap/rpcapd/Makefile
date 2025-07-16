################################
# Makefile for the remote daemon
################################
include ../../../../vars.mk

CFLAGS		= -DHAVE_REMOTE -DHAVE_SNPRINTF -D_GNU_SOURCE=1 -O3 -g
LINUXCFLAGS = -pthread
OSXCFLAGS   = -pthread -mmacosx-version-min=10.12 -DHAVE_VSNPRINTF -DHAVE_STRLCPY -DHAVE_SNPRINTF -D_U_= \
              -DUSE_DISPATCHED_FOR_IFRECV -Wno-deprecated-non-prototype -Wno-return-type
WINCFLAGS   = -DHAVE_STRUCT_TIMESPEC

#flags for debugging: -D_DEBUG -g -Wall

INCLUDE	= -I../
WININCLUDE = -I../Win32/Include -I../../../Common -I../../../../win32-pthreads

LINUXLIB = -lpcap
OSXLIB   = -lpcap

WINLIB   = -lwpcap -lpthreadGC2 -lpacket -lws2_32

LINUXLIBPATH = -L../
OSXLIBPATH   = -L../ -Wl,-w
WINLIBPATH   = -L../../lib -L../../../../win32-pthreads \
			   -L../../../packetNtx/Dll/Project

# Files that are needed to compile this project
FILES = rpcapd.c daemon.c utils.c fileconf.c ../pcap-remote.c ../sockutils.c \
		../pcap-new.c ../pcap.c ../savefile.c ../optimize.c ../grammar.c ../scanner.c ../gencode.c ../nametoaddr.c ../inet.c ../bpf_dump.c ../bpf_image.c
WINFILES = win32-svc.c win32-messages.o

LINUXTARGET = rpcapd
OSXTARGET   = rpcapd
WINTARGET   = rpcapd.exe

# XXX: this is awful...I think this is why they invented autotools 25 years ago.
ifeq ($(PLATFORM), windows)
	CC = $(WINCC)
	FILES += $(WINFILES)
	CFLAGS += $(WINCFLAGS)
	INCLUDE += $(WININCLUDE)
	LIB += $(WINLIB)
	LIBPATH += $(WINLIBPATH)
	TARGET = $(WINTARGET)
else ifeq ($(PLATFORM), linux)
	CC = $(LINUXCC)
	FILES += $(LINUXFILES)
	CFLAGS += $(LINUXCFLAGS)
	INCLUDE += $(LINUXINCLUDE)
	LIB += $(LINUXLIB)
	LIBPATH += $(LINUXLIBPATH)
	TARGET = $(LINUXTARGET)
else ifeq ($(PLATFORM), osx)
	CC = $(OSXCC)
	FILES += $(OSXFILES)
	CFLAGS += $(OSXCFLAGS)
	INCLUDE += $(OSXINCLUDE)
	LIB += $(OSXLIB)
	LIBPATH += $(OSXLIBPATH)
	TARGET = $(OSXTARGET)
else
default:
	$(MAKE) PLATFORM=linux executable
ifdef WINCC_VERSION
	$(MAKE) PLATFORM=windows executable
endif
endif

executable: ../grammar.c ../scanner.c $(TARGET)

../grammar.c: ../grammar.y
	cd .. && yacc -d grammar.y && mv y.tab.c grammar.c && mv y.tab.h tokdefs.h

../scanner.c: ../scanner.l ../tokdefs.h
	cd .. && lex -t scanner.l > scanner.c

# XXX: using object files would make incremental rebuilds faster...
$(TARGET): $(FILES)
	$(CC) $(CFLAGS) $(INCLUDE) $(LIBPATH) -o $(TARGET) $(FILES) $(LIB)

win32-messages.o: win32-messages.mc
	x86_64-w64-mingw32-windmc $<
	x86_64-w64-mingw32-windres -i win32-messages.rc -o $@

install:
	cp $(LINUXTARGET) rpcapd-dist/rpcapd-64bit-linux
	chmod 0755 rpcapd-dist/rpcapd-64bit-linux

clean:
	rm -f rpcapd rpcapd.osx rpcapd.exe win32-messages.bin win32-messages.h \
		  win32-messages.o win32-messages.rc

distclean: clean
	rm -f rpcapd-dist/rpcapd-64bit-linux
