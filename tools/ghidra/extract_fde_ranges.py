"""Extract executable, non-overlapping FDE ranges for bounded Ghidra batches."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TARGET_SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1"
PT_LOAD = 1
PF_X = 1
SHT_NOBITS = 8


def uleb(data, offset):
    value = shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift > 63:
            raise ValueError("oversized ULEB128")


def sleb(data, offset):
    value = shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            if byte & 0x40:
                value -= 1 << shift
            return value, offset
        if shift > 63:
            raise ValueError("oversized SLEB128")


def read_form(data, offset, form, pointer_size=8):
    if form == 0x00:
        return struct.unpack_from("<Q", data, offset)[0], offset + 8
    widths = {0x02: (2, False), 0x03: (4, False), 0x04: (8, False),
              0x0a: (2, True), 0x0b: (4, True), 0x0c: (8, True)}
    if form == 0x01:
        return uleb(data, offset)
    if form == 0x09:
        return sleb(data, offset)
    if form in widths:
        width, signed = widths[form]
        return int.from_bytes(data[offset:offset + width], "little", signed=signed), offset + width
    raise ValueError(f"unsupported DW_EH_PE format {form:#x}")


class Elf:
    def __init__(self, path, expected_sha256):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if self.sha256 != expected_sha256.lower():
            raise ValueError(f"engine SHA-256 mismatch: {self.sha256}")
        if self.data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", self.data, 18)[0] != 183:
            raise ValueError("expected little-endian ELF64 AArch64")
        phoff, shoff = struct.unpack_from("<QQ", self.data, 32)
        phentsize, phnum = struct.unpack_from("<HH", self.data, 54)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", self.data, 58)
        self.segments = [struct.unpack_from("<IIQQQQQQ", self.data, phoff + i * phentsize)
                         for i in range(phnum)]
        raw_sections = [struct.unpack_from("<IIQQQQIIQQ", self.data, shoff + i * shentsize)
                        for i in range(shnum)]
        names_sec = raw_sections[shstrndx]
        names = self.data[names_sec[4]:names_sec[4] + names_sec[5]]
        self.sections = {}
        for sec in raw_sections:
            start = sec[0]
            end = names.find(b"\0", start)
            name = names[start:end].decode("ascii") if end >= 0 else ""
            self.sections[name] = sec

    def file_offset(self, va, size=1):
        for sec in self.sections.values():
            if sec[1] != SHT_NOBITS and sec[3] <= va and va + size <= sec[3] + sec[5]:
                return sec[4] + va - sec[3]
        raise ValueError(f"address is not file-backed: {va:#x}")

    def executable_file_backed(self, start, end):
        return start < end and any(
            p[0] == PT_LOAD and p[1] & PF_X and p[3] <= start and end <= p[3] + p[5]
            and end <= p[3] + p[6]
            and p[2] + (end - p[3]) <= p[2] + p[5]
            and p[2] + (end - p[3]) <= len(self.data)
            for p in self.segments
        )

    def cie_encoding(self, cie_va):
        cie_off = self.file_offset(cie_va, 8)
        length = struct.unpack_from("<I", self.data, cie_off)[0]
        if length == 0xffffffff:
            length = struct.unpack_from("<Q", self.data, cie_off + 4)[0]
            offset = cie_off + 12
            end = offset + length
        else:
            offset = cie_off + 4
            end = offset + length
        if end > len(self.data):
            raise ValueError("CIE extends past ELF file")
        cie_id = struct.unpack_from("<I", self.data, offset)[0]
        if cie_id != 0:
            raise ValueError("FDE points to a non-CIE record")
        offset += 4
        version = self.data[offset]
        offset += 1
        nul = self.data.index(0, offset, end)
        augmentation = self.data[offset:nul].decode("ascii")
        offset = nul + 1
        _, offset = uleb(self.data, offset)
        _, offset = sleb(self.data, offset)
        if version == 1:
            offset += 1
        else:
            _, offset = uleb(self.data, offset)
        encoding = None
        if augmentation.startswith("z"):
            aug_size, offset = uleb(self.data, offset)
            aug_end = offset + aug_size
            for char in augmentation[1:]:
                if char == "R":
                    encoding = self.data[offset]
                    offset += 1
                elif char == "L":
                    offset += 1
                elif char == "P":
                    pointer_encoding = self.data[offset]
                    offset += 1
                    _, offset = read_form(self.data, offset, pointer_encoding & 0x0f)
                elif char == "S":
                    continue
                else:
                    raise ValueError(f"unsupported CIE augmentation {char!r}")
            if offset > aug_end:
                raise ValueError("CIE augmentation overrun")
        if encoding is None:
            encoding = 0x00
        return encoding

    def decode_pointer(self, encoding, offset, field_va):
        if encoding == 0xff:
            raise ValueError("omitted pointer")
        if encoding & 0x80:
            raise ValueError("indirect DW_EH_PE pointer unsupported")
        application = encoding & 0x70
        value, next_offset = read_form(self.data, offset, encoding & 0x0f)
        if application == 0x10:
            value += field_va
        elif application not in (0x00,):
            raise ValueError(f"unsupported DW_EH_PE application {application:#x}")
        return value, next_offset

    def fde_range(self, fde_va):
        offset = self.file_offset(fde_va, 8)
        length = struct.unpack_from("<I", self.data, offset)[0]
        if length == 0xffffffff:
            length = struct.unpack_from("<Q", self.data, offset + 4)[0]
            cie_field = offset + 12
            id_size = 8
            end = cie_field + length
        else:
            cie_field = offset + 4
            id_size = 4
            end = cie_field + length
        if end > len(self.data) or length < id_size:
            raise ValueError("FDE record has invalid length")
        cie_delta = struct.unpack_from("<I", self.data, cie_field)[0] if id_size == 4 else struct.unpack_from("<Q", self.data, cie_field)[0]
        cie_va = fde_va + (cie_field - offset) - cie_delta
        encoding = self.cie_encoding(cie_va)
        cursor = cie_field + id_size
        start_field_va = fde_va + (cursor - offset)
        start, cursor = self.decode_pointer(encoding, cursor, start_field_va)
        if cursor >= end:
            raise ValueError("FDE omits range length")
        size, _ = read_form(self.data, cursor, encoding & 0x0f)
        return start, size

    def fde_ranges(self):
        header = self.sections[".eh_frame_hdr"]
        frame = self.sections[".eh_frame"]
        base_va, file_off, size = header[3], header[4], header[5]
        raw = self.data[file_off:file_off + size]
        if len(raw) < 12 or raw[0] != 1:
            raise ValueError("invalid .eh_frame_hdr")
        frame_enc, count_enc, table_enc = raw[1:4]
        if (frame_enc, count_enc, table_enc) != (0x1b, 0x03, 0x3b):
            raise ValueError(f"unexpected .eh_frame_hdr encodings: {frame_enc:#x}/{count_enc:#x}/{table_enc:#x}")
        frame_ptr_delta = struct.unpack_from("<i", raw, 4)[0]
        frame_ptr = base_va + 4 + frame_ptr_delta
        count = struct.unpack_from("<I", raw, 8)[0]
        if frame_ptr < frame[3] or frame_ptr >= frame[3] + frame[5]:
            raise ValueError(".eh_frame_hdr points outside .eh_frame")
        if 12 + count * 8 > len(raw):
            raise ValueError("truncated .eh_frame_hdr table")
        seen = set()
        previous_start = -1
        valid = []
        rejected = []
        duplicates = overlaps = nonexec = invalid = 0
        for index in range(count):
            entry = 12 + index * 8
            start = base_va + struct.unpack_from("<i", raw, entry)[0]
            fde_va = base_va + struct.unpack_from("<i", raw, entry + 4)[0]
            try:
                if start in seen:
                    duplicates += 1
                    rejected.append((start, fde_va, "duplicate_start"))
                    continue
                seen.add(start)
                if start < previous_start:
                    raise ValueError("unsorted .eh_frame_hdr entries")
                previous_start = start
                fde_start, length = self.fde_range(fde_va)
                if fde_start != start or length <= 0:
                    raise ValueError("FDE/header start mismatch or empty range")
                end = start + length
                if not self.executable_file_backed(start, end):
                    nonexec += 1
                    rejected.append((start, fde_va, "not_executable_file_backed"))
                    continue
                if valid and start < valid[-1][1]:
                    overlaps += 1
                    rejected.append((start, fde_va, "overlaps_previous_range"))
                    continue
                fde_file = self.file_offset(fde_va)
                valid.append((start, end, fde_file))
            except (IndexError, KeyError, OverflowError, struct.error, ValueError) as error:
                invalid += 1
                rejected.append((start, fde_va, f"invalid:{error}"))
        return valid, rejected, {"header_entries": count, "valid_executable_ranges": len(valid),
                                "duplicates": duplicates, "overlaps": overlaps,
                                "non_executable_or_unbacked": nonexec, "invalid": invalid}


def select_anchors(ranges, anchors, neighbours):
    selected = set()
    missing = []
    for anchor in anchors:
        found = None
        lo, hi = 0, len(ranges)
        while lo < hi:
            mid = (lo + hi) // 2
            if ranges[mid][0] <= anchor:
                lo = mid + 1
            else:
                hi = mid
        index = lo - 1
        if index >= 0 and ranges[index][0] <= anchor < ranges[index][1]:
            found = index
            for neighbor in range(max(0, index - neighbours), min(len(ranges), index + neighbours + 1)):
                selected.add(neighbor)
        else:
            missing.append(anchor)
    return [ranges[i] for i in sorted(selected)], missing


def write_ranges(path, ranges, sha):
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write(f"# sha256={sha} image=ELF_VA fields=start,end,fde_file_offset\n")
        for start, end, fde_file in ranges:
            output.write(f"0x{start:x}\t0x{end:x}\t0x{fde_file:x}\n")


def self_test():
    assert uleb(bytes.fromhex("e58e26"), 0) == (624485, 3)
    assert sleb(bytes.fromhex("9bf159"), 0) == (-624485, 3)
    assert read_form(bytes.fromhex("78563412"), 0, 0x03) == (0x12345678, 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, nargs="?")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--expected-sha256", default=TARGET_SHA256)
    parser.add_argument("--anchor", action="append", default=[], help="ELF virtual address; repeatable")
    parser.add_argument("--neighbours", type=int, default=0, help="adjacent FDEs to include around each anchor")
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        print("FDE parser self-tests passed")
        return
    if args.engine is None:
        parser.error("engine path is required unless --self-test is used")
    if args.output_dir is None:
        parser.error("--output-dir is required unless --self-test is used")
    if args.batch_size < 1 or args.neighbours < 0:
        parser.error("batch-size must be positive and neighbours nonnegative")

    elf = Elf(args.engine, args.expected_sha256)
    ranges, rejected, counts = elf.fde_ranges()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_ranges(args.output_dir / "validated-fde-ranges.tsv", ranges, elf.sha256)
    with (args.output_dir / "rejected-fde-ranges.tsv").open("w", encoding="utf8", newline="\n") as output:
        output.write("start\tfde_va\treason\n")
        for start, fde_va, reason in rejected:
            output.write(f"0x{start:x}\t0x{fde_va:x}\t{reason}\n")

    anchors = [int(value, 0) for value in args.anchor]
    selected, missing = select_anchors(ranges, anchors, args.neighbours)
    write_ranges(args.output_dir / "selected-fde-ranges.tsv", selected, elf.sha256)
    for start in range(0, len(selected), args.batch_size):
        write_ranges(args.output_dir / f"selected-batch-{start // args.batch_size:04d}.tsv",
                     selected[start:start + args.batch_size], elf.sha256)
    summary = {"sha256": elf.sha256, "architecture": "AArch64", **counts,
               "selected_ranges": len(selected), "missing_anchors": [hex(x) for x in missing],
               "note": "FDE ranges are candidates, not proof of unique C++ functions."}
    (args.output_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf8")
    print(json.dumps(summary, separators=(",", ":")))


if __name__ == "__main__":
    main()
