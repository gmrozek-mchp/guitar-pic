#!/usr/bin/env bash
# Prepend the SAM9X7 ROM-code NAND boot header to an at91bootstrap binary, so the
# boot region can be written by a plain `nand write` (the u-boot/JTAG path in
# ../openocd/program-nand.md) instead of SAM-BA's `writeboot` (which synthesizes
# the header itself).
#
# The ROM reads page 0 without ECC, expecting 52 copies of a 32-bit word encoding
# the NAND/PMECC params, with the bootstrap code at offset 0xD0 (SAM9X7 Data Sheet
# DS60001813D, "NAND Flash Specific Header Detection"). The word MUST match the
# actual chip (u-boot `nand info`), NOT at91bootstrap addpmecchead.py's SAM9X7
# defaults (2048/64/4) — this board's NAND is 4096/256/8-bit/512 -> 0xc2605007.
#
# The header word + offset math is inlined here (mirrors at91bootstrap
# scripts/pmecc_head.py) so this is self-contained — no external tree needed.
#
# Usage: ./make-pmecchead.sh [in.bin] [out.bin] [page oob eccbits sector]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IN="${1:-$here/sam9x7-nandflashboot-uboot-4.0.13.bin}"
OUT="${2:-$here/sam9x7-nandflashboot-uboot-4.0.13-pmecchead.bin}"
PAGE="${3:-4096}"; OOB="${4:-256}"; ECC="${5:-8}"; SECTOR="${6:-512}"

python3 - "$IN" "$OUT" "$PAGE" "$OOB" "$ECC" "$SECTOR" <<'PY'
import sys, struct
inp, out, page, oob, ecc, sector = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])

# ecc bytes per sector for BCH: m = 12 + sector/512 (Galois field degree)
ecc_bytes = ((12 + sector // 512) * ecc + 7) // 8
nb_sectors = page // sector
ecc_offset = oob - nb_sectors * ecc_bytes

# bitfield codes (pmecc_head.json): index into the "meaning" maps
sectors_code = {1: 0, 2: 1, 4: 2, 8: 3}[nb_sectors]
ecc_code     = {2: 0, 4: 1, 8: 2, 12: 3, 24: 4}[ecc]
sector_code  = {512: 0, 1024: 1}[sector]

word = (
    (1            << 0)   |  # usePmecc
    (sectors_code << 1)   |  # nbSectorPerPage  (bits 1-3)
    (oob          << 4)   |  # spareSize        (bits 4-12)
    (ecc_code     << 13)  |  # eccBitReq        (bits 13-15)
    (sector_code  << 16)  |  # sectorSize       (bits 16-17)
    (ecc_offset   << 18)  |  # eccOffset        (bits 18-26)
    (0xc          << 28)     # key = valid      (bits 28-31)
)

hdr = struct.pack("<I", word) * 52   # 208 bytes (0xd0); code follows at 0xd0
data = open(inp, "rb").read()
open(out, "wb").write(hdr + data)
print("PMECC header word 0x%08x (page=%d oob=%d ecc=%d sector=%d, eccOffset=%d); "
      "code at 0x%x; wrote %s (%d bytes)"
      % (word, page, oob, ecc, sector, ecc_offset, len(hdr), out, len(hdr) + len(data)))
PY
