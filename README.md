# ssversion

Tiny Switch homebrew that dumps the `ns_ssversion:/entry` SystemSaveData
(system save id `0x49`, the record holding the SystemUpdate title id
`0100000000000816`) as hex and as parsed fields.

Enumerates SystemSaveData records with `fsOpenSaveDataInfoReader`
(all spaces), prints every record found, then opens the match through
`fsOpenSaveDataFileSystemBySystemSaveDataId` using exactly the attributes
the reader returned - fsp-srv matches the whole filter, so this is the
reliable order on both consoles. Reads `/entry` (up to 0x40 bytes) and prints:

```
== hex (48 bytes) ==
...
== parsed ==
title id     : 0100000000000816
supernag     : 0x585001e0
0x0C         : 3
state        : 3 (no update needed)
last check   : 1794337376 (2026-11-10 19:02:56 UTC)
fw version   : 23.0.1
time valid   : 1
last 'no upd': 1794337376 (2026-11-10 19:02:56 UTC)
```

Layout: `0x00` u64 title id, `0x08` u32 safe version (supernag), `0x0C` u32
(always seen as 3), `0x10` u8 state (0 unknown, 1 network error, 2 needs
update, 3 no update needed), `0x18` u64 posix last-check, `0x20..0x22` fw
major/minor/micro, `0x23` u8 time-valid flag, `0x28` u64 posix last-time-of-
no-update-needed.

## Files

Drop one of the release nros into `switch/` on the SD card and start it from
hbmenu. Plus quits.

* `ssversion-switch1.nro` - original Switch (devkitA64/libnx)
* `ssversion-switch2.nro` - Switch 2 (ounce-devkit/libounce)

## Building

Switch 1:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64 make
```

Switch 2 (build the devkit image once from
'https://github.com/ouncebrew/ounce-devkit):

```sh
docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile
```

Both link the same `source/main.c`; only toolchain flags and headers differ.
