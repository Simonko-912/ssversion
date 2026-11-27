# vslist - Switch 2 build with the ounce devkit
#   docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile
# Same source as the Switch 1 build; only toolchain wiring differs.

DEVKITPRO ?= /opt/devkitpro
LIBOUNCE := $(DEVKITPRO)/libounce
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
include $(LIBOUNCE)/switch2_cpu.mk

OUT := build/switch2

ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -Wall $(ARCH) -isystem $(LIBOUNCE)/include
LIBS := -lnx

all: $(OUT)/vslist.nro

$(OUT)/vslist.elf: source/main.c
	@mkdir -p $(OUT)
	$(TOOLS)gcc -specs=$(LIBOUNCE)/switch.specs $(CFLAGS) $< \
	    -L$(LIBOUNCE)/lib $(LIBS) -o $@

$(OUT)/vslist.nacp:
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "vslist" "wren" "1.0" $@

$(OUT)/vslist.nro: $(OUT)/vslist.elf $(OUT)/vslist.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/vslist.nacp

clean:
	rm -rf $(OUT)

.PHONY: all clean
