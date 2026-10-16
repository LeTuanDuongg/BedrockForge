"""Bounded relocation-backed vtable evidence for the exact gameplay experiment ELF."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1":
        raise ValueError("Exact 1.26.50.4 engine required")
    table = struct.unpack_from("<Q", data, 40)[0]
    stride, count, names_index = struct.unpack_from("<HHH", data, 58)
    rows = [struct.unpack_from("<IIQQQQIIQQ", data, table + i * stride) for i in range(count)]
    name_row = rows[names_index]
    names = data[name_row[4]:name_row[4] + name_row[5]]
    sections = {names[s[0]:names.index(0, s[0])].decode(): s for s in rows}

    def offset(va):
        for s in rows:
            if s[1] != 8 and s[3] <= va < s[3] + s[5]:
                return s[4] + va - s[3]
        raise ValueError(f"Unmapped address {va:x}")

    rela = sections[".rela.dyn"]
    relocations = {}
    for off in range(rela[4], rela[4] + rela[5], rela[9]):
        location, info, addend = struct.unpack_from("<QQq", data, off)
        if info & 0xffffffff == 1027:
            relocations[location] = addend
    result = {"sha256": digest, "tables": {}}
    ro, text = sections[".rodata"], sections[".text"]
    for name in ("16CobblestoneBlock", "12BedrockBlock", "11ButtonBlock", "11BlockLegacy", "5Block"):
        pos = data.find(b"\0" + name.encode() + b"\0", ro[4], ro[4] + ro[5])
        if pos < 0:
            continue
        name_va = pos + 1 - ro[4] + ro[3]
        typeinfos = {loc - 8 for loc, value in relocations.items() if value == name_va}
        tables = []
        for loc, value in relocations.items():
            if value not in typeinfos or struct.unpack_from("<q", data, offset(loc - 8))[0] != 0:
                continue
            slots = []
            for index in range(220):
                address = relocations.get(loc + 8 + index * 8)
                if address is None or not text[3] <= address < text[3] + text[5]:
                    break
                code = data[offset(address):offset(address) + 32]
                slots.append({"slot": index, "address": hex(address), "bytes": code.hex()})
            tables.append({"vtable": hex(loc + 8), "typeinfo": hex(value), "slots": slots})
        result["tables"][name] = tables
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = inspect(args.engine)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for name, tables in result["tables"].items():
        print(name, [(t["vtable"], len(t["slots"])) for t in tables])
