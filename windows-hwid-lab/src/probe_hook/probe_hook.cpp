#include "probe_hook.h"
#include "log_util.h"

#include <windows.h>
#include <wbemidl.h>
#include <MinHook.h>
#include <string>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "wbemuuid.lib")

namespace {

// ---------------- 用 NtQueryKey 反查 HKEY 对应的完整路径 ----------------
// 比"额外 hook RegOpenKeyEx 维护一张表"更简单：无论这个 HKEY 是怎么打开的
// （哪怕是程序启动前就存在的句柄、或者走的是我们没拦截到的函数），都能查到路径。
using NtQueryKeyProc = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

std::wstring GetKeyFullPath(HKEY hKey) {
    static NtQueryKeyProc NtQueryKey = reinterpret_cast<NtQueryKeyProc>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryKey"));
    if (!NtQueryKey) return L"";

    BYTE buffer[1024];
    ULONG resultLen = 0;
    const int KeyNameInformation = 3;
    LONG status = NtQueryKey(hKey, KeyNameInformation, buffer, sizeof(buffer), &resultLen);
    if (status != 0 /* STATUS_SUCCESS */) return L"";

    ULONG nameLenBytes = *reinterpret_cast<ULONG*>(buffer);
    const wchar_t* namePtr = reinterpret_cast<const wchar_t*>(buffer + sizeof(ULONG));
    return std::wstring(namePtr, nameLenBytes / sizeof(wchar_t));
}

// ---------------- 注册表探测 ----------------

decltype(&RegQueryValueExW) Real_RegQueryValueExW = nullptr;
decltype(&RegQueryValueExA) Real_RegQueryValueExA = nullptr;

LSTATUS WINAPI Hook_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
                                      LPBYTE lpData, LPDWORD lpcbData) {
    LSTATUS rc = Real_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    std::wstring path = GetKeyFullPath(hKey);
    std::wstringstream ss;
    ss << L"[REG] " << path << L" \\ " << (lpValueName ? lpValueName : L"(默认值)") << L"  rc=" << rc;
    Log_Line(ss.str());
    return rc;
}

LSTATUS WINAPI Hook_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
                                      LPBYTE lpData, LPDWORD lpcbData) {
    LSTATUS rc = Real_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    std::wstring path = GetKeyFullPath(hKey);
    std::wstring wValueName = L"(默认值)";
    if (lpValueName) {
        int len = MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, nullptr, 0);
        wValueName.assign(len > 0 ? len - 1 : 0, L'\0');
        if (len > 0) MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, wValueName.data(), len);
    }
    std::wstringstream ss;
    ss << L"[REG-A] " << path << L" \\ " << wValueName << L"  rc=" << rc;
    Log_Line(ss.str());
    return rc;
}


// ---------------- WMI 探测 ----------------

