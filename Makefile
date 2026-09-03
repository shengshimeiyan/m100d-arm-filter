# Makefile for rastertolhplh — ARM-native CUPS filter for Lenovo M100D
# Replaces the proprietary lnthr8zfilter.app (x86-64 only)
#
# This filter is STANDALONE — no libcups/libcupsimage dependency.
# The patched jbigkit-2.1 sources are vendored in jbig/ — no download needed.
#
# Quick build on ARM device:
#   make
#   sudo make install

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11

# ── Paths ──
JBIG_DIR        = jbig
CUPS_FILTER_DIR = /usr/lib/cups/filter
CUPS_PPD_DIR    = /usr/share/ppd/Lenovo

TARGET  = rastertolhplh

# ── Build ──
.PHONY: all clean install uninstall

all: $(TARGET)

jbig85.o: $(JBIG_DIR)/jbig85.c $(JBIG_DIR)/jbig85.h
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -c $< -o $@

jbig_ar.o: $(JBIG_DIR)/jbig_ar.c
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -c $< -o $@

$(TARGET): rastertolhplh.c jbig85.o jbig_ar.o
	$(CC) $(CFLAGS) -I$(JBIG_DIR) -o $@ rastertolhplh.c jbig85.o jbig_ar.o -lm -static

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
