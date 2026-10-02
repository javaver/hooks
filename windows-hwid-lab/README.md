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
| `src/probe_dll` | **探测版** Hook DLL：不篡改任何返回值，只把注册表/WMI/DeviceIoControl/卷序列号/SMBIOS 这几条候选路径的调用记录到日志，用于"不知道目标程序走哪条路径"时先摸清情况 |
| `src/injector` | 一个最小化的注入器：启动目标程序并把指定的 DLL（`hook_dll.dll` 或 `probe_dll.dll`）注入进去 |

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

## 不知道目标程序用哪种方式读硬件信息时怎么办

不建议一上来就把所有可能的接口全 Hook 一遍（维护成本高、也无法确认"是不是真的全覆盖了"）。
更可靠的做法是先侦察、再动手：

1. **静态/工具辅助观察**：用 Sysinternals 的 Process Monitor 过滤目标进程，看有没有命中
   `HARDWARE\DESCRIPTION\System\...` 之类的注册表路径；或者用 `strings`/IDA/Ghidra
   看二进制里有没有 `"Win32_Processor"`、`"SELECT ... FROM"` 这类明文字符串，
   很多时候比动态调试更快就能猜到个大概。
2. **用本项目新增的 `probe_dll` 做动态确认**：它只记录、不篡改任何返回值，把下面这些
   候选路径都挂了日志：
   - 注册表 `RegQueryValueExW/A`（用 `NtQueryKey` 反查完整路径，不需要额外维护 HKEY 表）
   - WMI `CoCreateInstance(WbemLocator) -> ExecQuery / CreateInstanceEnum / GetObject`
     （记录完整的 WQL 查询语句或类名）
   - 磁盘底层 `DeviceIoControl`（标出 `IOCTL_STORAGE_QUERY_PROPERTY`、
     `SMART_RCV_DRIVE_DATA` 等几个常见的硬件信息相关 IOCTL 码）
   - 卷序列号 `GetVolumeInformationW`（提醒：这是卷序列号，不是磁盘物理序列号，是两回事）
   - 原始 SMBIOS 表 `GetSystemFirmwareTable(Provider='RSMB', ...)`（这条路径完全绕开
     注册表和 WMI，直接读固件表，是最容易被漏掉的一条）

   用法：
   ```powershell
   build\Release\injector.exe <目标程序.exe> build\Release\probe_dll.dll
   # 跑一遍目标程序里会触发硬件信息查询的功能
   type %TEMP%\hwid_lab_probe.log
   ```
   日志里出现了哪些 `[REG]` / `[WMI]` / `[IOCTL]` / `[VOLINFO]` / `[SMBIOS]` 行，
   就说明目标程序实际在用哪条路径——然后再针对性地在 `hook_dll` 里补齐对应的伪造逻辑，
   而不是猜。

3. **如果 probe_dll 什么都没记录到**：说明目标程序很可能是直接执行了 `CPUID` 指令、
   或者走的是用户态/内核态 API 之外的路径（比如通过自己的内核驱动读硬件），
   这种情况已经超出用户态 Hook 能覆盖的范围，参考文末"扩展练习"里关于虚拟机方案的说明。

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
