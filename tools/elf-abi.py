#!/usr/bin/env python3
"""Read the ARM ABI contract straight out of the ELF header.

The Kindle needs EABI5 + hard-float. Confirmed against the working
gambatte-k2 binary (see PLAN.md section 2). No readelf required.
"""
import struct, sys

path = sys.argv[1]
d = open(path, 'rb').read()
if d[:4] != b'\x7fELF':
    print("  x not an ELF file"); sys.exit(1)

e_flags = struct.unpack_from('<I', d, 0x24)[0]
hard = bool(e_flags & 0x400)   # EF_ARM_ABI_FLOAT_HARD
soft = bool(e_flags & 0x200)   # EF_ARM_ABI_FLOAT_SOFT
eabi = (e_flags >> 24) & 0xff

print("  e_flags=0x%08x  EABI%d  hard_float=%s" % (e_flags, eabi, hard))

ok = True
if eabi != 5:
    print("  x EABI version is %d, Kindle needs 5" % eabi); ok = False
if not hard:
    print("  x not hard-float; Kindle needs armhf"); ok = False
if soft:
    print("  x soft-float flag is set"); ok = False
sys.exit(0 if ok else 1)
