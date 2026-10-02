#pragma once
#include <string>

// 极简的追加写日志工具，线程安全。日志文件固定写在系统临时目录下：
// %TEMP%\hwid_lab_probe.log
void Log_Init();
void Log_Line(const std::wstring& line);
