# windows-hwid-lab —— Windows 硬件信息 Hook 学习实验

## 这是什么

一个用来**学习 Windows Hook 技术**的自包含实验项目，演示应用程序获取
CPU / GPU / 磁盘序列号 / 主板信息时会经过的两条最常见路径：

1. **注册表路径**：`HKLM\HARDWARE\DESCRIPTION\System\...`
2. **WMI 路径**：`Win32_Processor` / `Win32_VideoController` /
   `Win32_DiskDrive` / `Win32_BaseBoard`

项目包含三个部分：

| 目录 | 作用 |
|---|---|
| `src/test_target` | 一个"被测程序"：用注册表 + WMI 两种方式查询硬件信息并打印出来 |
| `src/hook_dll` | Hook 逻辑所在的 DLL：拦截 `RegQueryValueEx*`、`CoCreateInstance`（进而包装 WMI 的 `IWbemServices`/`IEnumWbemClassObject`），把查到的值替换成"每个进程启动时随机生成一次"的假数据 |
| `src/injector` | 一个最小化的注入器：启动 `test_target.exe` 并把 `hook_dll.dll` 注入进去 |

## 适用范围 / 使用边界

- 本项目只针对**你自己编写、自己启动的 `test_target.exe`**，用来理解
  "Hook 要覆盖哪些层级才能保证信息一致"这个问题。
- 没有包含、也不建议用来对抗任何具体的反作弊 / DRM / 风控系统 —— 那些系统通常
  运行在内核态并专门检测此类篡改，绕过它们通常违反对应软件的服务条款，
  本项目不是为此设计的。
- `*Async` 系列 WMI 接口、`DeviceIoControl` 磁盘底层拦截、`CPUID` 级别的拦截
  （需要 Hypervisor）未包含，留作练习，文末有说明思路。

## 原理简述

- `RegQueryValueExW/A` 本身只知道一个 `HKEY` 句柄，不知道完整路径，
  所以同时 Hook 了 `RegOpenKeyExW/A`、`RegCreateKeyExW/A`、`RegCloseKey`，
  维护一张 `HKEY -> 完整路径` 的表，这样才能判断"当前查询的是不是我们要伪造的键值"。
- WMI 的调用链是 `CoCreateInstance(CLSID_WbemLocator) -> IWbemLocator::ConnectServer
  -> IWbemServices::ExecQuery -> IEnumWbemClassObject::Next -> IWbemClassObject::Get`。
  Hook 的做法不是去逐个拦 `Get`（那样要实现一遍完整的 `IWbemClassObject` 接口），
  而是在 `IEnumWbemClassObject::Next` 拿到对象后，直接对返回的
  `IWbemClassObject` 调用 `Put()` 把目标属性原地改写，这样只需要包一层
  `IWbemLocator` / `IWbemServices` / `IEnumWbemClassObject`（这三个接口方法数量少，
  好实现），不用重写 `IWbemClassObject`。
- 两条路径各自独立生效，于是一个程序不管是读注册表还是查 WMI，看到的都是
  同一套"本次进程生成的假数据"——这正是前面讨论的"只 Hook 顶层 API 容易
  信息不一致"问题的一个小规模解法。

## 构建（需要 Windows + Visual Studio 2019/2022 + CMake 3.20+）

```powershell
git clone <this repo>
cd windows-hwid-lab
cmake -B build -A x64
cmake --build build --config Release
```

CMake 会通过 `FetchContent` 自动拉取 [MinHook](https://github.com/TsudaKageyu/minhook)
（BSD-2-Clause 协议）用于实现 x86/x64 的 inline hook，不需要你手动下载。

## 运行 / 验证

```powershell
# 1. 先直接跑一遍，看到的是你机器的真实硬件信息
build\Release\test_target.exe

# 2. 用注入器启动并注入 hook dll，再跑一遍
build\Release\injector.exe build\Release\test_target.exe build\Release\hook_dll.dll

# 3. 多运行几次第 2 步，观察每次打印出来的 CPU 名称 / ProcessorId /
#    显卡名称 / 磁盘序列号 / 主板型号+序列号 是否每次都不同，
#    并且同一次运行里"注册表读到的"和"WMI 查到的"是否互相一致
```

## 扩展练习（思路，未实现）

- `DeviceIoControl` 拦截 `IOCTL_STORAGE_QUERY_PROPERTY` / `SMART_RCV_DRIVE_DATA`，
  伪造 `GetVolumeInformation` 之外、程序直接读盘控制器返回的序列号。
- `IWbemServices::ExecQueryAsync` / `CreateInstanceEnum`：对异步查询，
  需要再包一层 `IWbemObjectSink`，在 `Indicate()` 里对 `apObjArray` 做同样的 `Put()`。
- 真正的 `CPUID` 指令只能在 Hypervisor 层拦截（VT-x VM-Exit），
  用户态/内核态 Hook 都够不到，这也是为什么很多"硬件信息随机化"工具
  最终选择整个跑在虚拟机里、改虚拟机的 SMBIOS 配置，而不是在真机上做 Hook。
