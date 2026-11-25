#!/bin/sh
# Test of tools/dm_offsets.py (the addresses libkb_dm.so is built with) on small ELF files made here.
# If a real dump is at research/dm (or DM_REAL_DUMP=<folder>) the addresses found in it are compared
# with native/include/dm_offsets.h. Needs python3 + pyelftools; without them: SKIPPED (exit 0).
set -e
cd "$(dirname "$0")"
if ! python3 -c "import elftools" 2>/dev/null; then echo "SKIPPED: python3 with pyelftools not available"; exit 0; fi
exec python3 -B test_dm_offsets.py
