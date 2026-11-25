#!/bin/sh
# Build native/build/libkb_dm.so (QNX 6.5 armle-v7, soft-float) with clang + ld.lld; no QNX SDK:
# the unit's libc.so.3 is replaced at link time by a stub that only carries the symbol names.
# Entry point kb_dm_init via DT_INIT (the QNX 6.5 run-time linker predates .init_array).
#
# The hook is tied to the addresses in two files of the unit (libOpenMAXAL.so.1 and the DisplayManager
# binary). include/dm_offsets.h holds those of firmware 0480. For another firmware: DM_DUMP=<folder with
# the dumped files> ./build.sh - tools/dm_offsets.py finds the addresses in them and the library is
# built for that firmware (the output says which files it was built for).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CLANG="${CLANG:-clang}"
LLD="${LLD:-ld.lld}"
OUT="$HERE/build"
CFLAGS="--target=armv7a-none-eabi -marm -mfloat-abi=soft -fPIC -Os -ffreestanding -nostdlib
 -fno-stack-protector -fno-builtin -fvisibility=hidden -std=c99 -Wall -Wextra -Werror
 -I$HERE/include -I$HERE/../shim/src"
mkdir -p "$OUT"
# generated offsets of a dump come first in the include path; a stale one is never left behind
rm -rf "$OUT/gen"
OFFSETS="firmware 0480 (include/dm_offsets.h)"
if [ -n "${DM_DUMP:-}" ]; then
  mkdir -p "$OUT/gen"
  python3 -B "$HERE/../tools/dm_offsets.py" "$DM_DUMP" -o "$OUT/gen/dm_offsets.h" \
    --note "Built from the dump $(basename "$DM_DUMP")."
  OFFSETS="the dump $DM_DUMP ($(sed -n 's/^#define DM_OFFSETS_NOTE "\(.*\)"$/\1/p' "$OUT/gen/dm_offsets.h"))"
  CFLAGS="-I$OUT/gen $CFLAGS"
fi
"$CLANG" $CFLAGS -fvisibility=default -c "$HERE/stubs/libc_stub.c" -o "$OUT/libc_stub.o"
"$LLD" -shared --hash-style=sysv -soname libc.so.3 -o "$OUT/libc.so.3" "$OUT/libc_stub.o"
"$CLANG" $CFLAGS -c "$HERE/src/dm_hook.c" -o "$OUT/dm_hook.o"
"$LLD" -shared -z defs -z max-page-size=0x1000 --hash-style=sysv --gc-sections \
  -soname libkb_dm.so -init kb_dm_init -o "$OUT/libkb_dm.so" "$OUT/dm_hook.o" "$OUT/libc.so.3"
echo "$OUT/libkb_dm.so  (addresses of $OFFSETS)"
