OSXCC   = xcrun clang
LINUXCC = gcc
WINCC   = x86_64-w64-mingw32-gcc  # 64-bit

WINCC_VERSION := $(shell $(WINCC) --version 2> /dev/null)

SOLARISCC = gcc
SOLARISCFLAGS = -DHAVE_REMOTE -DHAVE_SNPRINTF -D_GNU_SOURCE=1 -O3 -g -pthread
SOLARISINCLUDE = -I../
SOLARISLIB = -lpcap -lcrypt -lsocket -lnsl
SOLARISLIBPATH = -L../
SOLARISTARGET = rpcapd
