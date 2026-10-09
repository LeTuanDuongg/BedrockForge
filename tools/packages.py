"""Strict framework manifest validation and deterministic mod packaging.

No archive is executed or installed by this tool. Native code is trusted code.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

def semver(value):
    if not isinstance(value,str) or not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)",value):
        raise ValueError("v1 supports numeric release semver only")
    return tuple(map(int,value.split('.')))
def satisfies(version, constraint):
    if constraint.startswith('>='): return semver(version)>=semver(constraint[2:])
    if constraint.startswith('=='): return semver(version)==semver(constraint[2:])
    return semver(version)==semver(constraint)
def manifest(data):
    required={'id','display_name','author','version','framework_api','minecraft_versions','dependencies','optional_dependencies','capabilities','native'}
    if set(data)!=required: raise ValueError("manifest fields differ from schema")
    if not re.fullmatch(r'[a-z][a-z0-9_]{1,63}',data['id']): raise ValueError("mod ID")
    for key in ('display_name','author'):
        if not isinstance(data[key],str) or not 0<len(data[key])<=128: raise ValueError(key)
    semver(data['version']);semver(data['framework_api'])
    if data['framework_api']!='1.0.0': raise ValueError("unsupported framework API")
    if not isinstance(data['minecraft_versions'],list) or any(not re.fullmatch(r'\d+\.\d+\.\d+(\.\d+)?',v) for v in data['minecraft_versions']): raise ValueError("Minecraft versions must be exact")
    for key in ('dependencies','optional_dependencies'):
        if not isinstance(data[key],dict): raise ValueError(key)
        for name,version in data[key].items():
            if not re.fullmatch(r'[a-z][a-z0-9_]{1,63}',name): raise ValueError("dependency ID")
            satisfies('0.0.0',version)
    if set(data['dependencies']) & set(data['optional_dependencies']): raise ValueError("dependency specified twice")
    if not isinstance(data['capabilities'],list) or any(not isinstance(c,str) or not re.fullmatch(r'[a-z0-9_.]+',c) for c in data['capabilities']): raise ValueError("capabilities")
    native=data['native']
    if set(native)!={'library','entry_point','abi','mod_api'} or native['entry_point']!='bf_mod_entry' or native['abi']!='arm64-v8a' or native['mod_api']!=2: raise ValueError("native entry")
    if not re.fullmatch(r'lib[a-z0-9_]+\.so',native['library']): raise ValueError("library path")
    return data

def resolve(mods, minecraft_version, capabilities, safe=False):
    if safe: return []
    by_id={}
    for data in mods:
        m=manifest(data)
        if m['id'] in by_id: raise ValueError("duplicate mod")
        if minecraft_version not in m['minecraft_versions']: raise ValueError("unverified Minecraft version")
        if not set(m['capabilities'])<=set(capabilities): raise ValueError("missing capability")
        by_id[m['id']]=m
    edges={}
    for name,m in by_id.items():
        deps=dict(m['dependencies'])
        deps.update({d:v for d,v in m['optional_dependencies'].items() if d in by_id})
        for d,v in deps.items():
            if d not in by_id or not satisfies(by_id[d]['version'],v): raise ValueError("missing or incompatible dependency")
        edges[name]=set(deps)
    result=[]
    while edges:
        ready=sorted(n for n,e in edges.items() if not e)
        if not ready: raise ValueError("dependency cycle")
        for n in ready: result.append(by_id[n]); del edges[n]
        for e in edges.values(): e.difference_update(ready)
    return result

def elf_arm64(path):
    data=Path(path).read_bytes()
    if len(data)<64 or data[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(data[18:20],'little')!=183 or int.from_bytes(data[16:18],'little')!=3:
        raise ValueError("expected little-endian ARM64 ELF shared library")
    return data
def archive(output, files):
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            if '/' in name or '\\' in name or name in ('.','..'): raise ValueError("unsafe archive path")
            info=zipfile.ZipInfo(name,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,data)

def main():
    p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('library',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args();m=manifest(json.loads(args.manifest.read_text(encoding='utf-8')))
    library=elf_arm64(args.library)
    files={'bedrockforge.mod.json':json.dumps(m,sort_keys=True,indent=2).encode(),m['native']['library']:library}
    files['SHA256SUMS.json']=json.dumps({n:hashlib.sha256(d).hexdigest() for n,d in files.items()},sort_keys=True).encode()
    archive(args.output,files)
if __name__=='__main__': main()
