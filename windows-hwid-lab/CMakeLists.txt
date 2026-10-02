cmake_minimum_required(VERSION 3.20)
project(windows_hwid_lab LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "该项目只能在 Windows 上构建（使用了 Win32 API / COM / 注册表）")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    # 源码里的中文注释是 UTF-8（无 BOM）编码。cl.exe 在非英文系统区域设置下
    # （比如简体中文代码页 936）默认会用 ANSI 代码页去解析源文件，
    # 遇到 UTF-8 的多字节字符会解析错乱，引发一连串看起来不相关的语法错误
    # （缺少分号、花括号不匹配等）。加 /utf-8 让 cl.exe 始终按 UTF-8 解析源码
    # 和执行字符集，和系统区域设置无关。
    add_compile_options(/utf-8)
endif()

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_subdirectory(src/hook_dll)
add_subdirectory(src/probe_dll)
add_subdirectory(src/test_target)
add_subdirectory(src/injector)
