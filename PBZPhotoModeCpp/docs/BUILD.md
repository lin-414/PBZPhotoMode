# 构建 DLL（已验证可行）

> 前提：能访问 Epic 授权的 `UEPseudo` 私有仓库。没有它，RE-UE4SS 无法编译。

**已实测通过（2026-10-30）**：MSVC 19.44.35229.0 + Ninja + `Game__Shipping__Win64`，
产物 `build-dll/PBZPhotoModeCpp.dll`（535 KB），导入表 71 个符号全部能在你游戏里那份
`ue4ss/UE4SS.dll` 的导出表里找到 —— 这点很关键，说明 ABI 对得上。

## 1. 准备 UE4SS 源码与依赖

本仓库不包含 UE4SS 源码；请自行克隆 RE-UE4SS，并检出与游戏内 `UE4SS.dll` 相同的提交。

```bash
cd D:/path/to/RE-UE4SS

# 必须和游戏里那份 UE4SS.dll 同一个 commit，否则符号顺序/结构体可能对不上
git checkout v3.0.1-1183-gcdbc0b66      # = aedfc5bc

# 子模块是 SSH 地址，没配 SSH key 时用 insteadOf 临时改 HTTPS（不要改 .gitmodules）
git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive
```

`deps/first/Unreal`（=UEPseudo，含 `include/` 与 `generated_include/`）与
`deps/first/patternsleuth` 都会检出。Rust 需要 `%USERPROFILE%\.cargo\bin` 在 PATH 里。

## 2. 配置与编译

```powershell
cd <本仓库>\\PBZPhotoModeCpp

# 核心层单测（不需要 UE4SS，几秒钟）
tools\\build_tests.bat

# 带 UE4SS 构建 DLL（脚本内部 call vcvars64 + Ninja）
tools\\build_dll.bat
```

脚本等价于：

```bash
cmake -S . -B build-dll -G Ninja \
      -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 \
      -DPBZPM_WITH_UE4SS=ON \
      -DPBZPM_UE4SS_DIR=D:/path/to/RE-UE4SS
cmake --build build-dll
```

**UE4SS 用的是自定义构建类型 `Game__Shipping__Win64`，不是 `Release`**；
Ninja 是单配置生成器，`-DCMAKE_BUILD_TYPE` 就是最终配置。

产物：`build-dll/PBZPhotoModeCpp.dll`。

## 3. 部署

UE4SS 的 C++ mod 目录结构（注意 `dlls` 子目录，且**不需要写 mods.txt**）：

```
<影之刃零>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\
│  └─ PBZPhotoModeCpp.dll
├─ config.ini
└─ enabled.txt
```

打包：把上面的目录结构打成 zip（`dist/PBZPhotoModeCpp_v1.0.0-cpp.zip`）。

## 4. 编译期踩过的坑（再遇到别重复排查）

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `'4SS' 不是内部或外部命令` 刷屏几千行 | `.bat` 用 LF 换行，cmd 解析多行 `if (...)` 抽风 | 用 `b'\r\n'` 规范化成 CRLF |
| `error C1083: 无法打开 <CppUserModBase.hpp>` | `target_link_libraries(... PRIVATE UE4SS)` 不传播 include | 改成 `PUBLIC UE4SS` |
| `error C2872: "UObject": 不明确的符号` | 我们自己的头文件写了 `namespace RC { class UObject; }` | 前向声明必须写 `RC::Unreal::UObject` |
| `error C4003: 类函数宏的调用"max"参数不足` | `Windows.h` 的 `max` 宏污染 UE 模板 | UE 头文件在前，`NOMINMAX` 后置包含 `Windows.h` |
| `error C2065: PBZPM_VERSION 未声明`（在 `STR()` 里） | `STR(x)` 展开成 `L##x`，宏参数不展开 | 加两级宏 `PBZPM_WIDEN` |
| `error C2039: IsValid 不是 UObjectGlobals 的成员` | UEPseudo 没有这个函数 | 用 `obj->GetClassPrivate() != nullptr` 判活 |

## 5. 运行时自检

```bash
# 依赖是不是 UE4SS.dll
dumpbin /dependents build-dll/PBZPhotoModeCpp.dll

# 我们导入了哪些 UE4SS 符号，是否都在你游戏里那份 UE4SS.dll 的导出表里
dumpbin /imports build-dll/PBZPhotoModeCpp.dll
dumpbin /exports D:/Steam/steamapps/common/PBZ/platform/steam/ue4ss/UE4SS.dll
```
