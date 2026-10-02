// test_target.exe —— 被测程序：分别用"注册表"和"WMI"两种方式查询硬件信息。
// 直接运行能看到真实硬件信息；配合 injector 注入 hook_dll.dll 后再运行，
// 应该看到两种方式查到的都是同一套随机假数据，且每次运行都不同。

#include <windows.h>
#include <wbemidl.h>
#include <iostream>
#include <string>
#include <initializer_list>

#pragma comment(lib, "wbemuuid.lib")

namespace {

// 小帮手：构造一个临时 BSTR 并在作用域结束时自动释放，避免引入 comdef.h/_bstr_t
// 以及它需要额外链接 comsuppw.lib 的麻烦。
class TempBstr {
public:
    explicit TempBstr(const wchar_t* s) : m_b(SysAllocString(s)) {}
    ~TempBstr() { SysFreeString(m_b); }
    operator BSTR() const { return m_b; }
private:
    BSTR m_b;
};

void PrintRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* label) {
    HKEY hKey;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        std::wcout << L"[REG] " << label << L": <打开键失败>\n";
        return;
    }
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LSTATUS rc = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hKey);
    if (rc == ERROR_SUCCESS) {
        std::wcout << L"[REG] " << label << L": " << buf << L"\n";
    } else {
        std::wcout << L"[REG] " << label << L": <查询失败, rc=" << rc << L">\n";
    }
}

void PrintRegInfo() {
    std::wcout << L"---- 注册表 ----\n";
    PrintRegString(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                    L"ProcessorNameString", L"CPU Name");
    PrintRegString(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                    L"Identifier", L"CPU Identifier");
    PrintRegString(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                    L"VendorIdentifier", L"CPU Vendor");
    PrintRegString(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                    L"BaseBoardManufacturer", L"Board Manufacturer");
    PrintRegString(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                    L"BaseBoardProduct", L"Board Product");
}

bool GetBstrProp(IWbemClassObject* obj, const wchar_t* name, std::wstring* out) {
    VARIANT v;
    VariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR) {
        *out = v.bstrVal;
        ok = true;
    }
    VariantClear(&v);
    return ok;
}

void QueryAndPrint(IWbemServices* svc, const wchar_t* wql, std::initializer_list<const wchar_t*> props) {
    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = svc->ExecQuery(TempBstr(L"WQL"), TempBstr(wql),
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr)) {
        std::wcout << L"  <查询失败: 0x" << std::hex << hr << std::dec << L">\n";
        return;
    }
    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;
    while (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK && returned > 0) {
        for (auto p : props) {
            std::wstring val;
            if (GetBstrProp(obj, p, &val))
                std::wcout << L"    " << p << L" = " << val << L"\n";
        }
        obj->Release();
    }
    enumerator->Release();
}

void PrintWmiInfo() {
    std::wcout << L"\n---- WMI ----\n";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::wcout << L"CoInitializeEx 失败\n";
        return;
    }
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    IWbemLocator* locator = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                           reinterpret_cast<LPVOID*>(&locator));
    if (FAILED(hr)) {
        std::wcout << L"CoCreateInstance(WbemLocator) 失败\n";
        CoUninitialize();
        return;
    }

    IWbemServices* svc = nullptr;
    hr = locator->ConnectServer(TempBstr(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    if (FAILED(hr)) {
        std::wcout << L"ConnectServer 失败\n";
        locator->Release();
        CoUninitialize();
        return;
    }

    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    std::wcout << L"  Win32_Processor:\n";
    QueryAndPrint(svc, L"SELECT Name, ProcessorId FROM Win32_Processor", {L"Name", L"ProcessorId"});

    std::wcout << L"  Win32_VideoController:\n";
    QueryAndPrint(svc, L"SELECT Name, PNPDeviceID FROM Win32_VideoController", {L"Name", L"PNPDeviceID"});

    std::wcout << L"  Win32_DiskDrive:\n";
    QueryAndPrint(svc, L"SELECT Model, SerialNumber FROM Win32_DiskDrive", {L"Model", L"SerialNumber"});

    std::wcout << L"  Win32_BaseBoard:\n";
    QueryAndPrint(svc, L"SELECT Manufacturer, Product, SerialNumber FROM Win32_BaseBoard",
                  {L"Manufacturer", L"Product", L"SerialNumber"});

    svc->Release();
    locator->Release();
    CoUninitialize();
}

} // namespace

int wmain() {
    std::wcout << L"==== test_target: 硬件信息探测 ====\n\n";
    PrintRegInfo();
    PrintWmiInfo();
    std::wcout << L"\n按回车退出...";
    std::wcin.get();
    return 0;
}
