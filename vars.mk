OSXCC   = xcrun clang
LINUXCC = gcc
WINCC   = x86_64-w64-mingw32-gcc  # 64-bit

WINCC_VERSION := $(shell $(WINCC) --version 2> /dev/null)
