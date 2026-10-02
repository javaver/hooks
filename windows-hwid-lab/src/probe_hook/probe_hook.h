#pragma once

// 探测版 Hook：只记录，不篡改任何返回值。
// 用来回答"目标程序到底是通过哪种方式读取硬件信息的"这个问题——
// 把它注入到目标进程跑一遍，然后看 %TEMP%\hwid_lab_probe.log。
//
// 覆盖的候选路径：
//   - 注册表：RegQueryValueExW/A（用 NtQueryKey 反查 HKEY 对应的完整路径，
//     不需要像 reg_hook 那样额外 hook RegOpenKeyEx 维护路径表）
//   - WMI：CoCreateInstance(CLSID_WbemLocator) -> ExecQuery / CreateInstanceEnum
//     的查询语句 / 类名
//   - 磁盘底层：DeviceIoControl 的 IOCTL 码（重点标出几个常见的硬件信息相关码）
//   - 卷序列号：GetVolumeInformationW（注意这和"磁盘物理序列号"是两回事）
//   - 原始 SMBIOS 表：GetSystemFirmwareTable(Provider='RSMB', ...) ——
//     这条路径绕过注册表和 WMI，直接读原始固件表，很多"硬件指纹"库会用它
bool ProbeHook_Install();
void ProbeHook_Uninstall();
