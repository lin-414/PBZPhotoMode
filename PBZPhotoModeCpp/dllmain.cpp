// UE4SS DLL 入口。UE4SS 用 start_mod / uninstall_mod 两个导出函数加载 C++ mod。
#include "mod_app.h"

extern "C" {

__declspec(dllexport) RC::CppUserModBase* start_mod() {
    return new pbzpm::PhotoModeMod();
}

__declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) {
    delete mod;
}

}  // extern "C"
