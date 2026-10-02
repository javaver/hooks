#include "log_util.h"
#include <windows.h>
#include <fstream>
#include <mutex>
#include <cstdio>

namespace {

std::mutex g_logMutex;
std::wstring g_logPath;

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

} // namespace

void Log_Init() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_logPath.empty()) return;
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    g_logPath = std::wstring(tempPath) + L"hwid_lab_probe.log";
}

void Log_Line(const std::wstring& line) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logPath.empty()) return;
    // MSVC 的 <fstream> 支持 wchar_t* 路径的扩展构造函数（非标准，但在 Windows/MSVC 上通用）。
    std::ofstream f(g_logPath.c_str(), std::ios::app);
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[32];
    sprintf_s(ts, "[PID %lu][%02d:%02d:%02d.%03d] ", GetCurrentProcessId(), st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds);
    f << ts << ToUtf8(line) << "\n";
}
