"""One-shot, read-only registry value tracing in the exact Android test engine.

Import inside NDK LLDB after gdb-remote attaches at the Minecraft menu, then call
run(lldb.debugger, local_engine_path, output_json, verified_load_bias). Hardware watchpoints alter
debug state only; this module never calls an engine function or writes memory.
"""
import hashlib
import json
from pathlib import Path
import struct
import time

ENGINE_SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1"
REGISTRY_VA = 0x134D7DB8
GETTER_VA = 0x103E2D54
WRITER_VA = 0x94F691C
POINTER_MASK = 0x00FFFFFFFFFFFFFF


def canonical_pointer(raw):
    return raw & POINTER_MASK


def engine_layout(path):
    """Admit only the pinned ELF, retaining VAs separately from file offsets."""
    path = Path(path)
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
        if digest != ENGINE_SHA256:
            raise ValueError("engine SHA-256 mismatch")
        source.seek(0)
        header = source.read(64)
        if header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != 183:
            raise ValueError("expected ELF64 little-endian AArch64")
        offset = struct.unpack_from("<Q", header, 32)[0]
        size, count = struct.unpack_from("<HH", header, 54)
        if size != 56 or not 1 <= count <= 64:
            raise ValueError("unsupported program-header table")
        source.seek(offset)
        segments = []
        for _ in range(count):
            kind, flags, file_offset, va, _, file_size, memory_size, _ = struct.unpack("<II6Q", source.read(size))
            if kind == 1:
                segments.append((va, memory_size, flags, file_offset, file_size))
        for va, _, flags, file_offset, file_size in segments:
            if flags & 1 and va <= WRITER_VA < va + file_size:
                source.seek(file_offset + WRITER_VA - va)
                writer = source.read(4)
                if writer != struct.pack("<I", 0xF90002F5):  # str x21, [x23]
                    raise ValueError("writer instruction mismatch")
                return segments, writer
    raise ValueError("writer outside executable file-backed PT_LOAD")


