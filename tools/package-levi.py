"""Package only the BedrockForge framework bootstrap for a loading smoke test.
Format is based on audited Levi examples/full-cpp-mod manifest and pack script.
"""
import argparse
import json
from pathlib import Path
from packages import archive, elf_arm64
p=argparse.ArgumentParser()
p.add_argument('build',type=Path);p.add_argument('output',type=Path)
p.add_argument('--smoke-test-only',action='store_true',required=True)
p.add_argument('--libcxx',type=Path,required=True)
a=p.parse_args()
files={}
for name in ('bedrockforge_levi','bedrockforge'):
    filename='lib'+name+'.so';files[filename]=elf_arm64(a.build/filename)
files['libc++_shared.so']=elf_arm64(a.libcxx)
files['manifest.json']=json.dumps({'type':'preload-native','name':'BedrockForge Loading Prototype','author':'BedrockForge contributors','version':'0.1.0','entry':'libbedrockforge_levi.so','minecraft_versions':[]},indent=2).encode()
files['LIMITATIONS.txt']=b'Framework bootstrap loading smoke test only. No plugin discovery or verified gameplay integration. Use an isolated offline profile. Native code is not sandboxed.'
archive(a.output,files)
