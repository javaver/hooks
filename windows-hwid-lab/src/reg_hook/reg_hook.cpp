#include "reg_hook.h"
#include "../fake_data/fake_data.h"

#include <windows.h>
#include <MinHook.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cwctype>

namespace {

// ---------- HKEY -> 完整路径 表 ----------
std::mutex g_mapMutex;
std::unordered_map<HKEY, std::wstring> g_keyPaths;

std::wstring ToUpper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towupper(c); });
    return s;
}

std::wstring RootName(HKEY h) {
    if (h == HKEY_LOCAL_MACHINE) return L"HKEY_LOCAL_MACHINE";
    if (h == HKEY_CURRENT_USER) return L"HKEY_CURRENT_USER";
    if (h == HKEY_CLASSES_ROOT) return L"HKEY_CLASSES_ROOT";
    if (h == HKEY_USERS) return L"HKEY_USERS";
    if (h == HKEY_CURRENT_CONFIG) return L"HKEY_CURRENT_CONFIG";
    return L"";
}

std::wstring ParentPathOf(HKEY hKey) {
    std::wstring root = RootName(hKey);
    if (!root.empty()) return root;
    std::lock_guard<std::mutex> lk(g_mapMutex);
    auto it = g_keyPaths.find(hKey);
    if (it != g_keyPaths.end()) return it->second;
    return L""; // 未知句柄（可能是打开前就存在、我们没拦到的键）
}

void RememberKeyPath(HKEY parent, const std::wstring& subKey, HKEY result) {
    std::wstring parentPath = ParentPathOf(parent);
    std::wstring full = subKey.empty()
        ? parentPath
        : (parentPath.empty() ? subKey : parentPath + L"\\" + subKey);
    std::lock_guard<std::mutex> lk(g_mapMutex);
    g_keyPaths[result] = full;
}

void ForgetKeyPath(HKEY h) {
    std::lock_guard<std::mutex> lk(g_mapMutex);
    g_keyPaths.erase(h);
}

// ---------- 目标键值表 ----------
using Supplier = std::wstring (*)();

struct Target {
    const wchar_t* pathSuffix; // 大写，匹配"路径以它结尾"
    const wchar_t* valueName;  // 大写，精确匹配
    Supplier supplier;
};

const Target kTargets[] = {
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\CENTRALPROCESSOR\\0", L"PROCESSORNAMESTRING", GetFakeCpuName},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\CENTRALPROCESSOR\\0", L"IDENTIFIER", GetFakeCpuIdentifier},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\CENTRALPROCESSOR\\0", L"VENDORIDENTIFIER", GetFakeCpuVendorId},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\BIOS", L"BASEBOARDMANUFACTURER", GetFakeBoardManufacturer},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\BIOS", L"BASEBOARDPRODUCT", GetFakeBoardProduct},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\BIOS", L"SYSTEMMANUFACTURER", GetFakeBoardManufacturer},
    {L"HARDWARE\\DESCRIPTION\\SYSTEM\\BIOS", L"SYSTEMPRODUCTNAME", GetFakeBoardProduct},
};

const Supplier* FindFakeSupplier(HKEY hKey, const std::wstring& valueName) {
    std::wstring path = ToUpper(ParentPathOf(hKey));
    std::wstring value = ToUpper(valueName);
    if (path.empty()) return nullptr;
    for (const auto& t : kTargets) {
        std::wstring suffix = t.pathSuffix;
        if (value == t.valueName &&
            path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return &t.supplier;
        }
    }
    return nullptr;
}

// ---------- 原函数指针 ----------
decltype(&RegOpenKeyExW) Real_RegOpenKeyExW = nullptr;
decltype(&RegOpenKeyExA) Real_RegOpenKeyExA = nullptr;
decltype(&RegCreateKeyExW) Real_RegCreateKeyExW = nullptr;
decltype(&RegCreateKeyExA) Real_RegCreateKeyExA = nullptr;
decltype(&RegCloseKey) Real_RegCloseKey = nullptr;
decltype(&RegQueryValueExW) Real_RegQueryValueExW = nullptr;
decltype(&RegQueryValueExA) Real_RegQueryValueExA = nullptr;

// ---------- Detour 实现 ----------
LSTATUS WINAPI Hook_RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    LSTATUS rc = Real_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    if (rc == ERROR_SUCCESS && phkResult)
        RememberKeyPath(hKey, lpSubKey ? lpSubKey : L"", *phkResult);
    return rc;
}

LSTATUS WINAPI Hook_RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    LSTATUS rc = Real_RegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    if (rc == ERROR_SUCCESS && phkResult) {
        std::wstring sub;
        if (lpSubKey) {
            int len = MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, nullptr, 0);
            sub.resize(len > 0 ? len - 1 : 0);
            if (len > 0) MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, sub.data(), len);
        }
        RememberKeyPath(hKey, sub, *phkResult);
    }
    return rc;
}

LSTATUS WINAPI Hook_RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD r, LPWSTR lpClass, DWORD opt,
                                     REGSAM sam, const LPSECURITY_ATTRIBUTES sa, PHKEY phkResult, LPDWORD disp) {
    LSTATUS rc = Real_RegCreateKeyExW(hKey, lpSubKey, r, lpClass, opt, sam, sa, phkResult, disp);
    if (rc == ERROR_SUCCESS && phkResult)
        RememberKeyPath(hKey, lpSubKey ? lpSubKey : L"", *phkResult);
    return rc;
}

