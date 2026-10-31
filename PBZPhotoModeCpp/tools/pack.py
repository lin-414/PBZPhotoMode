# 打 zip（两个独立模组）：
#   dist/PBZPhotoModeCpp  → dist/PBZPhotoModeCpp_<版本>.zip   （UE4SS DLL 主模组）
#   dist/PBZPhotoModeUI   → dist/PBZPhotoModeUI_<版本>.zip    （图形菜单资产 pak 模组）
# 用法：python tools/pack.py <版本>   （如 1.5.0-cpp；UI 包版本同号发布）
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
pack('dist/PBZPhotoModeUI', 'dist/PBZPhotoModeUI_%s.zip' % version, 'PBZPhotoModeUI')
