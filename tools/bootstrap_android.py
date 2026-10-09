"""Download pinned official Android build tools into the workspace.
No system install, game binary, license bypass or emulator is included.
"""
import concurrent.futures
import hashlib
import json
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path, PurePosixPath
ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT/'.tools'

def download(url, target, expected_sha1=None):
    partial=target.with_suffix(target.suffix+'.partial')
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():
        print('Downloading '+target.name,flush=True)
        with urllib.request.urlopen(url,timeout=90) as stream, partial.open('wb') as output:
            total=0;next_report=32*1024*1024
            while True:
                chunk=stream.read(1024*1024)
                if not chunk:break
                output.write(chunk);total+=len(chunk)
                if total>=next_report:
                    print(f'{target.name}: {total//(1024*1024)} MiB',flush=True);next_report+=32*1024*1024
        partial.replace(target)
    sha1=hashlib.sha1();sha256=hashlib.sha256()
    with target.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):sha1.update(block);sha256.update(block)
    if expected_sha1 and sha1.hexdigest()!=expected_sha1:raise ValueError('Official archive checksum mismatch: '+target.name)
    return {'url':url,'sha1':sha1.hexdigest(),'sha256':sha256.hexdigest(),'bytes':target.stat().st_size}

def extract(archive, destination):
    if (destination/'.bf-extracted').exists():return
    destination.mkdir(parents=True,exist_ok=True)
    base=destination.resolve()
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            parts=PurePosixPath(member.filename).parts
            if len(parts)<2:continue
            relative=Path(*parts[1:]);target=(base/relative).resolve()
            if not target.is_relative_to(base) or ((member.external_attr>>16)&0o170000)==0o120000:raise ValueError('Unsafe archive member')
            if member.is_dir():target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                with z.open(member) as source,target.open('wb') as out:
                    while chunk:=source.read(1024*1024):out.write(chunk)
    (destination/'.bf-extracted').write_text('Official pinned archive extracted\n')
    print('Extracted '+str(destination.relative_to(ROOT)),flush=True)

def main():
    xml=urllib.request.urlopen('https://dl.google.com/android/repository/repository2-1.xml',timeout=90).read()
    repo=ET.fromstring(xml);jobs=[]
    packages={'ndk;27.2.12479018':TOOLS/'sdk/ndk/27.2.12479018','build-tools;35.0.0':TOOLS/'sdk/build-tools/35.0.0','platforms;android-35':TOOLS/'sdk/platforms/android-35'}
    for path,destination in packages.items():
        package=next((p for p in repo.iter() if p.tag.endswith('remotePackage') and p.attrib.get('path')==path),None)
        if package is None:raise ValueError('Pinned package unavailable: '+path)
        for archive in package.findall('./archives/archive'):
            host=archive.findtext('host-os')
            if host not in (None,'windows'):continue
            url=archive.findtext('./complete/url');checksum=archive.findtext('./complete/checksum')
            if url:
                jobs.append((path,'https://dl.google.com/android/repository/'+url,TOOLS/'downloads'/url,destination,checksum));break
        else:raise ValueError('Windows archive unavailable: '+path)
    jobs.append(('jdk;21.0.12.1','https://aka.ms/download-jdk/microsoft-jdk-21.0.12.1-windows-x64.zip',TOOLS/'downloads/jdk-21.0.12.1.zip',TOOLS/'jdk',None))
    def install(job):
        name,url,archive,destination,checksum=job
        record=download(url,archive,checksum);extract(archive,destination);return name,record
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        records=dict(pool.map(install,jobs))
    (TOOLS/'android-downloads.json').write_text(json.dumps(records,indent=2)+'\n')
    print('Pinned Android toolchain ready',flush=True)
if __name__=='__main__':main()