LSTATUS WINAPI Hook_RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD r, LPSTR lpClass, DWORD opt,
                                     REGSAM sam, const LPSECURITY_ATTRIBUTES sa, PHKEY phkResult, LPDWORD disp) {
    LSTATUS rc = Real_RegCreateKeyExA(hKey, lpSubKey, r, lpClass, opt, sam, sa, phkResult, disp);
    if (rc == ERROR_SUCCESS && phkResult) {
        std::wstring sub;
        if (lpSubKey) {
            int len = MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, nullptr, 0);
            sub.resize(len > 0 ? len - 1 : 0);
            if (len > 0) MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, sub.data(), len);
        }
        RememberKeyPath(hKey, sub, *phkResult);
    }
    return rc;
}

LSTATUS WINAPI Hook_RegCloseKey(HKEY hKey) {
    ForgetKeyPath(hKey);
    return Real_RegCloseKey(hKey);
}

LSTATUS WINAPI Hook_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                                      LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    const Supplier* sup = lpValueName ? FindFakeSupplier(hKey, lpValueName) : nullptr;
    if (!sup) {
        return Real_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }

    std::wstring fake = (*sup)();
    DWORD neededBytes = (DWORD)((fake.size() + 1) * sizeof(wchar_t));

    if (lpType) *lpType = REG_SZ;

    if (!lpData) {
        if (lpcbData) *lpcbData = neededBytes;
        return ERROR_SUCCESS;
    }
    if (!lpcbData) return ERROR_INVALID_PARAMETER;
    if (*lpcbData < neededBytes) {
        *lpcbData = neededBytes;
        return ERROR_MORE_DATA;
    }
    memcpy(lpData, fake.c_str(), neededBytes);
    *lpcbData = neededBytes;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI Hook_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                      LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    std::wstring wValueName;
    if (lpValueName) {
        int len = MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, nullptr, 0);
        wValueName.resize(len > 0 ? len - 1 : 0);
        if (len > 0) MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, wValueName.data(), len);
    }
    const Supplier* sup = lpValueName ? FindFakeSupplier(hKey, wValueName) : nullptr;
    if (!sup) {
        return Real_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }

    std::wstring fakeW = (*sup)();
    int aLen = WideCharToMultiByte(CP_ACP, 0, fakeW.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string fakeA(aLen > 0 ? aLen - 1 : 0, '\0');
    if (aLen > 0) WideCharToMultiByte(CP_ACP, 0, fakeW.c_str(), -1, fakeA.data(), aLen, nullptr, nullptr);
    DWORD neededBytes = (DWORD)(fakeA.size() + 1);

    if (lpType) *lpType = REG_SZ;
    if (!lpData) {
        if (lpcbData) *lpcbData = neededBytes;
        return ERROR_SUCCESS;
    }
    if (!lpcbData) return ERROR_INVALID_PARAMETER;
    if (*lpcbData < neededBytes) {
        *lpcbData = neededBytes;
        return ERROR_MORE_DATA;
    }
    memcpy(lpData, fakeA.c_str(), neededBytes);
    *lpcbData = neededBytes;
    return ERROR_SUCCESS;
}

template <typename T>
bool CreateAndEnable(T target, T detour, T* original) {
    if (MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                       reinterpret_cast<void**>(original)) != MH_OK)
        return false;
    return MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK;
}

} // namespace

bool RegHook_Install() {
    // 注意：MH_Initialize()/MH_Uninitialize() 由 dllmain.cpp 统一调用一次，
    // 这里不要重复调用，否则会和 wmi_hook 互相冲突。
    bool ok = true;
    ok &= CreateAndEnable(&RegOpenKeyExW, &Hook_RegOpenKeyExW, &Real_RegOpenKeyExW);
    ok &= CreateAndEnable(&RegOpenKeyExA, &Hook_RegOpenKeyExA, &Real_RegOpenKeyExA);
    ok &= CreateAndEnable(&RegCreateKeyExW, &Hook_RegCreateKeyExW, &Real_RegCreateKeyExW);
    ok &= CreateAndEnable(&RegCreateKeyExA, &Hook_RegCreateKeyExA, &Real_RegCreateKeyExA);
    ok &= CreateAndEnable(&RegCloseKey, &Hook_RegCloseKey, &Real_RegCloseKey);
    ok &= CreateAndEnable(&RegQueryValueExW, &Hook_RegQueryValueExW, &Real_RegQueryValueExW);
    ok &= CreateAndEnable(&RegQueryValueExA, &Hook_RegQueryValueExA, &Real_RegQueryValueExA);
    return ok;
}

void RegHook_Uninstall() {
    MH_DisableHook(reinterpret_cast<void*>(&RegOpenKeyExW));
    MH_DisableHook(reinterpret_cast<void*>(&RegOpenKeyExA));
    MH_DisableHook(reinterpret_cast<void*>(&RegCreateKeyExW));
    MH_DisableHook(reinterpret_cast<void*>(&RegCreateKeyExA));
    MH_DisableHook(reinterpret_cast<void*>(&RegCloseKey));
    MH_DisableHook(reinterpret_cast<void*>(&RegQueryValueExW));
    MH_DisableHook(reinterpret_cast<void*>(&RegQueryValueExA));
}
