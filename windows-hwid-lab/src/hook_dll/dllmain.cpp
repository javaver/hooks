#include <windows.h>
#include <MinHook.h>
#include "../fake_data/fake_data.h"
#include "../reg_hook/reg_hook.h"
#include "../wmi_hook/wmi_hook.h"

namespace {

void InstallAllHooks() {
    FakeData_Init();

    if (MH_Initialize() != MH_OK) {
        OutputDebugStringW(L"[hook_dll] MH_Initialize failed\n");
        return;
    }
    if (!RegHook_Install()) OutputDebugStringW(L"[hook_dll] RegHook_Install failed\n");
    if (!WmiHook_Install()) OutputDebugStringW(L"[hook_dll] WmiHook_Install failed\n");

    OutputDebugStringW(L"[hook_dll] hooks installed\n");
}

void UninstallAllHooks() {
    RegHook_Uninstall();
    WmiHook_Uninstall();
    MH_Uninitialize();
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            InstallAllHooks();
            break;
        case DLL_PROCESS_DETACH:
            UninstallAllHooks();
            break;
        default:
            break;
    }
    return TRUE;
}
