// 共享测试夹具：让 tests/main.cpp 与按子系统拆分的测试 TU（test_mod_runtime / test_input_pure /
// test_pbzfm_validation）共用同一套 CHECK 宏与全局计数，避免复制一份断言实现。
// 计数用 C++17 inline 变量：所有 TU 看到的是同一个实例。
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace pbzpm_test {

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_section = "";

inline void report_fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("  [FAIL] %s:%d  %s\n", file, line, what.c_str());
}

inline void check(bool cond, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (!cond) report_fail(file, line, what);
}

inline void check_eq_int(long long got, long long want, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (got != want) {
        std::printf("  [FAIL] %s:%d  %s（期望 %lld，实际 %lld）\n", file, line, what.c_str(), want, got);
        ++g_failures;
    }
}

inline void check_near(double got, double want, double eps, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (std::fabs(got - want) > eps) {
        std::printf("  [FAIL] %s:%d  %s（期望 %.6f，实际 %.6f）\n", file, line, what.c_str(), want, got);
        ++g_failures;
    }
}

inline void check_str(const std::string& got, const std::string& want, const char* file, int line,
                      const std::string& what) {
    ++g_checks;
    if (got != want) {
        std::printf("  [FAIL] %s:%d  %s（期望 \"%s\"，实际 \"%s\"）\n", file, line, what.c_str(), want.c_str(),
                    got.c_str());
        ++g_failures;
    }
}

inline void section(const char* name) {
    g_section = name;
    std::printf("\n== %s ==\n", name);
    std::fflush(stdout);
}

}  // namespace pbzpm_test

#define CHECK(cond) ::pbzpm_test::check((cond), __FILE__, __LINE__, #cond)
#define CHECK_INT(got, want) ::pbzpm_test::check_eq_int((got), (want), __FILE__, __LINE__, #got)
#define CHECK_NEAR(got, want, eps) ::pbzpm_test::check_near((got), (want), (eps), __FILE__, __LINE__, #got)
#define CHECK_STR(got, want) ::pbzpm_test::check_str((got), (want), __FILE__, __LINE__, #got)
