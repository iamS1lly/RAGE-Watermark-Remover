#include "pch.h"
#include "rfwm_log.h"
#include "rfwm_config.h"
#include "rfwm_hook.h"

DWORD WINAPI MainThread(LPVOID) {
    LOG("RFWM v1.4.0");
    LoadConfig();

    while (!GetModuleHandleA("font-renderer.dll")) {
        Sleep(500);
    }

    InstallFontRendererHook();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_ourModule = hModule;
        InitLogPath();
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}