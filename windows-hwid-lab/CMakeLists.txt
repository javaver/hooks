cmake_minimum_required(VERSION 3.20)
project(windows_hwid_lab LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "该项目只能在 Windows 上构建（使用了 Win32 API / COM / 注册表）")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_subdirectory(src/hook_dll)
add_subdirectory(src/test_target)
add_subdirectory(src/injector)
