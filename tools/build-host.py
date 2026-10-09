"""Build only BedrockForge-owned Java/native code; never bundle Minecraft."""
from pathlib import Path
import subprocess,zipfile,os

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'.tools/sdk'; JDK=ROOT/'.tools/jdk/bin'; BT=SDK/'build-tools/35.0.0'
OUT=ROOT/'build-host'; OUT.mkdir(exist_ok=True)
def run(*args): subprocess.run([str(a) for a in args],check=True,cwd=ROOT)
classes=OUT/'classes';classes.mkdir(exist_ok=True)
android=SDK/'platforms/android-35/android.jar'
run(JDK/'javac.exe','--release','11','-classpath',android,'-d',classes,*sorted((ROOT/'android-host/app/src').rglob('*.java')))
env=os.environ.copy();env['JAVA_HOME']=str(JDK.parent);env['PATH']=str(JDK)+os.pathsep+env['PATH']
subprocess.run([str(BT/'d8.bat'),'--min-api','30','--lib',str(android),'--output',str(OUT),*map(str,classes.rglob('*.class'))],env=env,check=True)
unsigned=OUT/'unsigned.apk'
run(BT/'aapt2.exe','link','-o',unsigned,'--manifest',ROOT/'android-host/app/AndroidManifest.xml','-I',android)
with zipfile.ZipFile(unsigned,'a',compression=zipfile.ZIP_DEFLATED) as apk:
 apk.write(OUT/'classes.dex','classes.dex')
 for name in ('bedrockforge','bedrockforge_host'):
  apk.write(ROOT/'build-android'/f'lib{name}.so',f'lib/arm64-v8a/lib{name}.so')
 apk.write(SDK/'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so','lib/arm64-v8a/libc++_shared.so')
aligned=OUT/'aligned.apk';run(BT/'zipalign.exe','-f','-p','4',unsigned,aligned)
key=ROOT/'.tools/host-development.jks'
if not key.exists():
 run(JDK/'keytool.exe','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','development','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=BedrockForge Development')
result=OUT/'bedrockforge-development.apk'
run(JDK/'java.exe','-jar',BT/'lib/apksigner.jar','sign','--ks',key,'--ks-pass','pass:android','--out',result,aligned)
run(JDK/'java.exe','-jar',BT/'lib/apksigner.jar','verify',result)
print(result)
