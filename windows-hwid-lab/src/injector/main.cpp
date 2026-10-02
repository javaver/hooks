// injector.exe <target.exe> <hook_dll.dll>
//
// 用挂起模式创建目标进程，向其中写入 DLL 路径字符串，
// 通过 CreateRemoteThread + LoadLibraryW 把 hook_dll.dll 注入进去，
// 再恢复主线程执行。是最经典、最常见的用户态 DLL 注入手法之一。

#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"用法: injector.exe <target.exe> <hook_dll.dll>\n";
        return 1;
    }

    std::wstring targetPath = argv[1];
    std::wstring dllPath = std::filesystem::absolute(argv[2]).wstring();

    if (!std::filesystem::exists(dllPath)) {
        std::wcerr << L"找不到 DLL: " << dllPath << L"\n";
        return 1;
    }

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(targetPath.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                         &si, &pi)) {
        std::wcerr << L"CreateProcess 失败, err=" << GetLastError() << L"\n";
        return 1;
    }

    bool ok = false;
    do {
        SIZE_T dllPathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
        LPVOID remoteMem = VirtualAllocEx(pi.hProcess, nullptr, dllPathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remoteMem) {
            std::wcerr << L"VirtualAllocEx 失败, err=" << GetLastError() << L"\n";
            break;
        }
        if (!WriteProcessMemory(pi.hProcess, remoteMem, dllPath.c_str(), dllPathBytes, nullptr)) {
            std::wcerr << L"WriteProcessMemory 失败, err=" << GetLastError() << L"\n";
            break;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<void*>(GetProcAddress(kernel32, "LoadLibraryW")));

        HANDLE hThread = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLibraryW, remoteMem, 0, nullptr);
        if (!hThread) {
            std::wcerr << L"CreateRemoteThread 失败, err=" << GetLastError() << L"\n";
            break;
        }
        WaitForSingleObject(hThread, INFINITE);

        // 注意：远程线程的退出码只有 32 位，在 x64 进程里这是 LoadLibraryW 返回的
        // HMODULE 被截断后的低 32 位，这里只用它判断"是否为 0（失败）"，
        // 不代表真实的模块地址。
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        CloseHandle(hThread);

        if (exitCode == 0) {
            std::wcerr << L"远程 LoadLibraryW 返回 0，注入可能失败\n";
            break;
        }

        std::wcout << L"已注入 " << dllPath << L"，HMODULE=0x" << std::hex << exitCode << std::dec << L"\n";
        ok = true;
    } while (false);

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return ok ? 0 : 1;
}
