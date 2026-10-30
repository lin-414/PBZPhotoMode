# 打 zip：把 dist/PBZPhotoModeCpp 整目录压成 dist/PBZPhotoModeCpp_<版本>.zip
import os
import shutil
import sys
import zipfile

root = 'dist/PBZPhotoModeCpp'
version = sys.argv[1] if len(sys.argv) > 1 else 'dev'
zip_path = 'dist/PBZPhotoModeCpp_%s.zip' % version

os.makedirs(os.path.join(root, 'dlls'), exist_ok=True)
shutil.copyfile('build-dll/PBZPhotoModeCpp.dll', os.path.join(root, 'dlls', 'PBZPhotoModeCpp.dll'))

if os.path.exists(zip_path):
    os.remove(zip_path)

with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
    for base, _dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(base, f)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            z.write(p, 'PBZPhotoModeCpp/' + rel)

print(zip_path, os.path.getsize(zip_path))
with zipfile.ZipFile(zip_path) as z:
    for n in z.namelist():
        print('  ', n)
