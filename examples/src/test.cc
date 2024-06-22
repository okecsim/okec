// #include <iostream>
// #include <cstdint>

// namespace okec {
//     enum class color : uint32_t {
//         FG_BLACK         = 30,
//         FG_RED           = 31,
//         FG_GREEN         = 32,
//         FG_YELLOW        = 33,
//         FG_BLUE          = 34,
//         FG_MAGENTA       = 35,
//         FG_CYAN          = 36,
//         FG_LIGHT_GRAY    = 37,
//         FG_DEFAULT       = 39,
//         BG_RED           = 41,
//         BG_GREEN         = 42,
//         BG_BLUE          = 44,
//         BG_DEFAULT       = 49,
//         FG_DARK_GRAY     = 90,
//         FG_LIGHT_RED     = 91,
//         FG_LIGHT_GREEN   = 92,
//         FG_LIGHT_YELLOW  = 93,
//         FG_LIGHT_BLUE    = 94,
//         FG_LIGHT_MAGENTA = 95,
//         FG_LIGHT_CYAN    = 96,
//         FG_WHITE         = 97
//     };

//     std::ostream& operator<<(std::ostream& os, color code) {
//         return os << "\033[" << static_cast<int>(code) << "m";
//     }
// }


// int main() {
//     std::cout << "This ->" << okec::color::FG_RED << "word"
//          << okec::color::FG_DEFAULT << "<- is red." << std::endl;
// }


#include <iostream>
#include <string>
#include <format>
#include <tuple>
#include <cstdint>
#include <fmt/color.h>

enum class color : uint32_t {
    debug = 0xCC8BF5,            // rgb(204,139,245)
    info = 0x77F9F6,             // rgb(119,249,246)
    warning = 0xFCF669,          // rgb(252,246,105)
    success = 0x84FD61,          // rgb(132,253,97)
    error = 0xE96A63,            // rgb(233,106,99)
};

std::tuple<int, int, int> color_to_rgb(color c) {
    auto value = static_cast<uint32_t>(c);
    int r = (value >> 16) & 0xFF;
    int g = (value >> 8) & 0xFF;
    int b = value & 0xFF;
    return std::make_tuple(r, g, b);
}

// 函数来生成指定 RGB 颜色的 ANSI 转义码
std::string rgb_color(int r, int g, int b) {
    return std::format("\033[38;2;{};{};{}m", r, g, b);
}

// 函数来生成重置颜色的 ANSI 转义码
constexpr const char* reset_color = "\033[0m";

int main() {
    double time_seconds = 123.456; // 这里是你要输出的时间值
    std::string formatted_time = std::format("{:.2f}", time_seconds);

    auto [r, g, b] = color_to_rgb(color::debug);

    // 输出带有 RGB 指定颜色的文本
    std::cout << rgb_color(r, g, b) << formatted_time << reset_color << std::endl;
    return 0;
}
