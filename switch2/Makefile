# ssversion dumper - Switch 2 build with the ounce devkit
# (https://github.com/ouncebrew/ounce-devkit):
#   docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile
#
# Same source as the Switch 1 build; only the toolchain flags and the
# libnx/libounce lookup differ.

DEVKITPRO ?= /opt/devkitpro
LIBOUNCE := $(DEVKITPRO)/libounce
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
include $(LIBOUNCE)/switch2_cpu.mk

OUT := build/switch2

ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -Wall $(ARCH) -isystem $(LIBOUNCE)/include
LIBS := -lnx

all: $(OUT)/ssversion.nro

$(OUT)/ssversion.elf: source/main.c
	@mkdir -p $(OUT)
	$(TOOLS)gcc -specs=$(LIBOUNCE)/switch.specs $(CFLAGS) $< \
	    -L$(LIBOUNCE)/lib $(LIBS) -o $@

$(OUT)/ssversion.nacp:
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "ssversion" "wren" "1.8" $@

$(OUT)/ssversion.nro: $(OUT)/ssversion.elf $(OUT)/ssversion.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/ssversion.nacp

clean:
	rm -rf $(OUT)

.PHONY: all clean
