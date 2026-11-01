# 打 zip（仅主模组；图形菜单资产模组 PBZPhotoModeUI 已随图形菜单路线判死移除，不再打包）：
#   dist/PBZPhotoModeCpp  → dist/PBZPhotoModeCpp_<版本>.zip   （UE4SS DLL 主模组）
# 用法：python tools/pack.py <版本>   （如 1.0.20-cpp）
import os
import shutil
import sys
import zipfile

version = sys.argv[1] if len(sys.argv) > 1 else 'dev'


def pack(root, zip_path, prefix):
    if not os.path.isdir(root):
        print('SKIP（目录不存在）:', root)
        return
    if 'dlls' in os.listdir(root):
        os.makedirs(os.path.join(root, 'dlls'), exist_ok=True)
        shutil.copyfile('build-dll/PBZPhotoModeCpp.dll', os.path.join(root, 'dlls', 'PBZPhotoModeCpp.dll'))
    # 语言词典随包分发：仓库 languages/ 同步进发行目录（DLL 从 <mod 目录>/languages/*.json 加载）
    if os.path.isdir('languages'):
        lang_dst = os.path.join(root, 'languages')
        os.makedirs(lang_dst, exist_ok=True)
        for f in sorted(os.listdir('languages')):
            if f.endswith('.json'):
                shutil.copyfile(os.path.join('languages', f), os.path.join(lang_dst, f))
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, _dirs, files in os.walk(root):
            for f in files:
                p = os.path.join(base, f)
                rel = os.path.relpath(p, root).replace(os.sep, '/')
                z.write(p, prefix + '/' + rel)
    print(zip_path, os.path.getsize(zip_path))
    with zipfile.ZipFile(zip_path) as z:
        for n in z.namelist():
            print('  ', n)


pack('dist/PBZPhotoModeCpp', 'dist/PBZPhotoModeCpp_%s.zip' % version, 'PBZPhotoModeCpp')
