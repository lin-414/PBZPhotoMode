// 工具：输出 default_config_text()（带注释的默认配置文本）。
// 由 tools/gen_config.bat 编译调用（源文件由 build\ 迁入 tools\，新克隆可直接用）；
// 默认写入 config.ini.example，也可传参数指定别的路径。
#include <cstdio>
#include <string>

#include "core/config.h"

int main() {
    const std::string text = pbzpm::default_config_text();
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}
