#pragma once
#include <string>

// 每次进程启动调用一次，生成本次运行要用的一整套假硬件信息。
// 同一个进程生命周期内多次调用 Get* 返回的是同一套值（保证注册表/WMI两条
// 路径互相一致），不同进程（下次运行）会生成不同的值。
void FakeData_Init();

std::wstring GetFakeCpuName();        // 例如 "Intel(R) Core(TM) i7-xxxxK CPU"
std::wstring GetFakeCpuVendorId();    // 例如 "GenuineIntel"
std::wstring GetFakeCpuIdentifier();  // 注册表 Identifier 字段
std::wstring GetFakeCpuProcessorId(); // WMI Win32_Processor.ProcessorId

std::wstring GetFakeGpuName();        // WMI Win32_VideoController.Name
std::wstring GetFakeGpuPnpDeviceId(); // WMI Win32_VideoController.PNPDeviceID

std::wstring GetFakeDiskSerial();     // WMI Win32_DiskDrive.SerialNumber
std::wstring GetFakeDiskModel();      // WMI Win32_DiskDrive.Model

std::wstring GetFakeBoardManufacturer(); // Win32_BaseBoard.Manufacturer
std::wstring GetFakeBoardProduct();      // Win32_BaseBoard.Product
std::wstring GetFakeBoardSerial();       // Win32_BaseBoard.SerialNumber
