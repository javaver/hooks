#include "fake_data.h"
#include <windows.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <cstdio>
#include <cwchar>

namespace {

std::mt19937_64& Rng() {
    // 种子取：高精度计数器 + 进程 ID + 启动时间，保证"每次运行"都不同，
    // 但同一次运行内是确定的（多次取值一致）。
    static std::mt19937_64 rng = [] {
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        std::seed_seq seed{
            static_cast<unsigned>(qpc.QuadPart & 0xffffffff),
            static_cast<unsigned>(qpc.QuadPart >> 32),
            static_cast<unsigned>(GetCurrentProcessId()),
            static_cast<unsigned>(GetTickCount64() & 0xffffffff)
        };
        return std::mt19937_64(seed);
    }();
    return rng;
}

std::wstring RandomHex(int nibbles) {
    static const wchar_t* kHex = L"0123456789ABCDEF";
    std::wstring s;
    s.reserve(nibbles);
    std::uniform_int_distribution<int> dist(0, 15);
    for (int i = 0; i < nibbles; ++i) s.push_back(kHex[dist(Rng())]);
    return s;
}

std::wstring RandomSerial(int len) {
    static const wchar_t* kAlnum = L"ABCDEFGHJKLMNPQRSTUVWXYZ0123456789";
    std::wstring s;
    s.reserve(len);
    std::uniform_int_distribution<int> dist(0, 34);
    for (int i = 0; i < len; ++i) s.push_back(kAlnum[dist(Rng())]);
    return s;
}

int RandomInRange(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(Rng());
}

struct FakeProfile {
    std::wstring cpuName, cpuVendorId, cpuIdentifier, cpuProcessorId;
    std::wstring gpuName, gpuPnpId;
    std::wstring diskSerial, diskModel;
    std::wstring boardManufacturer, boardProduct, boardSerial;
};

FakeProfile& Profile() {
    static FakeProfile p;
    return p;
}

std::once_flag g_once;

} // namespace

void FakeData_Init() {
    std::call_once(g_once, [] {
        auto& p = Profile();

        static const wchar_t* kCpuModels[] = {
            L"Intel(R) Core(TM) i7-%d00K CPU @ %d.%dGHz",
            L"Intel(R) Core(TM) i9-%d00KF CPU @ %d.%dGHz",
            L"AMD Ryzen 7 %d00X 8-Core Processor",
            L"AMD Ryzen 9 %d00X 12-Core Processor",
        };
        wchar_t buf[256];
        int idx = RandomInRange(0, 3);
        int gen = RandomInRange(9, 14);
        int ghzWhole = RandomInRange(2, 5);
        int ghzFrac = RandomInRange(0, 9);
        swprintf_s(buf, kCpuModels[idx], gen, ghzWhole, ghzFrac);
        p.cpuName = buf;
        p.cpuVendorId = (idx < 2) ? L"GenuineIntel" : L"AuthenticAMD";
        p.cpuIdentifier = (idx < 2)
            ? L"Intel64 Family 6 Model 158 Stepping 10"
            : L"AMD64 Family 25 Model 33 Stepping 2";
        p.cpuProcessorId = RandomHex(16);

        static const wchar_t* kGpuModels[] = {
            L"NVIDIA GeForce RTX 40%d0", L"NVIDIA GeForce RTX 30%d0 Ti",
            L"AMD Radeon RX 7%d00 XT", L"Intel(R) Arc(TM) A%d0 Graphics",
        };
        swprintf_s(buf, kGpuModels[RandomInRange(0, 3)], RandomInRange(5, 9));
        p.gpuName = buf;
        swprintf_s(buf, L"PCI\\VEN_%s&DEV_%s&SUBSYS_%s&REV_A1",
                   RandomHex(4).c_str(), RandomHex(4).c_str(), RandomHex(8).c_str());
        p.gpuPnpId = buf;

        p.diskSerial = RandomSerial(20);
        static const wchar_t* kDiskModels[] = {
            L"Samsung SSD 980 PRO %dTB", L"WD Black SN850X %dTB",
            L"Crucial MX500 %dGB", L"Seagate BarraCuda %dTB",
        };
        swprintf_s(buf, kDiskModels[RandomInRange(0, 3)], RandomInRange(1, 4));
        p.diskModel = buf;

        static const wchar_t* kBoardMakers[] = {L"ASUSTeK COMPUTER INC.", L"Micro-Star International Co., Ltd.",
                                                 L"Gigabyte Technology Co., Ltd.", L"ASRock"};
        int mIdx = RandomInRange(0, 3);
        p.boardManufacturer = kBoardMakers[mIdx];
        swprintf_s(buf, L"B%d%d-PLUS GAMING", RandomInRange(5, 7), RandomInRange(0, 9));
        p.boardProduct = buf;
        p.boardSerial = RandomSerial(16);
    });
}

std::wstring GetFakeCpuName() { return Profile().cpuName; }
std::wstring GetFakeCpuVendorId() { return Profile().cpuVendorId; }
std::wstring GetFakeCpuIdentifier() { return Profile().cpuIdentifier; }
std::wstring GetFakeCpuProcessorId() { return Profile().cpuProcessorId; }

std::wstring GetFakeGpuName() { return Profile().gpuName; }
std::wstring GetFakeGpuPnpDeviceId() { return Profile().gpuPnpId; }

std::wstring GetFakeDiskSerial() { return Profile().diskSerial; }
std::wstring GetFakeDiskModel() { return Profile().diskModel; }

std::wstring GetFakeBoardManufacturer() { return Profile().boardManufacturer; }
std::wstring GetFakeBoardProduct() { return Profile().boardProduct; }
std::wstring GetFakeBoardSerial() { return Profile().boardSerial; }
