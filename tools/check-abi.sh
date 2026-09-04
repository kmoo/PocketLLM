#!/bin/sh
# Assert the cross-compiled binary matches the contract we derived from the
# working gambatte-k2 deployment. A red gate here must block deploy. PLAN.md §2.
set -e
BIN="$1"
[ -n "$BIN" ] && [ -f "$BIN" ] || { echo "check-abi: no binary given"; exit 1; }

desc=$(file -b "$BIN")
echo "ABI: $desc"

fail=0
echo "$desc" | grep -q "ELF 32-bit LSB"      || { echo "  ✗ not 32-bit LSB ELF"; fail=1; }
echo "$desc" | grep -q "ARM"                 || { echo "  ✗ not ARM"; fail=1; }
echo "$desc" | grep -q "statically linked"   || { echo "  ✗ not statically linked"; fail=1; }
echo "$desc" | grep -qi "interpreter"        && { echo "  ✗ has an ELF interpreter"; fail=1; }

# No dynamic library dependencies at all.
if strings "$BIN" | grep -qE '^lib[a-z0-9_+-]+\.so'; then
    echo "  ✗ references a shared library"; fail=1
fi
python3 tools/elf-abi.py "$BIN" || fail=1

[ $fail -eq 0 ] || { echo "ABI GATE FAILED"; exit 1; }
echo "ABI gate OK"
