"""Read-only ELF identity and relocation-backed RTTI discovery. No engine execution."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TARGET_SHA256 = "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a"
NAMES = ("21ChestScreenController", "25ContainerScreenController", "15ChestBlockActor",
         "21ContainerManagerModel", "9Inventory", "16ScreenController",
         "27ComplexInventoryTransaction", "27ItemUseInventoryTransaction", "26InventoryTransactionPacket",
         "9ItemStack")


def inspect(data):
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
        raise ValueError("Expected little-endian ELF64 AArch64")
    offset = struct.unpack_from("<Q", data, 40)[0]
    stride, count, strings_index = struct.unpack_from("<HHH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * stride) for i in range(count)]
    strings = sections[strings_index]
    names = data[strings[4]:strings[4] + strings[5]]
    sections = {names[s[0]:names.index(0, s[0])].decode(): s for s in sections}

    def file_offset(va):
        for section in sections.values():
            if section[1] != 8 and section[3] <= va < section[3] + section[5]:
                return section[4] + va - section[3]
        raise ValueError(f"Not file-backed: {va:#x}")

    ro = sections[".rodata"]
    rela = sections[".rela.dyn"]
    relatives = {}
    reverse = {}
    for i in range(rela[4], rela[4] + rela[5], rela[9]):
        location, info, addend = struct.unpack_from("<QQq", data, i)
        if info & 0xffffffff == 1027:  # ELF R_AARCH64_RELATIVE
            relatives[location] = addend
            reverse.setdefault(addend, []).append(location)
    result = {"sha256": hashlib.sha256(data).hexdigest(), "architecture": "AArch64",
              "rtti": {}, "limitations": ["RTTI/vtable candidates only; virtual method meanings and signatures are unverified."]}
    for name in NAMES:
        needle = b"\0" + name.encode() + b"\0"
        index = data.find(needle, ro[4], ro[4] + ro[5])
        if index < 0:
            result["rtti"][name] = {"found": False}
            continue
        va = index + 1 - ro[4] + ro[3]
        candidates = []
        for name_ref in reverse.get(va, []):
            typeinfo = name_ref - 8
            for type_ref in reverse.get(typeinfo, []):
                # Itanium ABI: offset-to-top, typeinfo pointer, then virtual function pointers.
                top = struct.unpack_from("<q", data, file_offset(type_ref - 8))[0]
                if top != 0:
                    continue
                slots = []
                for slot in range(8):
                    address = relatives.get(type_ref + 8 + slot * 8)
                    text = sections[".text"]
                    if address is None or not text[3] <= address < text[3] + text[5]:
                        break
                    slots.append(hex(address))
                if slots:
                    candidates.append({"typeinfo": hex(typeinfo), "vtable_address_point": hex(type_ref + 8), "slots": slots})
        result["rtti"][name] = {"found": True, "name_va": hex(va), "vtables": candidates}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--expected-sha256",
        default=TARGET_SHA256,
        help="Exact expected ELF SHA-256 (defaults to the original 1.26.52.3 target)",
    )
    args = parser.parse_args()
    data = args.engine.read_bytes()
    if hashlib.sha256(data).hexdigest() != args.expected_sha256.lower():
        raise SystemExit("Engine SHA-256 mismatch; target profile rejected")
    result = inspect(data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    seeds = []
    for name, item in result["rtti"].items():
        for table in item.get("vtables", []):
            for slot, address in enumerate(table["slots"][2:4], start=2):
                seeds.append(f"{name}.slot{slot} {address}")
    args.output.with_suffix(".seeds").write_text("\n".join(seeds) + "\n", encoding="utf8")
    contract_seeds = []
    vtable_seeds = []
    for name, item in result["rtti"].items():
        for table_index, table in enumerate(item.get("vtables", [])):
            vtable_seeds.append(
                f"{name}.vtable{table_index} {table['vtable_address_point']} {table['typeinfo']}"
            )
            for slot, address in enumerate(table["slots"]):
                contract_seeds.append(f"{name}.vtable{table_index}.slot{slot} {address}")
    args.output.with_suffix(".contracts.seeds").write_text("\n".join(contract_seeds) + "\n", encoding="utf8")
    args.output.with_suffix(".contracts.vtables").write_text("\n".join(vtable_seeds) + "\n", encoding="utf8")
    print(f"Verified engine; {sum(bool(v.get('vtables')) for v in result['rtti'].values())} classes with relocation-backed vtables. {args.output}")


if __name__ == "__main__":
    main()
