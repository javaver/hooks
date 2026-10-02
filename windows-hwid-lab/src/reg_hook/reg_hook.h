#pragma once

// 安装/卸载对 RegOpenKeyEx / RegCreateKeyEx / RegCloseKey / RegQueryValueEx
// 的 inline hook（基于 MinHook）。安装后，命中目标键值的查询会被改写为
// fake_data.h 里生成的假数据。
bool RegHook_Install();
void RegHook_Uninstall();
