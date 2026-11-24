# ssversion dumper - Switch 1 build (devkitA64 + libnx).
#   docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64 make
# The Switch 2 build uses switch2/Makefile in the ounce-devkit image:
#   docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile

DEVKITPRO ?= /opt/devkitpro
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-

OUT := build

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -Wall $(ARCH) -isystem $(DEVKITPRO)/libnx/include
LIBS := -lnx

all: $(OUT)/ssversion.nro

$(OUT)/ssversion.elf: source/main.c
	@mkdir -p $(OUT)
	$(TOOLS)gcc -specs=$(DEVKITPRO)/libnx/switch.specs $(CFLAGS) $< \
	    -L$(DEVKITPRO)/libnx/lib $(LIBS) -o $@

$(OUT)/ssversion.nacp:
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "ssversion" "wren" "2.5" $@

$(OUT)/ssversion.nro: $(OUT)/ssversion.elf $(OUT)/ssversion.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/ssversion.nacp

clean:
	rm -rf $(OUT)

.PHONY: all clean
