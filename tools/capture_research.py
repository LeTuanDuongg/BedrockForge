"""Record immutable source links without copying upstream code or assets."""
import json
import subprocess
from pathlib import Path
from urllib.parse import quote
ROOT=Path(__file__).resolve().parents[1]
REPOS={
 'LeviLaunchroid':('LiteLDev/LeviLaunchroid',['LICENSE','docs/guide/compatibility.md','docs/guide/developer.md','examples/full-cpp-mod/CMakeLists.txt','examples/full-cpp-mod/manifest.json','app/src/main/java/org/levimc/launcher/util/LauncherStorage.java']),
 'preloader-android':('LiteLDev/preloader-android',['CMakeLists.txt','include/pl/Mod.hpp','include/pl/ModMenu.hpp','src/pl/internal/NativeModLifecycle.cpp','src/pl/PreLoader.cpp']),
 'innercore-mod-toolchain':('zheka2304/innercore-mod-toolchain',['README.md','toolchain-setup.py']),
 'RedPowerPE':('MineExplorer/RedPowerPE',['README.md','src/dev/items/bags.ts','src/dev/api/MachineRegistry.ts','src/dev/integration/recipe_viewer.ts','make.json']),
 'JEI-Bedrock':('Satoshi442/JEI-Bedrock',['LICENSE','README.md','CMakeLists.txt','manifest.json']),
 'InnerCore-horizon-sources':('CheatBoss/InnerCore-horizon-sources',['com/zhekasmirnov/horizon/HorizonLibrary.java','com/zhekasmirnov/innercore/api/NativeAPI.java','com/zhekasmirnov/innercore/api/runtime/Callback.java','com/zhekasmirnov/innercore/api/mod/ui/container/Container.java','com/zhekasmirnov/innercore/api/runtime/saver/world/WorldDataSaver.java']),
 'netease-modsdk-wiki':('easecation/netease-modsdk-wiki',['README.md','docs/wiki/modsdk/modsdk-intro.md','docs/mcdocs/1-ModAPI/接口/通用/System.md','docs/mcdocs/1-ModAPI/接口/通用/事件.md','docs/mcdocs/1-ModAPI/接口/物品.md','docs/mcdocs/1-ModAPI/接口/方块/容器.md','docs/mcdocs/1-ModAPI/接口/世界/配方.md','docs/mcdocs/1-ModAPI/接口/世界/自定义数据.md','docs/mcdocs/1-ModAPI/接口/自定义UI/通用.md','docs/mcdocs/1-ModAPI/接口/自定义UI/UI控件.md','docs/mcguide/20-玩法开发/13-模组SDK编程/2-Python脚本开发/0-脚本开发入门.md']),
 'nuoyanlib':('charminglee/nuoyanlib',['LICENSE','README.md','src/nuoyanlib/core/server/comp.py','src/nuoyanlib/common/communicate.py','src/nuoyanlib/core/listener.py','src/nuoyanlib/client/ui/nyc/button.py']),
 'netease-bedrock-wiki':('MCNeteaseDevs/netease-bedrock-wiki',['README.md','mcguide/20-玩法开发/13-模组SDK编程/2-Python脚本开发/0-脚本开发入门.md','mcguide/27-手机网络游戏/课程9：服务器上线/第2节：PE测试.md']),
}
records={}
for directory,(repo,paths) in REPOS.items():
    checkout=ROOT/'.research'/directory
    pin=subprocess.check_output(['git','-C',str(checkout),'rev-parse','HEAD'],text=True).strip()
    date=subprocess.check_output(['git','-C',str(checkout),'log','-1','--format=%cs'],text=True).strip()
    files=[]
    for path in paths:
        if (checkout/path).is_file():
            files.append({'path':path,'url':f'https://github.com/{repo}/blob/{pin}/{quote(path)}'})
    records[directory]={'repository':repo,'commit':pin,'commit_date':date,'sources':files}
destination=ROOT/'docs/research';destination.mkdir(parents=True,exist_ok=True)
(destination/'SOURCE_PINS.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
lines=['# Nguồn đã kiểm tra','', 'Ngày kiểm tra: 2026-10-09 (Asia/Saigon). VERIFIED trong audit nghĩa là xác nhận trong mã nguồn/tài liệu; không có nghĩa đã chạy trên thiết bị.','']
for name,record in records.items():
    lines+=['## '+name,'',f"Commit: `{record['commit']}`.",'']
    for entry in record.get('sources',[]):lines.append(f"- [{entry['path']}]({entry['url']})")
    if 'url' in record:lines.append(f"- [README]({record['url']}) — {record['inspection']}")
    lines.append('')
(destination/'SOURCES.md').write_text('\n'.join(lines),encoding='utf-8')
print(f'Recorded {len(records)} repositories')
