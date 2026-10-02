#pragma once

// 安装/卸载对 CoCreateInstance(CLSID_WbemLocator, ...) 的 inline hook，
// 返回的 IWbemLocator / IWbemServices / IEnumWbemClassObject 都会被包一层代理，
// 在 IEnumWbemClassObject::Next 拿到结果对象后用 IWbemClassObject::Put()
// 原地改写 Win32_Processor / Win32_VideoController / Win32_DiskDrive /
// Win32_BaseBoard 里几个关心的属性。
//
// 局限性（教学示例，未覆盖）：
//   - IWbemServices::ExecQueryAsync / CreateInstanceEnumAsync 不会被改写
//   - QueryInterface 到 IWbemLocator / IWbemServices / IEnumWbemClassObject
//     以外的接口时不会继续保持代理（直接报 E_NOINTERFACE）
bool WmiHook_Install();
void WmiHook_Uninstall();
