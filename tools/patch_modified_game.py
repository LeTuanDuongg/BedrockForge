"""Version-pinned local research APK copy; does not alter licensing or authentication."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile

APK_SHA = 'b618ac8afda57ec19e5614afbabdc85765d10224dbdc1ec47315a94243e2fdb9'
ENGINE_SHA = '53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a'
PACKAGE = 'dev.hybridmcpe.research'
ENGINE_ENTRY = 'lib/arm64-v8a/libminecraftpe.so'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_sha(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def patch_engine(original):
    require(sha(original) == ENGINE_SHA, 'Original engine SHA-256 mismatch')
    data = bytearray(original)
    require(data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 183,
            'Expected little-endian ELF64 ARM64')
    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    phsize, phnum, shsize, shnum, shstr = struct.unpack_from('<HHHHH', data, 54)
    require(phsize == 56 and shsize == 64, 'Unexpected ELF headers')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shsize) for i in range(shnum)]
    names_header = sections[shstr]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    named = {}
    for i, section in enumerate(sections):
        name = names[section[0]:].split(b'\0', 1)[0].decode('ascii')
        named[name] = (i, section)
    dyn_i, dyn = named['.dynamic']
    str_i, strings = named['.dynstr']
    require((strings[4], strings[5], dyn[4], dyn[5]) ==
            (0x2f4cd8, 0x7ff3a3, 0x13934000, 0x240), 'Unexpected target section layout')
    string_end = strings[4] + strings[5]
    library = b'libhybridmcpe.so\0'
    require(named['.rela.dyn'][1][4] - string_end >= len(library), 'No dynstr padding')
    require(not any(data[string_end:string_end + len(library)]), 'Dynstr padding is occupied')
    null = dyn[4] + dyn[5] - 16
    require(not any(data[null:null + 32]), 'Dynamic terminator padding is occupied')
    tags = [struct.unpack_from('<qQ', data, pos) for pos in range(dyn[4], null, 16)]
    require(all(tag != 0 for tag, _ in tags), 'Unexpected early dynamic terminator')
    require(sum(tag == 10 for tag, _ in tags) == 1, 'Expected one DT_STRSZ')
    allowed_changes = [(string_end, string_end + len(library)), (null, null + 16),
                       (shoff + str_i * shsize + 32, shoff + str_i * shsize + 40),
                       (shoff + dyn_i * shsize + 32, shoff + dyn_i * shsize + 40)]
    dynamic_ph = phoff + 8 * phsize
    require(struct.unpack_from('<I', data, dynamic_ph)[0] == 2, 'Expected PT_DYNAMIC index 8')
    require(struct.unpack_from('<Q', data, dynamic_ph + 8)[0] == dyn[4], 'PT_DYNAMIC offset mismatch')
    require(any(struct.unpack_from('<I', data, phoff + i * phsize)[0] == 1 and
                struct.unpack_from('<Q', data, phoff + i * phsize + 8)[0] <= null and
                struct.unpack_from('<Q', data, phoff + i * phsize + 8)[0] +
                struct.unpack_from('<Q', data, phoff + i * phsize + 32)[0] >= null + 32
                for i in range(phnum)), 'Dynamic padding is outside PT_LOAD')
    data[string_end:string_end + len(library)] = library
    struct.pack_into('<qQ', data, null, 1, strings[5])
    for index, (tag, value) in enumerate(tags):
        if tag == 10:
            require(value == strings[5], 'DT_STRSZ mismatch')
            struct.pack_into('<Q', data, dyn[4] + index * 16 + 8, value + len(library))
            allowed_changes.append((dyn[4] + index * 16 + 8, dyn[4] + index * 16 + 16))
    struct.pack_into('<Q', data, shoff + str_i * shsize + 32, strings[5] + len(library))
    struct.pack_into('<Q', data, shoff + dyn_i * shsize + 32, dyn[5] + 16)
    for offset in (32, 40):
        require(struct.unpack_from('<Q', data, dynamic_ph + offset)[0] == dyn[5], 'PT_DYNAMIC size mismatch')
        struct.pack_into('<Q', data, dynamic_ph + offset, dyn[5] + 16)
        allowed_changes.append((dynamic_ph + offset, dynamic_ph + offset + 8))
    require(len(data) == len(original), 'Engine length changed')
    cursor = 0
    for start, end in sorted(allowed_changes):
        require(data[cursor:start] == original[cursor:start], 'Unapproved engine bytes changed')
        cursor = end
    require(data[cursor:] == original[cursor:], 'Unapproved engine bytes changed')
    for name, (_, section) in named.items():
        if name in ('.text', '.rodata') or section[1] in (4, 9):
            start, size = section[4:6]
            require(data[start:start + size] == original[start:start + size], name + ' changed')
    for index, section in enumerate(sections):
        updated = struct.unpack_from('<IIQQQQIIQQ', data, shoff + index * shsize)
        require(updated[3:5] == section[3:5], 'Section address or offset changed')
    for index in range(phnum):
        start = phoff + index * phsize
        require(data[start + 8:start + 32] == original[start + 8:start + 32], 'Segment address or offset changed')
    return bytes(data)


def read_length(data, pos, utf8):
    if utf8:
        first = data[pos]
        return (((first & 127) << 8) | data[pos + 1], pos + 2) if first & 128 else (first, pos + 1)
    first = struct.unpack_from('<H', data, pos)[0]
    return (((first & 32767) << 16) | struct.unpack_from('<H', data, pos + 2)[0], pos + 4) if first & 32768 else (first, pos + 2)


def length_bytes(value, utf8):
    if utf8:
        return bytes([value]) if value < 128 else bytes([(value >> 8) | 128, value & 255])
    return struct.pack('<H', value) if value < 32768 else struct.pack('<HH', (value >> 16) | 32768, value & 65535)


def patch_manifest(original):
    require(struct.unpack_from('<HH', original)[0:2] == (3, 8), 'Expected binary Android XML')
    chunks, cursor = [], 8
    while cursor < len(original):
        kind, header, size = struct.unpack_from('<HHI', original, cursor)
        require(size >= header >= 8 and cursor + size <= len(original), 'Invalid XML chunk')
        chunks.append(bytearray(original[cursor:cursor + size]))
        cursor += size
    pools = [i for i, c in enumerate(chunks) if struct.unpack_from('<H', c)[0] == 1]
    require(len(pools) == 1, 'Expected one XML string pool')
    pool_index = pools[0]
    pool = chunks[pool_index]
    _, header, _, count, style_count, flags, string_start, style_start = struct.unpack_from('<HHIIIIII', pool)
    utf8 = bool(flags & 256)
    strings = []
    for i in range(count):
        pos = string_start + struct.unpack_from('<I', pool, header + i * 4)[0]
        size, pos = read_length(pool, pos, utf8)
        if utf8:
            size, pos = read_length(pool, pos, True)
        strings.append(bytes(pool[pos:pos + size * (1 if utf8 else 2)]).decode('utf-8' if utf8 else 'utf-16le'))

    def intern(value):
        if value not in strings:
            strings.append(value)
        return strings.index(value)

    original_package = None
    removed = 0
    output = []
    for chunk in chunks:
        kind = struct.unpack_from('<H', chunk)[0]
        if kind == 0x102:
            element = strings[struct.unpack_from('<I', chunk, 20)[0]]
            attr_start, attr_size, attr_count = struct.unpack_from('<HHH', chunk, 24)
            require(attr_size >= 20, 'Invalid XML attribute size')
            attrs = []
            for i in range(attr_count):
                pos = 16 + attr_start + i * attr_size
                _, name, raw, value_size, zero, typ, value = struct.unpack_from('<IIIHBBI', chunk, pos)
                text = strings[value] if typ == 3 else None
                attrs.append((pos, strings[name], text))
            if element == 'manifest':
                packages = [value for _, name, value in attrs if name == 'package']
                require(len(packages) == 1 and packages[0] == 'com.mojang.minecraftpe', 'Unexpected original package')
                original_package = packages[0]
            for pos, name, value in attrs:
                replacement = None
                if element == 'manifest' and name == 'package':
                    replacement = PACKAGE
                elif value and original_package and ((element == 'provider' and name == 'authorities') or
                      (element in ('permission', 'uses-permission', 'permission-tree', 'permission-group') and name == 'name')):
                    replacement = ';'.join(PACKAGE + item[len(original_package):]
                                           if item.startswith(original_package + '.') else item for item in value.split(';'))
                if replacement is not None and replacement != value:
                    index = intern(replacement)
                    if struct.unpack_from('<I', chunk, pos + 8)[0] != 0xffffffff:
                        struct.pack_into('<I', chunk, pos + 8, index)
                    struct.pack_into('<I', chunk, pos + 16, index)
        output.append(chunk)
    require(original_package is not None, 'Incomplete manifest')
    payload, offsets = bytearray(), []
    for value in strings:
        offsets.append(len(payload))
        raw = value.encode('utf-8' if utf8 else 'utf-16le')
        units = len(value.encode('utf-16le')) // 2
        payload += length_bytes(units, utf8)
        if utf8:
            payload += length_bytes(len(raw), True)
        payload += raw + (b'\0' if utf8 else b'\0\0')
    payload += b'\0' * (-len(payload) % 4)
    style_offsets = pool[header + count * 4:header + (count + style_count) * 4]
    styles = pool[style_start:] if style_start else b''
    new_start = header + (len(strings) + style_count) * 4
    new_pool = bytearray(pool[:header])
    new_pool += struct.pack('<' + 'I' * len(offsets), *offsets) + style_offsets + payload + styles
    struct.pack_into('<I', new_pool, 4, len(new_pool))
    struct.pack_into('<IIIII', new_pool, 8, len(strings), style_count, flags & ~1,
                     new_start, new_start + len(payload) if styles else 0)
    output[output.index(chunks[pool_index])] = new_pool
    result = b''.join(output)
    return struct.pack('<HHI', 3, 8, len(result) + 8) + result, removed


def prepare(apk, destination):
    apk = Path(apk).resolve()
    destination = Path(destination).resolve()
    require('mcpe_research' not in [part.lower() for part in destination.parts], 'Research originals are read-only')
    require(file_sha(apk) == APK_SHA, 'Original APK SHA-256 mismatch')
    with zipfile.ZipFile(apk) as source:
        engine = patch_engine(source.read(ENGINE_ENTRY))
        manifest, removed = patch_manifest(source.read('AndroidManifest.xml'))
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'libminecraftpe.so').write_bytes(engine)
    (destination / 'AndroidManifest.xml').write_bytes(manifest)
    digest = sha(engine)
    report = dict(source_apk=str(apk), original_apk_sha256=APK_SHA, original_engine_sha256=ENGINE_SHA,
                  modified_engine_sha256=digest, manifest_sha256=sha(manifest), package=PACKAGE,
                   removed_storage_permissions=removed)
    (destination / 'patch-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (destination / 'engine_profile.h').write_text(
        '#pragma once\n#define HYBRID_MODIFIED_ENGINE_SHA256 "' + digest + '"\n'
        '#define HYBRID_MODIFIED_ENGINE_SIZE ' + str(len(engine)) + '\n', encoding='utf-8')
    print(json.dumps(report))


def package(prepared, runtime, mod, output, control=False, payload_only=False):
    prepared, output = Path(prepared), Path(output).resolve()
    require('mcpe_research' not in [part.lower() for part in output.parts], 'Research originals are read-only')
    report = json.loads((prepared / 'patch-report.json').read_text(encoding='utf-8'))
    require(file_sha(report['source_apk']) == APK_SHA, 'Original APK SHA-256 mismatch')
    require(file_sha(prepared / 'libminecraftpe.so') == report['modified_engine_sha256'], 'Prepared engine changed')
    require(file_sha(prepared / 'AndroidManifest.xml') == report['manifest_sha256'], 'Prepared manifest changed')
    require(output != Path(report['source_apk']).resolve(), 'Cannot overwrite original APK')
    replacements = {'AndroidManifest.xml': prepared / 'AndroidManifest.xml'}
    if not control and not payload_only:
        replacements.update({ENGINE_ENTRY: prepared / 'libminecraftpe.so',
                             'lib/arm64-v8a/libhybridmcpe.so': Path(runtime),
                             'lib/arm64-v8a/libhybrid_bootstrap_check.so': Path(mod)})
    elif payload_only:
        replacements.update({'lib/arm64-v8a/libhybridmcpe.so': Path(runtime),
                             'lib/arm64-v8a/libhybrid_bootstrap_check.so': Path(mod)})
    for path in replacements.values():
        require(path.is_file(), 'Missing input: ' + str(path))
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(report['source_apk']) as source, zipfile.ZipFile(output, 'w', allowZip64=True) as target:
        for entry in source.infolist():
            name = entry.filename
            if name in replacements or (name.upper().startswith('META-INF/') and
                    (name.upper().endswith(('.SF', '.RSA', '.DSA', '.EC')) or name.upper() == 'META-INF/MANIFEST.MF')):
                continue
            with source.open(entry) as src, target.open(entry, 'w') as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
        for name, path in replacements.items():
            info = zipfile.ZipInfo(name, (2009, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            with path.open('rb') as src, target.open(info, 'w') as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
    print(json.dumps({'unsigned_apk': str(output), 'sha256': file_sha(output),
                      'control': control, 'payload_only': payload_only,
                      'runtime_loaded_by_engine': not control and not payload_only}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    first = commands.add_parser('prepare')
    first.add_argument('--apk', required=True)
    first.add_argument('--out-dir', '--prepared', dest='prepared', required=True)
    second = commands.add_parser('package')
    second.add_argument('--prepared', required=True)
    second.add_argument('--runtime', required=True)
    second.add_argument('--lifecycle-mod', '--mod', dest='mod', required=True)
    second.add_argument('--apk')
    second.add_argument('--output', required=True)
    second.add_argument('--payload-only', action='store_true',
                        help='Package native payloads without modifying/loading the engine')
    control = commands.add_parser('control')
    control.add_argument('--prepared', required=True)
    control.add_argument('--apk')
    control.add_argument('--output', required=True)
    payload = commands.add_parser('payload-only')
    payload.add_argument('--prepared', required=True)
    payload.add_argument('--runtime', required=True)
    payload.add_argument('--lifecycle-mod', '--mod', dest='mod', required=True)
    payload.add_argument('--apk')
    payload.add_argument('--output', required=True)
    args = vars(parser.parse_args())
    command = args.pop('command')
    if command == 'prepare':
        args['destination'] = args.pop('prepared')
        prepare(**args)
    else:
        apk = args.pop('apk')
        if apk:
            report = json.loads((Path(args['prepared']) / 'patch-report.json').read_text(encoding='utf-8'))
            require(Path(apk).resolve() == Path(report['source_apk']).resolve(), 'APK differs from prepared source')
        if command == 'control':
            args.update(runtime=None, mod=None, control=True)
        elif command == 'payload-only':
            args['payload_only'] = True
        package(**args)


if __name__ == '__main__':
    main()
