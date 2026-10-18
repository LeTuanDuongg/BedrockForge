"""Bounded, read-only snapshot of the observed exact-target CobblestoneBlock.

Run only in the debuggable HybridMCPE test process. No engine calls or writes.
"""
import json
from pathlib import Path
import struct
import time

from lldb_registry_value import ENGINE_SHA256, canonical_pointer, engine_layout


def run(debugger, engine_path, output_path, load_bias, block_type):
    import lldb
    process = debugger.GetSelectedTarget().GetProcess()
    result = {"engine_sha256": ENGINE_SHA256, "pid": process.GetProcessID(),
              "host_monotonic_ns": time.monotonic_ns(), "result": "incomplete"}
    segments, _ = engine_layout(engine_path)

    def read(address, size):
        error = lldb.SBError()
        data = process.ReadMemory(canonical_pointer(address), size, error)
        if not error.Success() or len(data) != size:
            raise ValueError("unreadable bounded field: " + hex(address))
        return data

    def pointer(address):
        return struct.unpack("<Q", read(address, 8))[0]

    def mapped(address, size):
        return any(load_bias + va <= address and address + size <= load_bias + va + length
                   for va, length, _, _, _ in segments)

    def rtti(obj):
        vptr = pointer(obj)
        if not mapped(vptr - 8, 16):
            raise ValueError("vtable outside exact engine mappings")
        ti = pointer(vptr - 8)
        if not mapped(ti, 16):
            raise ValueError("typeinfo outside exact engine mappings")
        name = pointer(ti + 8)
        text = bytearray()
        for i in range(96):
            if not mapped(name + i, 1):
                raise ValueError("RTTI name outside engine")
            value = read(name + i, 1)
            if value == b"\0":
                return {"vptr": hex(vptr), "vtable_elf_va": hex(vptr - load_bias),
                        "typeinfo_elf_va": hex(ti - load_bias), "name": text.decode("ascii")}
            text.extend(value)
        raise ValueError("RTTI name limit")

    try:
        if process.GetState() != lldb.eStateStopped:
            raise ValueError("requires stopped supported debug process")
        if read(load_bias, 6) != b"\x7fELF\x02\x01":
            raise ValueError("load bias mismatch")
        result["block_type"] = {"pointer": hex(block_type), **rtti(block_type)}
        if pointer(block_type) != load_bias + 0x130D6190:
            raise ValueError("not the validated CobblestoneBlock vtable")
        begin, end, capacity, primary = struct.unpack("<4Q", read(block_type + 0x1E8, 32))
        result["state_fields"] = dict(zip(("begin", "end", "capacity", "primary"),
                                           map(hex, (begin, end, capacity, primary))))
        if end < begin or capacity < end or (end - begin) % 8 or (end - begin) // 8 > 4096:
            raise ValueError("invalid observed state-vector bounds")
        result["state_count"] = (end - begin) // 8
        if not primary:
            result["result"] = "primary_unavailable"
        else:
            state = {"pointer": hex(primary), **rtti(primary)}
            result["primary"] = state
            if state["name"] != "5Block":
                raise ValueError("primary RTTI is not Block; owner field not read")
            owner = pointer(primary + 0x68)
            state.update(owner=hex(owner), owner_matches=canonical_pointer(owner) == canonical_pointer(block_type),
                         first_matches=bool(begin) and pointer(begin) == primary,
                         ordinal11c=struct.unpack("<I", read(primary + 0x11C, 4))[0],
                         assigned12a=read(primary + 0x12A, 1)[0])
            result["result"] = "type_state_edge_validated" if state["owner_matches"] and state["first_matches"] else "relationship_mismatch"
    except Exception as error:
        result["error"] = str(error)
    finally:
        if process.GetState() == lldb.eStateStopped:
            error = process.Detach()
            result["detached"] = error.Success()
        Path(output_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
        print("block_type_state_snapshot=" + json.dumps(result))