def run(debugger, engine_path, output_path, load_bias, timeout=180):
    import lldb

    segments = []
    output = Path(output_path)
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    events = []
    report = {"engine_sha256": ENGINE_SHA256, "pid": process.GetProcessID(),
              "load_bias": hex(load_bias), "events": events, "engine_memory_writes": 0}
    watches = []
    old_async = debugger.GetAsync()

    def emit(stage, **fields):
        event = {"stage": stage, "host_monotonic_ns": time.monotonic_ns(), **fields}
        events.append(event)
        temporary = output.with_suffix(".tmp")
        temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
        temporary.replace(output)
        print("registry_value_trace=" + json.dumps(event), flush=True)

    def read(address, size):
        error = lldb.SBError()
        data = process.ReadMemory(canonical_pointer(address), size, error)
        if error.Fail() or len(data) != size:
            raise RuntimeError("unreadable target memory at " + hex(address) + ": " + str(error))
        return data

    def word(address):
        return struct.unpack("<Q", read(address, 8))[0]

    def in_engine(address, size=8):
        address = canonical_pointer(address)
        return any(flags & 4 and load_bias + va <= address and
                   address + size <= load_bias + va + length for va, length, flags, _, _ in segments)

    def stack(thread):
        return [{"pc": hex(frame.GetPC()), "elf_va": hex(frame.GetPC() - load_bias)
                 if in_engine(frame.GetPC(), 4) else None, "symbol": frame.GetFunctionName()}
                for frame in list(thread)[:16]]

    def context(thread):
        frame = thread.GetFrameAtIndex(0)
        return {"tid": thread.GetThreadID(), "thread": thread.GetName(),
                "pc": hex(frame.GetPC()), "elf_va": hex(frame.GetPC() - load_bias),
                "stop_reason": thread.GetStopDescription(256), "stack": stack(thread)}

    def watch(address):
        error = lldb.SBError()
        item = target.WatchAddress(address, 8, False, True, error)
        if error.Fail() or not item.IsValid():
            raise RuntimeError("hardware watchpoint rejected: " + str(error))
        watches.append(item.GetID())
        return item.GetID()

    def resume_until_stop(seconds):
        stop_id = process.GetStopID()
        error = process.Continue()
        if error.Fail():
            raise RuntimeError("continue failed: " + str(error))
        deadline = time.monotonic() + seconds
        listener = debugger.GetListener()
        while time.monotonic() < deadline:
            state = process.GetState()
            if state in (lldb.eStateExited, lldb.eStateDetached, lldb.eStateInvalid):
                raise RuntimeError("target left debug session: " + str(state))
            if state == lldb.eStateStopped and process.GetStopID() > stop_id:
                stopped = [t for t in process if t.GetStopReason() == lldb.eStopReasonWatchpoint]
                if not stopped:
                    emit("unexpected_stop", threads=[context(t) for t in process
                         if t.GetStopReason() not in (lldb.eStopReasonNone, lldb.eStopReasonInvalid)])
                    raise RuntimeError("non-watchpoint stop; preserving original signal behavior")
                return stopped[0]
            event = lldb.SBEvent()
            listener.WaitForEvent(1, event)
        raise TimeoutError("bounded hardware-watchpoint observation timed out")

    def describe_value(raw):
        value = canonical_pointer(raw)
        data = {"raw_pointer": hex(raw), "canonical_pointer": hex(value)}
        if not value or value % 8:
            return data
        try:
            vptr = canonical_pointer(word(value))
            if not in_engine(vptr - 8, 16):
                return {**data, "validation": "vtable_outside_engine"}
            typeinfo = canonical_pointer(word(vptr - 8))
            if not in_engine(typeinfo, 16):
                return {**data, "validation": "typeinfo_outside_engine"}
            name = canonical_pointer(word(typeinfo + 8))
            text = bytearray()
            for i in range(128):
                if not in_engine(name + i, 1):
                    raise ValueError("RTTI name outside engine")
                char = read(name + i, 1)[0]
                if char == 0:
                    return {**data, "vptr": hex(vptr), "vtable_elf_va": hex(vptr - load_bias),
                            "typeinfo": hex(typeinfo), "rtti": text.decode("ascii"),
                            "validation": "engine_itanium_rtti_chain", "ownership": "unknown"}
                text.append(char)
            raise ValueError("RTTI name exceeds bound")
        except (RuntimeError, ValueError, UnicodeError) as error:
            return {**data, "validation": "unvalidated", "error": str(error)}

    try:
        segments, writer_bytes = engine_layout(engine_path)
        if process.GetState() != lldb.eStateStopped or not report["pid"]:
            raise RuntimeError("requires an attached stopped process")
        debugger.SetAsync(True)
        # Preserve the engine's existing fault handlers, including deliberate safe-read faults.
        signals = process.GetUnixSignals()
        for number in (7, 11, 34):
            signals.SetShouldStop(number, False)
            signals.SetShouldNotify(number, False)
            signals.SetShouldSuppress(number, False)
        if read(load_bias, 6) != b"\x7fELF\x02\x01":
            raise ValueError("runtime ELF load bias mismatch")
        if read(load_bias + WRITER_VA, 4) != writer_bytes:
            raise ValueError("runtime writer bytes mismatch")
        if read(load_bias + GETTER_VA, 12) != struct.pack("<3I", 0xB00187A0, 0x9136E000, 0xD65F03C0):
            raise ValueError("runtime registry getter mismatch")
        registry = load_bias + REGISTRY_VA
        root_field = registry + 8
        root_before = word(root_field)
        if root_before:
            raise ValueError("registry already populated; start at the menu in a fresh test process")
        root_watch = watch(root_field)
        emit("root_watch_armed", registry=hex(registry), root_field=hex(root_field), root_before=hex(root_before))
        thread = resume_until_stop(timeout)
        frame = thread.GetFrameAtIndex(0)
        pc = frame.GetPC()
        node = canonical_pointer(frame.FindRegister("x21").GetValueAsUnsigned())
        link = canonical_pointer(frame.FindRegister("x23").GetValueAsUnsigned())
        root = canonical_pointer(word(root_field))
        emit("root_write", **context(thread), root_after=hex(root), x21=hex(node), x23=hex(link))
        if pc - load_bias not in (WRITER_VA, WRITER_VA + 4) or link != root_field or node != root or not node:
            raise ValueError("root write does not establish the pinned new-node provenance")
        snapshot = read(node, 0x50)
        initial_value = struct.unpack_from("<Q", snapshot, 0x48)[0]
        target.DeleteWatchpoint(root_watch)
        watches.remove(root_watch)
        value_watch = watch(node + 0x48)
        emit("node_value_watch_armed", node=hex(node), field=hex(node + 0x48),
             initial_value=hex(initial_value), key_hex=snapshot[0x20:0x48].hex(),
             node_hex=snapshot.hex(), value_already_assigned=bool(initial_value))
        if initial_value:
            report["result"] = "observed_value_present_at_insertion"
            emit("value_present_at_insertion", value=describe_value(initial_value))
            return
        thread = resume_until_stop(30)
        value = word(node + 0x48)
        emit("node_value_write", **context(thread), node=hex(node), value_before=hex(initial_value),
             value_after=hex(value), key_unchanged=read(node + 0x20, 0x28) == snapshot[0x20:0x48],
             value=describe_value(value))
        report["result"] = "observed_value_write"
    except Exception as error:
        report["result"] = "incomplete"
        emit("error", error=str(error))
    finally:
        if process.GetState() == lldb.eStateRunning:
            process.SendAsyncInterrupt()
            deadline = time.monotonic() + 5
            while process.GetState() == lldb.eStateRunning and time.monotonic() < deadline:
                time.sleep(0.05)
        for item in watches:
            target.DeleteWatchpoint(item)
        if process.GetState() == lldb.eStateStopped:
            error = process.Detach()
            emit("detach", success=error.Success(), detail=str(error))
        else:
            emit("detach_unavailable", process_state=process.GetState())
        debugger.SetAsync(old_async)
