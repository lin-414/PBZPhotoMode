#pragma once

// 版本号集中在这里，UI 标题与日志都引用它。
//
// 三个宏的分工（见 docs/README.md「版本号」说明）：
//   PBZPM_VERSION      基线版本，1.0.x 是唯一在对外发布的线（1.1.0–1.8.1 图形菜单线已判死作废）
//   PBZPM_HOTFIX       同一基线上的热修序号，0 = 无热修（版本串与发行包名都不带后缀）
//   PBZPM_VERSION_FULL 完整版本串 —— **日志与 UI 必须用它**
//
// 为什么需要 HOTFIX：历史问题是同一条基线上做了 29 轮热修、产出了 52 个发行包，
// 但它们全部打印同一个 "1.0.75-cpp"，用户发来的日志无法定位到底跑的哪个构建，
// 只好在文档里另手写一个 "-hotfixNN" 版本号，于是出现两套并行版本串并且互相脱节。
// 现在把热修序号收进编译期常量：发一个热修包就 +1，日志/UI/生成物自动带上。
#define PBZPM_VERSION "2.0.1-cpp"
#define PBZPM_HOTFIX 0

#define PBZPM_STRINGIZE_IMPL(x) #x
#define PBZPM_STRINGIZE(x) PBZPM_STRINGIZE_IMPL(x)

#if PBZPM_HOTFIX > 0
#define PBZPM_VERSION_FULL PBZPM_VERSION "-hotfix" PBZPM_STRINGIZE(PBZPM_HOTFIX)
#else
#define PBZPM_VERSION_FULL PBZPM_VERSION
#endif

#define PBZPM_DISPLAY_NAME "PBZ Photo Mode (C++)"
