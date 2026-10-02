#include "wmi_hook.h"
#include "../fake_data/fake_data.h"

#include <windows.h>
#include <wbemidl.h>
#include <MinHook.h>
#include <string>
#include <atomic>

#pragma comment(lib, "wbemuuid.lib")

namespace {

// ---------------- 工具函数 ----------------

bool ClassNameIs(IWbemClassObject* obj, const wchar_t* name) {
    VARIANT v;
    VariantInit(&v);
    bool match = false;
    if (SUCCEEDED(obj->Get(L"__CLASS", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR) {
        match = (_wcsicmp(v.bstrVal, name) == 0);
    }
    VariantClear(&v);
    return match;
}

void PutStringProp(IWbemClassObject* obj, const wchar_t* prop, const std::wstring& value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value.c_str());
    obj->Put(prop, 0, &v, 0);
    VariantClear(&v);
}

void MutateObjectIfNeeded(IWbemClassObject* obj) {
    if (!obj) return;
    if (ClassNameIs(obj, L"Win32_Processor")) {
        PutStringProp(obj, L"Name", GetFakeCpuName());
        PutStringProp(obj, L"ProcessorId", GetFakeCpuProcessorId());
    } else if (ClassNameIs(obj, L"Win32_VideoController")) {
        PutStringProp(obj, L"Name", GetFakeGpuName());
        PutStringProp(obj, L"PNPDeviceID", GetFakeGpuPnpDeviceId());
    } else if (ClassNameIs(obj, L"Win32_DiskDrive")) {
        PutStringProp(obj, L"SerialNumber", GetFakeDiskSerial());
        PutStringProp(obj, L"Model", GetFakeDiskModel());
    } else if (ClassNameIs(obj, L"Win32_BaseBoard")) {
        PutStringProp(obj, L"Manufacturer", GetFakeBoardManufacturer());
        PutStringProp(obj, L"Product", GetFakeBoardProduct());
        PutStringProp(obj, L"SerialNumber", GetFakeBoardSerial());
    }
}

// ---------------- IEnumWbemClassObject 代理 ----------------

class CEnumWbemClassObject : public IEnumWbemClassObject {
public:
    explicit CEnumWbemClassObject(IEnumWbemClassObject* inner) : m_inner(inner) {
        m_inner->AddRef();
    }
    ~CEnumWbemClassObject() { m_inner->Release(); }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumWbemClassObject) {
            *ppv = static_cast<IEnumWbemClassObject*>(this);
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

    // IEnumWbemClassObject
    HRESULT STDMETHODCALLTYPE Reset() override { return m_inner->Reset(); }

    HRESULT STDMETHODCALLTYPE Next(long lTimeout, ULONG uCount, IWbemClassObject** apObjects,
                                    ULONG* puReturned) override {
        HRESULT hr = m_inner->Next(lTimeout, uCount, apObjects, puReturned);
        if (SUCCEEDED(hr) && apObjects && puReturned) {
            for (ULONG i = 0; i < *puReturned; ++i) {
                MutateObjectIfNeeded(apObjects[i]);
            }
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE NextAsync(ULONG uCount, IWbemObjectSink* pSink) override {
        // 教学示例未包装异步回调路径；直接透传。
        return m_inner->NextAsync(uCount, pSink);
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumWbemClassObject** ppEnum) override {
        IEnumWbemClassObject* cloned = nullptr;
        HRESULT hr = m_inner->Clone(&cloned);
        if (SUCCEEDED(hr) && ppEnum) {
            *ppEnum = new CEnumWbemClassObject(cloned);
            cloned->Release();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Skip(long lTimeout, ULONG nCount) override {
        return m_inner->Skip(lTimeout, nCount);
    }

private:
    std::atomic<ULONG> m_ref{1};
    IEnumWbemClassObject* m_inner;
};

// ---------------- IWbemServices 代理 ----------------

class CWbemServices : public IWbemServices {
public:
    explicit CWbemServices(IWbemServices* inner) : m_inner(inner) { m_inner->AddRef(); }
    ~CWbemServices() { m_inner->Release(); }

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

    // ---- 需要改写的两个同步"拿对象列表"的入口 ----
    HRESULT STDMETHODCALLTYPE ExecQuery(const BSTR strQueryLanguage, const BSTR strQuery, long lFlags,
                                         IWbemContext* pCtx, IEnumWbemClassObject** ppEnum) override {
        IEnumWbemClassObject* raw = nullptr;
        HRESULT hr = m_inner->ExecQuery(strQueryLanguage, strQuery, lFlags, pCtx, &raw);
        if (SUCCEEDED(hr) && ppEnum) {
            *ppEnum = new CEnumWbemClassObject(raw);
            raw->Release();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE CreateInstanceEnum(const BSTR strClass, long lFlags, IWbemContext* pCtx,
                                                  IEnumWbemClassObject** ppEnum) override {
        IEnumWbemClassObject* raw = nullptr;
        HRESULT hr = m_inner->CreateInstanceEnum(strClass, lFlags, pCtx, &raw);
        if (SUCCEEDED(hr) && ppEnum) {
            *ppEnum = new CEnumWbemClassObject(raw);
            raw->Release();
        }
        return hr;
    }

    // ---- 其余方法直接透传（未在本示例中改写，可按同样模式扩展） ----
    HRESULT STDMETHODCALLTYPE CreateInstanceEnumAsync(const BSTR strClass, long lFlags, IWbemContext* pCtx,
                                                       IWbemObjectSink* pSink) override {
        return m_inner->CreateInstanceEnumAsync(strClass, lFlags, pCtx, pSink);
    }
    HRESULT STDMETHODCALLTYPE OpenNamespace(const BSTR a, long b, IWbemContext* c, IWbemServices** d,
                                             IWbemCallResult** e) override {
        return m_inner->OpenNamespace(a, b, c, d, e);
    }
    HRESULT STDMETHODCALLTYPE CancelAsyncCall(IWbemObjectSink* s) override { return m_inner->CancelAsyncCall(s); }
    HRESULT STDMETHODCALLTYPE QueryObjectSink(long f, IWbemObjectSink** s) override {
        return m_inner->QueryObjectSink(f, s);
    }
    HRESULT STDMETHODCALLTYPE GetObject(const BSTR p, long f, IWbemContext* c, IWbemClassObject** o,
                                         IWbemCallResult** r) override {
        return m_inner->GetObject(p, f, c, o, r);
    }
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
    std::atomic<ULONG> m_ref{1};
    IWbemServices* m_inner;
};

// ---------------- IWbemLocator 代理 ----------------

class CWbemLocator : public IWbemLocator {
public:
    explicit CWbemLocator(IWbemLocator* inner) : m_inner(inner) { m_inner->AddRef(); }
    ~CWbemLocator() { m_inner->Release(); }

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
            *ppNamespace = new CWbemServices(raw);
            raw->Release();
        }
        return hr;
    }

private:
    std::atomic<ULONG> m_ref{1};
    IWbemLocator* m_inner;
};

// ---------------- CoCreateInstance hook ----------------

decltype(&CoCreateInstance) Real_CoCreateInstance = nullptr;

HRESULT WINAPI Hook_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid,
                                      LPVOID* ppv) {
    HRESULT hr = Real_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv && rclsid == CLSID_WbemLocator && riid == IID_IWbemLocator) {
        IWbemLocator* real = reinterpret_cast<IWbemLocator*>(*ppv);
        *ppv = new CWbemLocator(real);
        real->Release();
    }
    return hr;
}

} // namespace

bool WmiHook_Install() {
    if (MH_CreateHook(reinterpret_cast<void*>(&CoCreateInstance), reinterpret_cast<void*>(&Hook_CoCreateInstance),
                       reinterpret_cast<void**>(&Real_CoCreateInstance)) != MH_OK)
        return false;
    return MH_EnableHook(reinterpret_cast<void*>(&CoCreateInstance)) == MH_OK;
}

void WmiHook_Uninstall() {
    MH_DisableHook(reinterpret_cast<void*>(&CoCreateInstance));
}
