"""One-shot, read-only registry value tracing in the exact Android test engine.

Import inside NDK LLDB after gdb-remote attaches at the Minecraft menu, then call
run(lldb.debugger, local_engine_path, output_json, verified_load_bias). Hardware watchpoints alter
debug state only; this module never calls an engine function or writes memory.
Pass identifier="minecraft:cobblestone" to follow a bounded set of registry
insertions using hardware execution breakpoints, then watch that node's value.
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
INSERT_VA = 0x94F6850
POINTER_MASK = 0x00FFFFFFFFFFFFFF


def canonical_pointer(raw):
    return raw & POINTER_MASK


def registry_key_name(data, read):
    """Observed hash64 + libc++ string, bounded to the diagnostic's vanilla keys."""
    if len(data) != 0x28:
        raise ValueError("incomplete registry key")
    header = data[8]
    if header & 1:
        size, pointer = struct.unpack_from("<QQ", data, 16)
        if size > 96:
            return None
        value = read(pointer, size)
    else:
        size = header >> 1
        if size > 22:
            return None
        value = data[9:9 + size]
    return value.decode("ascii", errors="replace")


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


def run(debugger, engine_path, output_path, load_bias, timeout=180, identifier="minecraft:air",
        observe_default_state=False):
    import lldb

    segments = []
    output = Path(output_path)
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    events = []
    report = {"engine_sha256": ENGINE_SHA256, "pid": process.GetProcessID(),
              "load_bias": hex(load_bias), "identifier": "minecraft:cobblestone" if observe_default_state else identifier,
              "events": events, "engine_memory_writes": 0}
    watches = []
    breakpoints = []
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
        frames = []
        for index in range(16):
            frame = thread.GetFrameAtIndex(index)
            if not frame.IsValid():
                break
            frames.append({"pc": hex(frame.GetPC()), "elf_va": hex(frame.GetPC() - load_bias)
                           if in_engine(frame.GetPC(), 4) else None, "symbol": frame.GetFunctionName()})
        return frames

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

    def resume_until_stop(seconds, reason=lldb.eStopReasonWatchpoint):
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
                stopped = [t for t in process if t.GetStopReason() == reason]
                if not stopped:
                    emit("unexpected_stop", threads=[context(t) for t in process
                         if t.GetStopReason() not in (lldb.eStopReasonNone, lldb.eStopReasonInvalid)])
                    raise RuntimeError("unexpected stop; preserving original signal behavior")
                return stopped[0]
            event = lldb.SBEvent()
            listener.WaitForEvent(1, event)
        raise TimeoutError("bounded hardware-watchpoint observation timed out")

    def hardware_break(address):
        before = {target.GetBreakpointAtIndex(i).GetID() for i in range(target.GetNumBreakpoints())}
        result = lldb.SBCommandReturnObject()
        debugger.GetCommandInterpreter().HandleCommand(
            "breakpoint set --hardware --address " + hex(address), result)
        created = [target.GetBreakpointAtIndex(i) for i in range(target.GetNumBreakpoints())
                   if target.GetBreakpointAtIndex(i).GetID() not in before]
        breakpoints.extend(item.GetID() for item in created)
        if not result.Succeeded() or len(created) != 1 or not created[0].IsHardware():
            raise RuntimeError("hardware execution breakpoint unavailable: " + result.GetError())
        return created[0].GetID()

    def remove_break(item):
        if not target.BreakpointDelete(item):
            raise RuntimeError("hardware breakpoint cleanup failed")
        breakpoints.remove(item)

    def key_name(address):
        return registry_key_name(read(address, 0x28), read)

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
        if identifier not in ("minecraft:air", "minecraft:cobblestone", "minecraft:stone"):
            raise ValueError("only the three predefined vanilla observations are supported")
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
        if observe_default_state:
            # Stop before the base constructor: it may create the state itself.
            call_va = 0x100FB850
            expected = struct.pack("<I", 0x94000000 | ((0x101076DC - call_va) // 4))
            if not any(flags & 1 and va <= call_va and call_va + 4 <= va + size
                       for va, _, flags, _, size in segments):
                raise ValueError("base-constructor call outside executable ELF segment")
            if read(load_bias + call_va, 4) != expected:
                raise ValueError("base-constructor BL mismatch")
            entry = hardware_break(load_bias + call_va)
            emit("pre_base_constructor_armed", call_elf_va=hex(call_va),
                 phase="waiting_for_vanilla_cobblestone_construction")
            thread = resume_until_stop(timeout, lldb.eStopReasonBreakpoint)
            frame = thread.GetFrameAtIndex(0)
            registers = {"x" + str(i): frame.FindRegister("x" + str(i)).GetValueAsUnsigned()
                         for i in range(9)}
            block_type = canonical_pointer(registers["x0"])
            if frame.GetPC() != load_bias + call_va or not block_type or block_type % 8:
                raise ValueError("unexpected pre-constructor stop or invalid this")
            if canonical_pointer(frame.FindRegister("x19").GetValueAsUnsigned()) != block_type:
                raise ValueError("constructor this/x19 mismatch")
            field = block_type + 0x200
            before = word(field)
            state_watch = watch(field)
            emit("default_state_watch_armed", **context(thread), block_type=hex(block_type),
                 field=hex(field), initial_uninitialized_bits=hex(before),
                 registers={key: hex(value) for key, value in registers.items()})
            remove_break(entry)
            for index in range(12):
                thread = resume_until_stop(30)
                pc = thread.GetFrameAtIndex(0).GetPC()
                after = word(field)
                details = describe_value(after) if after else {"raw_pointer": "0x0"}
                emit("default_state_field_write", **context(thread), sequence=index,
                     block_type=hex(block_type), before=hex(before), after=hex(after),
                     registers={"x" + str(i): hex(thread.GetFrameAtIndex(0).FindRegister(
                         "x" + str(i)).GetValueAsUnsigned()) for i in range(31)},
                     instruction_at_pc=read(pc, 4).hex(), instruction_before_pc=read(pc - 4, 4).hex(),
                     value=details)
                before = after
                if after:
                    if details.get("rtti") != "5Block":
                        raise ValueError("non-null assigned pointer is not validated Block RTTI")
                    owner = word(canonical_pointer(after) + 0x68)
                    emit("default_state_owner", block_type=hex(block_type), state=hex(after),
                         owner=hex(owner), owner_matches=canonical_pointer(owner) == block_type,
                         type_value=describe_value(block_type))
                    report["result"] = ("observed_default_state_producer" if
                                        canonical_pointer(owner) == block_type else "owner_mismatch")
                    return
            raise RuntimeError("bounded default-state write count exhausted")
        registry = load_bias + REGISTRY_VA
        root_field = registry + 8
        root_before = word(root_field)
        if root_before:
            raise ValueError("registry already populated; start at the menu in a fresh test process")
        if identifier == "minecraft:air":
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
            if not target.DeleteWatchpoint(root_watch):
                raise RuntimeError("root watchpoint cleanup failed")
            watches.remove(root_watch)
        else:
            for va, _, flags, file_offset, file_size in segments:
                if flags & 1 and va <= INSERT_VA and INSERT_VA + 16 <= va + file_size:
                    with Path(engine_path).open("rb") as source:
                        source.seek(file_offset + INSERT_VA - va)
                        if read(load_bias + INSERT_VA, 16) != source.read(16):
                            raise ValueError("runtime insertion entry bytes mismatch")
                    break
            else:
                raise ValueError("insertion entry outside executable file-backed segment")
            entry = hardware_break(load_bias + INSERT_VA)
            emit("ordinary_insert_watch_armed", registry=hex(registry), identifier=identifier)
            deadline = time.monotonic() + timeout
            for attempt in range(256):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("ordinary block insertion not observed in time")
                thread = resume_until_stop(remaining, lldb.eStopReasonBreakpoint)
                frame = thread.GetFrameAtIndex(0)
                if frame.GetPC() != load_bias + INSERT_VA:
                    raise ValueError("unexpected insertion breakpoint PC")
                reg = lambda name: frame.FindRegister(name).GetValueAsUnsigned()
                if canonical_pointer(reg("x0")) != registry:
                    continue
                name = key_name(reg("x1"))
                emit("registry_insert_entry", **context(thread), key=name, attempt=attempt,
                     registers={"x" + str(i): hex(reg("x" + str(i))) for i in range(9)},
                     lr=hex(reg("x30")))
                if name == identifier:
                    break
            else:
                raise RuntimeError("bounded insertion hit limit reached")
            owner_tid = thread.GetThreadID()
            return_pc = canonical_pointer(reg("x30"))
            if not in_engine(return_pc, 4):
                raise ValueError("insertion return PC outside engine")
            remove_break(entry)
            returned = hardware_break(return_pc)
            thread = resume_until_stop(30, lldb.eStopReasonBreakpoint)
            if thread.GetThreadID() != owner_tid or thread.GetFrameAtIndex(0).GetPC() != return_pc:
                raise ValueError("insertion return thread/PC mismatch")
            node = canonical_pointer(thread.GetFrameAtIndex(0).FindRegister("x0").GetValueAsUnsigned())
            if not node or key_name(node + 0x20) != identifier:
                raise ValueError("returned node key does not match observed insertion arguments")
            emit("ordinary_insert_return", **context(thread), node=hex(node), key=identifier)
            remove_break(returned)
        snapshot = read(node, 0x50)
        initial_value = struct.unpack_from("<Q", snapshot, 0x48)[0]
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
        for item in breakpoints:
            target.BreakpointDelete(item)
        if process.GetState() == lldb.eStateStopped:
            error = process.Detach()
            emit("detach", success=error.Success(), detail=str(error))
        else:
            emit("detach_unavailable", process_state=process.GetState())
        debugger.SetAsync(old_async)
