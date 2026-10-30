# 打包 DLL 版（需要先编译出 PBZPhotoModeCpp.dll）
# 用法： pwsh -File tools/pack.ps1 -Dll build/Release/PBZPhotoModeCpp.dll
param(
    [string]$Dll = "build/Release/PBZPhotoModeCpp.dll",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not (Test-Path $Dll)) {
    Write-Host "[错误] 找不到 DLL：$Dll" -ForegroundColor Red
    Write-Host "请先按 docs/BUILD.md 编译，或用 -Dll 指定路径。" -ForegroundColor Yellow
    exit 1
}

$name = "PBZPhotoModeCpp_v$Version"
$stage = Join-Path $root "dist/$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }

# UE4SS 的 C++ mod 目录结构：Mods\<ModName>\dlls\<ModName>.dll
$dllDir = Join-Path $stage "ue4ss/Mods/PBZPhotoModeCpp/dlls"
New-Item -ItemType Directory -Force -Path $dllDir | Out-Null

Copy-Item $Dll (Join-Path $dllDir "PBZPhotoModeCpp.dll")
Copy-Item (Join-Path $root "config.ini.example") (Join-Path $stage "ue4ss/Mods/PBZPhotoModeCpp/config.ini")

Set-Content -Path (Join-Path $stage "ue4ss/Mods/PBZPhotoModeCpp/enabled.txt") -Value "PBZPhotoModeCpp : 1" -Encoding UTF8
Copy-Item (Join-Path $root "docs/README.md") (Join-Path $stage "安装说明.md")

$zip = Join-Path $root "dist/$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

Write-Host "[完成] $zip" -ForegroundColor Green
