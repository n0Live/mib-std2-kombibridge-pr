#!/bin/sh
# Host test of the navigation app classifier (native/src/nav_app_classify.h). Needs a host C
# compiler ($CC, else cc / clang / gcc); tries AddressSanitizer + UBSan first, plain if their
# runtime is missing. No compiler at all: SKIPPED (exit 0).
set -e
cd "$(dirname "$0")"
if [ -z "${CC:-}" ]; then
  for c in cc clang gcc; do if command -v $c >/dev/null 2>&1; then CC=$c; break; fi; done
fi
if [ -z "${CC:-}" ]; then echo "SKIPPED: no host C compiler"; exit 0; fi
OUT="${TMPDIR:-/tmp}/kb_test_navapp"
if ! "$CC" -std=gnu99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o "$OUT" test_navapp.c 2>/dev/null; then
  echo "(sanitizer runtime not available: building without it)"
  "$CC" -std=gnu99 -O1 -g -Wall -Wextra -Werror -o "$OUT" test_navapp.c
fi
exec "$OUT"
