# Makefile for rastertolhplh — ARM-native CUPS filter for Lenovo M100D
# Replaces the proprietary lnthr8zfilter.app (x86-64 only)

# ── Cross-compile settings ──
# For native ARM (aarch64) build on the target device:
CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
ARCH    =

# For cross-compiling from x86-64 to aarch64:
#   make CROSS=aarch64-linux-gnu-
#   (requires: gcc-aarch64-linux-gnu, libcups2-dev:arm64, etc.)

ifdef CROSS
CC      = $(CROSS)gcc
ARCH    = -march=armv8-a
endif

CFLAGS += $(ARCH)

# ── Paths ──
JBIG_DIR    = jbigkit-2.1/libjbig
CUPS_FILTER_DIR = /usr/lib/cups/filter
CUPS_PPD_DIR    = /usr/share/ppd/Lenovo

TARGET  = rastertolhplh

# ── Sources ──
JBIG_SRC = $(JBIG_DIR)/jbig85.c $(JBIG_DIR)/jbig_ar.c
MAIN_SRC = rastertolhplh.c

# ── Build ──
.PHONY: all clean install uninstall jbig

all: $(TARGET)

jbig:
	@test -d $(JBIG_DIR) || (echo "Download jbigkit-2.1 first:" && \
		echo "  wget https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz" && \
		echo "  tar xzf jbigkit-2.1.tar.gz" && exit 1)
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -c $(JBIG_DIR)/jbig85.c -o jbig85.o
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -c $(JBIG_DIR)/jbig_ar.c -o jbig_ar.o

$(TARGET): rastertolhplh.c jbig85.o jbig_ar.o
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -o $@ $(MAIN_SRC) jbig85.o jbig_ar.o \
		-lcupsimage -lcups -lm -lpthread -lz

clean:
	rm -f $(TARGET) jbig85.o jbig_ar.o

install: $(TARGET)
	install -d $(CUPS_FILTER_DIR)
	install -d $(CUPS_PPD_DIR)
	install -m 755 $(TARGET) $(CUPS_FILTER_DIR)/$(TARGET)
	install -m 644 lenovo-M100D-arm.ppd $(CUPS_PPD_DIR)/lenovo-M100D-arm.ppd
	-systemctl restart cups 2>/dev/null || service cups restart 2>/dev/null

uninstall:
	rm -f $(CUPS_FILTER_DIR)/$(TARGET)
	rm -f $(CUPS_PPD_DIR)/lenovo-M100D-arm.ppd
	-systemctl restart cups 2>/dev/null || service cups restart 2>/dev/null
