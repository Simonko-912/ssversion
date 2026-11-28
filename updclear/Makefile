# updclear - Switch 1 build (devkitA64 + libnx).
#   docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64:20260219 make
# Switch 2 build uses switch2/Makefile in the ounce-devkit image.

DEVKITPRO ?= /opt/devkitpro
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-

OUT := build

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -Wall $(ARCH) -isystem $(DEVKITPRO)/libnx/include
LIBS := -lnx

all: $(OUT)/updclear.nro

$(OUT)/updclear.elf: source/main.c
	@mkdir -p $(OUT)
	$(TOOLS)gcc -specs=$(DEVKITPRO)/libnx/switch.specs $(CFLAGS) $< \
	    -L$(DEVKITPRO)/libnx/lib $(LIBS) -o $@

$(OUT)/updclear.nacp:
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "updclear" "wren" "1.2" $@

$(OUT)/updclear.nro: $(OUT)/updclear.elf $(OUT)/updclear.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/updclear.nacp

clean:
	rm -rf $(OUT)

.PHONY: all clean
