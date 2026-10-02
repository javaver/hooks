#include <windows.h>
#include <MinHook.h>
#include "../probe_hook/probe_hook.h"

namespace {

void InstallProbe() {
    if (MH_Initialize() != MH_OK) {
        OutputDebugStringW(L"[probe_dll] MH_Initialize failed\n");
        return;
    }
    if (!ProbeHook_Install()) OutputDebugStringW(L"[probe_dll] ProbeHook_Install failed\n");
}

void UninstallProbe() {
    ProbeHook_Uninstall();
    MH_Uninitialize();
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            InstallProbe();
            break;
        case DLL_PROCESS_DETACH:
            UninstallProbe();
            break;
        default:
            break;
    }
    return TRUE;
}