class CWbemServicesProbe : public IWbemServices {
public:
    explicit CWbemServicesProbe(IWbemServices* inner) : m_inner(inner) { m_inner->AddRef(); }
    ~CWbemServicesProbe() { m_inner->Release(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IWbemServices) {
            *ppv = static_cast<IWbemServices*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE ExecQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags,
                                         IWbemContext* pCtx, IEnumWbemClassObject** ppEnum) override {
        std::wstringstream ss;
        ss << L"[WMI] ExecQuery(" << (strQueryLanguage ? strQueryLanguage : L"") << L"): "
           << (strQuery ? strQuery : L"");
        Log_Line(ss.str());
        return m_inner->ExecQuery(strQueryLanguage, strQuery, lFlags, pCtx, ppEnum); // 只记录，不包装结果
    }

    HRESULT STDMETHODCALLTYPE CreateInstanceEnum(const BSTR strClass, long lFlags, IWbemContext* pCtx,
                                                  IEnumWbemClassObject** ppEnum) override {
        std::wstringstream ss;
        ss << L"[WMI] CreateInstanceEnum: " << (strClass ? strClass : L"");
        Log_Line(ss.str());
        return m_inner->CreateInstanceEnum(strClass, lFlags, pCtx, ppEnum);
    }

    HRESULT STDMETHODCALLTYPE GetObject(const BSTR p, long f, IWbemContext* c, IWbemClassObject** o,
                                         IWbemCallResult** r) override {
        std::wstringstream ss;
        ss << L"[WMI] GetObject: " << (p ? p : L"");
        Log_Line(ss.str());
        return m_inner->GetObject(p, f, c, o, r);
    }

    // ---- 其余方法直接透传，不记录（按需可以仿照上面加日志） ----
    HRESULT STDMETHODCALLTYPE CreateInstanceEnumAsync(const BSTR a, long b, IWbemContext* c, IWbemObjectSink* d) override {
        return m_inner->CreateInstanceEnumAsync(a, b, c, d);
    }
    HRESULT STDMETHODCALLTYPE OpenNamespace(const BSTR a, long b, IWbemContext* c, IWbemServices** d,
                                             IWbemCallResult** e) override { return m_inner->OpenNamespace(a, b, c, d, e); }
    HRESULT STDMETHODCALLTYPE CancelAsyncCall(IWbemObjectSink* s) override { return m_inner->CancelAsyncCall(s); }
    HRESULT STDMETHODCALLTYPE QueryObjectSink(long f, IWbemObjectSink** s) override { return m_inner->QueryObjectSink(f, s); }
    HRESULT STDMETHODCALLTYPE GetObjectAsync(const BSTR p, long f, IWbemContext* c, IWbemObjectSink* s) override {
        return m_inner->GetObjectAsync(p, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE PutClass(IWbemClassObject* o, long f, IWbemContext* c, IWbemCallResult** r) override {
        return m_inner->PutClass(o, f, c, r);
    }
    HRESULT STDMETHODCALLTYPE PutClassAsync(IWbemClassObject* o, long f, IWbemContext* c, IWbemObjectSink* s) override {
        return m_inner->PutClassAsync(o, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE DeleteClass(const BSTR p, long f, IWbemContext* c, IWbemCallResult** r) override {
        return m_inner->DeleteClass(p, f, c, r);
    }
    HRESULT STDMETHODCALLTYPE DeleteClassAsync(const BSTR p, long f, IWbemContext* c, IWbemObjectSink* s) override {
        return m_inner->DeleteClassAsync(p, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE CreateClassEnum(const BSTR s, long f, IWbemContext* c, IEnumWbemClassObject** e) override {
        return m_inner->CreateClassEnum(s, f, c, e);
    }
    HRESULT STDMETHODCALLTYPE CreateClassEnumAsync(const BSTR s, long f, IWbemContext* c, IWbemObjectSink* k) override {
        return m_inner->CreateClassEnumAsync(s, f, c, k);
    }
    HRESULT STDMETHODCALLTYPE PutInstance(IWbemClassObject* o, long f, IWbemContext* c, IWbemCallResult** r) override {
        return m_inner->PutInstance(o, f, c, r);
    }
    HRESULT STDMETHODCALLTYPE PutInstanceAsync(IWbemClassObject* o, long f, IWbemContext* c, IWbemObjectSink* s) override {
        return m_inner->PutInstanceAsync(o, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE DeleteInstance(const BSTR p, long f, IWbemContext* c, IWbemCallResult** r) override {
        return m_inner->DeleteInstance(p, f, c, r);
    }
    HRESULT STDMETHODCALLTYPE DeleteInstanceAsync(const BSTR p, long f, IWbemContext* c, IWbemObjectSink* s) override {
        return m_inner->DeleteInstanceAsync(p, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE ExecQueryAsync(const BSTR l, const BSTR q, long f, IWbemContext* c,
                                              IWbemObjectSink* s) override {
        std::wstringstream ss;
        ss << L"[WMI] ExecQueryAsync: " << (q ? q : L"");
        Log_Line(ss.str());
        return m_inner->ExecQueryAsync(l, q, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE ExecNotificationQuery(const BSTR l, const BSTR q, long f, IWbemContext* c,
                                                     IEnumWbemClassObject** e) override {
        return m_inner->ExecNotificationQuery(l, q, f, c, e);
    }
    HRESULT STDMETHODCALLTYPE ExecNotificationQueryAsync(const BSTR l, const BSTR q, long f, IWbemContext* c,
                                                          IWbemObjectSink* s) override {
        return m_inner->ExecNotificationQueryAsync(l, q, f, c, s);
    }
    HRESULT STDMETHODCALLTYPE ExecMethod(const BSTR o, const BSTR m, long f, IWbemContext* c, IWbemClassObject* in,
                                          IWbemClassObject** out, IWbemCallResult** r) override {
        return m_inner->ExecMethod(o, m, f, c, in, out, r);
    }
    HRESULT STDMETHODCALLTYPE ExecMethodAsync(const BSTR o, const BSTR m, long f, IWbemContext* c,
                                               IWbemClassObject* in, IWbemObjectSink* s) override {
        return m_inner->ExecMethodAsync(o, m, f, c, in, s);
    }

private:
    ULONG m_ref{1};
    IWbemServices* m_inner;
};

class CWbemLocatorProbe : public IWbemLocator {
public:
    explicit CWbemLocatorProbe(IWbemLocator* inner) : m_inner(inner) { m_inner->AddRef(); }
    ~CWbemLocatorProbe() { m_inner->Release(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IWbemLocator) {
            *ppv = static_cast<IWbemLocator*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE ConnectServer(const BSTR strNetworkResource, const BSTR strUser, const BSTR strPassword,
                                             const BSTR strLocale, long lSecurityFlags, const BSTR strAuthority,
                                             IWbemContext* pCtx, IWbemServices** ppNamespace) override {
        IWbemServices* raw = nullptr;
        HRESULT hr = m_inner->ConnectServer(strNetworkResource, strUser, strPassword, strLocale, lSecurityFlags,
                                             strAuthority, pCtx, &raw);
        if (SUCCEEDED(hr) && ppNamespace) {
            std::wstringstream ss;
            ss << L"[WMI] ConnectServer: " << (strNetworkResource ? strNetworkResource : L"");
            Log_Line(ss.str());
            *ppNamespace = new CWbemServicesProbe(raw);
            raw->Release();
        }
        return hr;
    }

private:
    ULONG m_ref{1};
    IWbemLocator* m_inner;
};

decltype(&CoCreateInstance) Real_CoCreateInstance = nullptr;

HRESULT WINAPI Hook_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid,
                                      LPVOID* ppv) {
    HRESULT hr = Real_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv && rclsid == CLSID_WbemLocator && riid == IID_IWbemLocator) {
        Log_Line(L"[WMI] CoCreateInstance(CLSID_WbemLocator) 被调用");
        IWbemLocator* real = reinterpret_cast<IWbemLocator*>(*ppv);
        *ppv = new CWbemLocatorProbe(real);
        real->Release();
    }
    return hr;
}

// ---------------- DeviceIoControl 探测 ----------------

const wchar_t* DescribeIoctl(DWORD code) {
    switch (code) {
        case 0x002D1400: return L"IOCTL_STORAGE_QUERY_PROPERTY (常用于读磁盘序列号/型号)";
        case 0x0007C088: return L"SMART_RCV_DRIVE_DATA (SMART 数据，含序列号)";
        case 0x00070000: return L"IOCTL_DISK_GET_DRIVE_GEOMETRY";
        case 0x002D1080: return L"IOCTL_STORAGE_GET_DEVICE_NUMBER";
        case 0x0004D008: return L"IOCTL_SCSI_MINIPORT (可能携带 ATA PassThrough 取序列号)";
        case 0x0004D014: return L"IOCTL_ATA_PASS_THROUGH";
        default: return nullptr;
    }
}

decltype(&DeviceIoControl) Real_DeviceIoControl = nullptr;

BOOL WINAPI Hook_DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                  LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned,
                                  LPOVERLAPPED lpOverlapped) {
    BOOL ok = Real_DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize,
                                    lpBytesReturned, lpOverlapped);
    const wchar_t* desc = DescribeIoctl(dwIoControlCode);
    if (desc) {
        std::wstringstream ss;
        ss << L"[IOCTL] code=0x" << std::hex << dwIoControlCode << L" " << desc;
        Log_Line(ss.str());
    }
    return ok;
}

// ---------------- GetVolumeInformationW 探测 ----------------

decltype(&GetVolumeInformationW) Real_GetVolumeInformationW = nullptr;

BOOL WINAPI Hook_GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
                                        LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength,
                                        LPDWORD lpFileSystemFlags, LPWSTR lpFileSystemName, DWORD nFileSystemNameSize) {
    BOOL ok = Real_GetVolumeInformationW(lpRootPathName, lpVolumeNameBuffer, nVolumeNameSize, lpVolumeSerialNumber,
                                          lpMaximumComponentLength, lpFileSystemFlags, lpFileSystemName,
                                          nFileSystemNameSize);
    std::wstringstream ss;
    ss << L"[VOLINFO] GetVolumeInformationW(root=" << (lpRootPathName ? lpRootPathName : L"") << L") "
       << L"—— 注意这是卷序列号，不是磁盘物理序列号";
    Log_Line(ss.str());
    return ok;
}

// ---------------- GetSystemFirmwareTable 探测（原始 SMBIOS）----------------

decltype(&GetSystemFirmwareTable) Real_GetSystemFirmwareTable = nullptr;

UINT WINAPI Hook_GetSystemFirmwareTable(DWORD FirmwareTableProviderSignature, DWORD FirmwareTableID,
                                         PVOID pFirmwareTableBuffer, DWORD BufferSize) {
    UINT ret = Real_GetSystemFirmwareTable(FirmwareTableProviderSignature, FirmwareTableID, pFirmwareTableBuffer,
                                            BufferSize);
    if (FirmwareTableProviderSignature == 'RSMB') {
        Log_Line(L"[SMBIOS] GetSystemFirmwareTable(RSMB) 被调用 —— 直接读原始 SMBIOS 表，"
                 L"绕过注册表和 WMI，这种情况需要额外的拦截手段（见 README）");
    }
    return ret;
}

template <typename T>
bool CreateAndEnable(T target, T detour, T* original) {
    if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                       reinterpret_cast<void**>(original)) != MH_OK)
        return false;
    return MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK;
}

} // namespace

bool ProbeHook_Install() {
    Log_Init();
    Log_Line(L"==== probe_dll 已注入，开始记录 ====");

    bool ok = true;
    ok &= CreateAndEnable(&RegQueryValueExW, &Hook_RegQueryValueExW, &Real_RegQueryValueExW);
    ok &= CreateAndEnable(&RegQueryValueExA, &Hook_RegQueryValueExA, &Real_RegQueryValueExA);
    ok &= CreateAndEnable(&CoCreateInstance, &Hook_CoCreateInstance, &Real_CoCreateInstance);
    ok &= CreateAndEnable(&DeviceIoControl, &Hook_DeviceIoControl, &Real_DeviceIoControl);
    ok &= CreateAndEnable(&GetVolumeInformationW, &Hook_GetVolumeInformationW, &Real_GetVolumeInformationW);
    ok &= CreateAndEnable(&GetSystemFirmwareTable, &Hook_GetSystemFirmwareTable, &Real_GetSystemFirmwareTable);
    return ok;
}

void ProbeHook_Uninstall() {
    MH_DisableHook(reinterpret_cast<void*>(&RegQueryValueExW));
    MH_DisableHook(reinterpret_cast<void*>(&RegQueryValueExA));
    MH_DisableHook(reinterpret_cast<void*>(&CoCreateInstance));
    MH_DisableHook(reinterpret_cast<void*>(&DeviceIoControl));
    MH_DisableHook(reinterpret_cast<void*>(&GetVolumeInformationW));
    MH_DisableHook(reinterpret_cast<void*>(&GetSystemFirmwareTable));
}
