#!/bin/sh
# Host test of shim/src/sdr_density.h (display density in the ServiceDiscoveryResponse). Needs a host
# C++ compiler ($CXX, else clang++ or g++); under ASan+UBSan when available. No compiler: SKIPPED.
set -e
cd "$(dirname "$0")"
if [ -z "${CXX:-}" ]; then
  for c in clang++ g++ c++; do if command -v $c >/dev/null 2>&1; then CXX=$c; break; fi; done
fi
if [ -z "${CXX:-}" ]; then echo "SKIPPED: no host C++ compiler"; exit 0; fi
OUT="${TMPDIR:-/tmp}/kb_test_sdr_density"
if ! $CXX -std=c++11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
     -o "$OUT" test_sdr_density.cpp 2>/dev/null; then
  echo "(sanitizer runtime not available: building without it)"
  $CXX -std=c++11 -O1 -g -Wall -Wextra -o "$OUT" test_sdr_density.cpp
fi
exec "$OUT"
