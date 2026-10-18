"""Find direct AArch64 BL callsites to selected ELF VAs and map them to FDE ranges."""
import argparse
import bisect
import hashlib
from pathlib import Path
import struct
import sys

TARGET_SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1"
PT_LOAD, PF_X = 1, 1


def sign_extend(value, width):
    sign = 1 << (width - 1)
    return value - (1 << width) if value & sign else value


def load_fdes(path, sha256):
    rows, header_hash = [], None
    with Path(path).open(encoding="ascii") as f:
        for line in f:
            if line.startswith("#"):
                for field in line[1:].split():
                    if field.startswith("sha256="):
                        header_hash = field.split("=", 1)[1]
                continue
            if not line.strip():
                continue
            start, end, _ = (int(value, 0) for value in line.split())
            if start >= end or (rows and start < rows[-1][1]):
                raise ValueError("FDE ranges are invalid, unsorted, or overlapping")
            rows.append((start, end))
    if header_hash != sha256:
        raise ValueError("FDE TSV engine SHA-256 mismatch")
    return rows, [row[0] for row in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--fde-tsv", type=Path, required=True)
    parser.add_argument("--target", action="append", required=True, help="ELF virtual address; repeatable")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-sha256", default=TARGET_SHA256)
    args = parser.parse_args()
    if sys.byteorder != "little":
        parser.error("host must be little-endian for memoryview.cast")

    data = args.engine.read_bytes()
    sha256 = hashlib.sha256(data).hexdigest()
    if sha256 != args.expected_sha256.lower():
        raise SystemExit(f"engine SHA-256 mismatch: {sha256}")
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
        raise SystemExit("expected little-endian ELF64 AArch64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    segments = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize) for i in range(phnum)]
    targets = {int(value, 0) for value in args.target}
    ranges, starts = load_fdes(args.fde_tsv, sha256)
    found = {target: [] for target in targets}
    scanned_bytes = 0

    for segment in segments:
        p_type, flags, file_off, va, _, file_size, mem_size, _ = segment
        if p_type != PT_LOAD or not flags & PF_X or file_size == 0:
            continue
        if file_off + file_size > len(data) or file_size > mem_size or va & 3 or file_size & 3:
            raise ValueError("malformed executable PT_LOAD segment")
        words = memoryview(data)[file_off:file_off + file_size].cast("I")
        scanned_bytes += file_size
        for index, word in enumerate(words):
            if word & 0xFC000000 != 0x94000000:
                continue
            pc = va + index * 4
            imm = sign_extend(word & 0x03FFFFFF, 26)
            target = pc + (imm << 2)
            if target not in found:
                continue
            caller = pc
            fde_index = bisect.bisect_right(starts, caller) - 1
            fde = ranges[fde_index] if fde_index >= 0 and caller < ranges[fde_index][1] else None
            found[target].append((caller, fde))

    lines = [f"sha256={sha256}", f"architecture=AArch64 scanned_executable_bytes={scanned_bytes}",
             f"fde_ranges={len(ranges)} targets={len(targets)}"]
    for target in sorted(found):
        lines.append(f"target=0x{target:x} direct_bl_callers={len(found[target])}")
        for caller, fde in found[target]:
            if fde:
                lines.append(f"  callsite=0x{caller:x} caller_fde=0x{fde[0]:x}..0x{fde[1]:x}")
            else:
                lines.append(f"  callsite=0x{caller:x} caller_fde=UNKNOWN")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
